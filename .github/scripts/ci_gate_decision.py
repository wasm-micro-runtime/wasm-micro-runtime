#!/usr/bin/env python3
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""The CI approval gate's decision, as a standalone script.

The policy it implements is described in .github/workflows/README.md. Two
properties are guaranteed here rather than there:

  * Fail-open. Every unreadable API answer, and any bug in this file, degrades
    towards "run CI": minutes are cheaper than a missing or faked verdict.
  * Stateless. Only the current approval state and the check runs on the
    current head commit are read, never what an earlier run did.
"""
import argparse
import json
import os
import sys
# urllib, not requests: a missing or broken third-party dependency would make
# this file fail to import, and a gate that cannot start is a gate that blocks
# every PR. The stdlib always imports, so the fail-open promise above holds.
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Callable

# Conclusions that count as a verdict. Anything else - a cancelled check run,
# or whatever a killed run left behind - is not a verdict.
VALID_CONCLUSIONS = ("success", "failure")

# States that may publish the canonical check name:
#   - `run` and `skipped` really produced (or explicitly did not need) a
#     verdict, so the aggregation job's own conclusion is the verdict;
#   - `concluded` replays the verdict that still stands on this head commit.
#     It has to publish the canonical name too: GitHub reads a required check
#     from the *latest run of the workflow that reports it*, so a newer run
#     that only wrote an alias would hide the standing verdict and leave the
#     required check at "Expected" - see `_standing_verdict`.
# Every other state publishes an alias, so a gate that decided not to run (or
# was interrupted) can never satisfy the required check by accident.
CANONICAL_STATES = ("run", "skipped", "concluded")
ALIAS_SUFFIX = {"push": "on push", "awaiting": "awaiting approval"}

# Used when the ruleset cannot be read; these mirror ruleset 2034258.
DEFAULT_REQUIRED_APPROVALS = 1
DEFAULT_REQUIRE_LAST_PUSH_APPROVAL = True

# Only check runs published by Actions may be read as this repository's own
# verdicts; the ruleset also binds the required names to this integration.
GITHUB_ACTIONS_APP = "github-actions"


class ApiError(RuntimeError):
    """A read that did not produce an answer."""


@dataclass
class Decision:
    run: bool
    state: str
    check_name: str
    message: str
    # Only set for state="concluded": the verdict the aggregation job has to
    # publish. A "failure" means the job must fail on purpose, so the red stays
    # red instead of being replaced by the job's own (skipped, hence green)
    # result.
    verdict: str = ""


def check_name_for(state, canonical):
    """The check name the aggregation job must publish.

    `run`, `skipped` and `concluded` write the canonical name; `push`,
    `awaiting` and every unknown state write an alias. An unknown state falls
    back to an alias too, so adding a state later can never satisfy the
    required check by accident.

    An empty `canonical` gives an empty name in every state: it means the
    caller has no aggregation job and therefore publishes no check at all
    (codeql, nightly_run, spec_test_on_nuttx and wamr_wasi_extensions). A
    caller that does have an aggregation job must pass its check name, or that
    job falls back to its `... interrupted` alias and the required check never
    appears.
    """
    if state in CANONICAL_STATES:
        return canonical
    if not canonical:
        return ""
    return f"{canonical} {ALIAS_SUFFIX.get(state, state)}"


def _next_page(link_header):
    for part in (link_header or "").split(","):
        url, _, rel = part.partition(";")
        if 'rel="next"' in rel:
            return url.strip().strip("<>")
    return None


class Client:
    """The few read-only GitHub API calls the gate needs.

    The token must carry `checks: read` and `pull-requests: read`; without them
    every read raises ApiError and the gate falls open.
    """

    def __init__(self, repo, token, api_url=None, timeout=30):
        self.repo = repo
        self.token = token
        self.api_url = (
            api_url or os.environ.get("GITHUB_API_URL") or "https://api.github.com"
        ).rstrip("/")
        self.timeout = timeout

    def _call(self, method, url, payload=None):
        request = urllib.request.Request(url, data=payload, method=method)
        request.add_header("Authorization", f"Bearer {self.token}")
        request.add_header("Accept", "application/vnd.github+json")
        request.add_header("X-GitHub-Api-Version", "2022-11-28")
        request.add_header("User-Agent", "wamr-ci-gate")
        if payload is not None:
            request.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                body = response.read()
                link = response.headers.get("Link") or ""
        except urllib.error.HTTPError as err:
            raise ApiError(f"{method} {url} -> HTTP {err.code}: {err.read()[:200]}") from None
        except (urllib.error.URLError, OSError) as err:
            raise ApiError(f"{method} {url} -> {err}") from None
        try:
            return json.loads(body or b"null"), link
        except ValueError as err:
            raise ApiError(f"{method} {url} -> invalid JSON: {err}") from None

    def _rest(self, path):
        return f"{self.api_url}/repos/{self.repo}{path}"

    def _pages(self, path, key=None):
        url = self._rest(path)
        items = []
        while url:
            page, link = self._call("GET", url)
            if key:
                items.extend((page or {}).get(key) or [])
            else:
                items.extend(page or [])
            url = _next_page(link)
        return items

    def reviews(self, number):
        return self._pages(f"/pulls/{number}/reviews?per_page=100")

    def timeline(self, number):
        return self._pages(f"/issues/{number}/timeline?per_page=100")

    def changed_files(self, number):
        return [
            entry["filename"]
            for entry in self._pages(f"/pulls/{number}/files?per_page=100")
            if entry.get("filename")
        ]

    def rules_for_branch(self, branch):
        return self._pages(f"/rules/branches/{urllib.parse.quote(branch, safe='')}")

    def check_runs(self, head_sha, check_name):
        query = urllib.parse.urlencode({"check_name": check_name, "per_page": 100})
        return self._pages(f"/commits/{head_sha}/check-runs?{query}", key="check_runs")

    def check_suite(self, suite_id):
        page, _ = self._call("GET", self._rest(f"/check-suites/{suite_id}"))
        return page or {}

    def approval_state(self, number):
        """(reviewDecision, [state of each reviewer's latest opinionated review])."""
        owner, _, name = self.repo.partition("/")
        query = (
            "query($owner:String!,$repo:String!,$number:Int!){"
            "repository(owner:$owner,name:$repo){pullRequest(number:$number){"
            "reviewDecision latestOpinionatedReviews(first:100){nodes{state}}}}}"
        )
        payload = json.dumps(
            {"query": query, "variables": {"owner": owner, "repo": name, "number": number}}
        ).encode()
        data, _ = self._call("POST", self.api_url + "/graphql", payload)
        pull = (((data or {}).get("data") or {}).get("repository") or {}).get("pullRequest")
        if not pull:
            raise ApiError(f"GraphQL returned no pullRequest: {data}")
        nodes = (pull.get("latestOpinionatedReviews") or {}).get("nodes") or []
        return pull.get("reviewDecision"), [node.get("state") for node in nodes]


def warn(message):
    print(f"::warning title=approval gate::{message}")


@dataclass
class Gate:
    """One pipeline's gate: the caller's inputs plus the reads they need.

    `check_name` is the caller's canonical check name, empty when the caller
    has no aggregation job; `paths` are its path globs, empty to fall back to
    the `on.push.paths` of the workflow named by `workflow_ref`. `warn`
    surfaces degraded reads in the run UI.
    """

    client: Client
    check_name: str = ""
    paths: str = ""
    workflow_ref: str = ""
    run_attempt: int = 1
    warn: Callable[[str], None] = warn

    def decide(self, event_name, event) -> Decision:
        """The gate's answer for one event; never raises."""
        if event_name == "push":
            fork = bool((event.get("repository") or {}).get("fork"))
            return self._decision(
                fork,
                "push",
                "running CI for fork push"
                if fork
                else "skipping CI for upstream push; approval triggers CI",
            )

        if event_name not in ("pull_request_review", "pull_request"):
            return self._decision(True, "run", f"running CI for {event_name} event")

        try:
            return self._decide_for_pull_request(event_name, event)
        except Exception as err:  # noqa: BLE001 - a gate bug must never block a PR
            self.warn(f"the gate could not decide ({type(err).__name__}: {err}); running CI")
            return self._decision(True, "run", "running CI because the gate could not decide")

    def _decision(self, run, state, message, verdict="") -> Decision:
        return Decision(run, state, check_name_for(state, self.check_name), message, verdict)

    def _decide_for_pull_request(self, event_name, event) -> Decision:
        pull = event.get("pull_request") or {}
        number = pull.get("number")
        head_sha = (pull.get("head") or {}).get("sha")

        # Review events are only triggers to re-evaluate the PR. A submitted
        # comment can leave an earlier approval intact, and an approval event
        # can arrive after the current approval has been dismissed.
        allowed, reason = self._cleared_for_a_full_run(pull, number)
        if not allowed:
            return self._decision(False, "awaiting", f"skipping CI because {reason}")

        # Verdict reuse depends on the head commit and canonical check name,
        # not on which paths this pipeline filters.
        verdict = self._standing_verdict(number, head_sha) if self.check_name else None
        if verdict:
            # The canonical name has to be published again, not aliased away.
            # GitHub reads a required check from the latest run of the workflow
            # that reports it, so a newer run that only wrote an alias leaves
            # the required check at "Expected" and blocks the merge even though
            # the verdict is still there (#33 in the fork). A red is replayed by
            # failing the aggregation job, a green by letting it pass.
            return self._decision(
                False,
                "concluded",
                f"{head_sha} already has a verdict ({verdict}); publishing it again",
                verdict=verdict,
            )

        if not self._touches_paths(number):
            return self._decision(
                False, "skipped", "skipping CI because the PR does not touch relevant paths"
            )

        return self._decision(True, "run", f"running CI for approved relevant PR SHA {head_sha}")

    def _ruleset_params(self, base_ref):
        """The approval parameters the rulesets put on the PR's base branch."""
        if not base_ref:
            return {}
        try:
            rules = self.client.rules_for_branch(base_ref)
        except ApiError as err:
            self.warn(f"rules for {base_ref} unavailable ({err}); using the built-in defaults")
            return {}
        params = {}
        for rule in rules:
            parameters = rule.get("parameters") or {}
            if rule.get("type") == "pull_request":
                params["required_approving_review_count"] = max(
                    params.get("required_approving_review_count", 0),
                    int(parameters.get("required_approving_review_count") or 0),
                )
                params["require_last_push_approval"] = bool(
                    params.get("require_last_push_approval")
                ) or bool(parameters.get("require_last_push_approval"))
            elif rule.get("type") == "required_status_checks":
                params.setdefault("required_checks", []).extend(
                    check.get("context") for check in parameters.get("required_status_checks") or []
                )
        return params

    def _cleared_for_a_full_run(self, pull, number):
        """Question 1: is this PR approved enough for a full CI run?"""
        params = self._ruleset_params((pull.get("base") or {}).get("ref") or "")
        required_checks = params.get("required_checks")
        if self.check_name and required_checks is not None and self.check_name not in required_checks:
            # Visibility only: most pipelines publish a canonical name that no
            # ruleset lists. Nothing is wrong, it is just not required.
            listed = ", ".join(required_checks) or "none"
            print(f"[gate] {self.check_name!r} is not a required check ({listed} are)")
        try:
            decision, opinions = self.client.approval_state(number)
        except ApiError as err:
            self.warn(f"approval state unavailable ({err}); allowing CI to run")
            return True, "the approval state could not be read"
        if decision == "APPROVED":
            return True, "reviewDecision is APPROVED"
        required = params.get("required_approving_review_count") or DEFAULT_REQUIRED_APPROVALS
        last_push = params.get("require_last_push_approval", DEFAULT_REQUIRE_LAST_PUSH_APPROVAL)
        approved = sum(1 for state in opinions if state == "APPROVED")
        if last_push and approved >= required:
            # Rebase and "Update branch" land here: the review survives, but
            # require_last_push_approval resets reviewDecision. A push that
            # really changes the code is dismissed by GitHub itself, so it never
            # reaches this branch with an approval left to count.
            return True, (
                f"{approved} approval(s) survive and require_last_push_approval needs a fresh one"
            )
        return False, (
            f"reviewDecision is {decision or 'unknown'} and {approved} approval(s) survive"
            f" (need {required})"
        )

    def _touches_paths(self, number):
        """Question 2: does the PR touch the paths this pipeline cares about?"""
        # Imported here so the rest of the gate works even without pyyaml: only
        # the path filter has to parse the caller's workflow file.
        import pr_touches_paths  # noqa: E402

        try:
            changed = self.client.changed_files(number)
            patterns = self._patterns(pr_touches_paths)
        except Exception as err:  # any failure means "run", see the module docstring
            self.warn(f"changed files or path globs unavailable ({err}); treating the PR as relevant")
            return True
        return pr_touches_paths.touches(patterns, changed)

    def _patterns(self, pr_touches_paths):
        """Read the caller's explicit globs or its workflow's push paths."""
        if self.paths:
            return [line.strip() for line in self.paths.splitlines() if line.strip()]
        return pr_touches_paths.read_patterns(None, self.workflow_ref)

    def _dismissed_after(self, number, completed_at):
        """Was an approval dismissed after that verdict was published?

        A dismissal is the one thing that invalidates a verdict on an unchanged
        head commit: `approve -> dismiss -> approve` must run CI again. The
        review list says which reviews were dismissed but keeps their original
        submission time, so the dismissal time has to come from the timeline.
        """
        if not any(review.get("state") == "DISMISSED" for review in self.client.reviews(number)):
            return False
        stamps = [
            event.get("created_at") or ""
            for event in self.client.timeline(number)
            if event.get("event") == "review_dismissed"
        ]
        if not stamps:
            return True  # dismissed, but the time is unknown: do not trust the verdict
        return max(stamps) > completed_at

    def _standing_verdict(self, number, head_sha):
        """Question 3: the conclusion that still stands on this head commit.

        The verdict found here is not just a reason to skip CI: the caller
        publishes it again on the canonical check name, because GitHub reads a
        required check from the latest run of the workflow that reports it. The
        older record alone would not keep the required check satisfied.
        """
        if self.run_attempt > 1:
            # `gh run rerun` bumps run_attempt (a single-job re-run does too,
            # but then this job does not run again): an explicit re-run wants a
            # fresh verdict whatever the head commit already says.
            print(f"[gate] run_attempt={self.run_attempt}, treating any existing verdict as invalid")
            return None
        try:
            runs = self.client.check_runs(head_sha, self.check_name)
        except ApiError as err:
            self.warn(f"check runs on {head_sha} unavailable ({err}); running CI")
            return None
        candidates = [
            check
            for check in runs
            if check.get("status") == "completed"
            and check.get("conclusion") in VALID_CONCLUSIONS
            and (check.get("app") or {}).get("slug") == GITHUB_ACTIONS_APP
        ]
        for check in sorted(candidates, key=lambda check: check.get("started_at") or "", reverse=True):
            # A record left behind by a run that cancel-in-progress killed is
            # not a verdict. Its check suite reports `cancelled` even when the
            # record itself says `failure`, which is how #5083 left a red nobody
            # could overwrite.
            suite_id = (check.get("check_suite") or {}).get("id")
            try:
                suite = self.client.check_suite(suite_id) if suite_id else {}
            except ApiError as err:
                self.warn(f"check suite {suite_id} unavailable ({err}); ignoring that record")
                continue
            if suite.get("conclusion") == "cancelled":
                continue
            try:
                if self._dismissed_after(number, check.get("completed_at") or ""):
                    return None
            except ApiError as err:
                self.warn(f"review timeline unavailable ({err}); not reusing the verdict")
                return None
            return check.get("conclusion")
        return None


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Decide whether the calling pipeline should run CI for the current event.",
        epilog="""
Input: the workflow event JSON at --event-path, the caller's --check-name and
--paths, and read-only GitHub API answers about the PR. Every option defaults
to the environment variable shown with it, so `gate.yml` passes none of them.

Output: `run=true|false`, `state=`, `check_name=` and `verdict=` appended to
$GITHUB_OUTPUT (and echoed on stdout), plus one `::notice::` explaining the
decision and a `::warning::` per degraded read. Always exits 0 - a gate that
cannot decide answers `run=true` instead of failing.

Caller contract: run from the repository root with .github checked out (this
script imports pr_touches_paths, which needs pyyaml), gate every expensive job
on `run`, name the aggregation job after `check_name`, and make that job fail
when `state=concluded` and `verdict=failure`. A caller without an aggregation
job passes no `--check-name` and ignores both outputs.
""",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--event-name", default=os.environ.get("GITHUB_EVENT_NAME", ""),
                        help="GITHUB_EVENT_NAME")
    parser.add_argument("--event-path", default=os.environ.get("GITHUB_EVENT_PATH", ""),
                        help="GITHUB_EVENT_PATH")
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY", ""),
                        help="GITHUB_REPOSITORY, as owner/name")
    parser.add_argument("--run-attempt", type=int, default=int(os.environ.get("GITHUB_RUN_ATTEMPT") or 1),
                        help="GITHUB_RUN_ATTEMPT; above 1 no existing verdict is reused")
    parser.add_argument(
        "--workflow-ref",
        default=os.environ.get("GATE_WORKFLOW_REF") or os.environ.get("GITHUB_WORKFLOW_REF", ""),
        help="GATE_WORKFLOW_REF; the workflow whose on.push.paths is the fallback path filter",
    )
    parser.add_argument("--check-name", default=os.environ.get("GATE_CHECK_NAME", ""),
                        help="GATE_CHECK_NAME; the caller's canonical check name, empty if it has none")
    parser.add_argument("--paths", default=os.environ.get("GATE_PATHS", ""),
                        help="GATE_PATHS; newline-separated path globs")
    parser.add_argument(
        "--token", default=os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN") or "",
        help="GITHUB_TOKEN; needs checks:read and pull-requests:read",
    )
    parser.add_argument("--output", default=os.environ.get("GITHUB_OUTPUT", ""),
                        help="GITHUB_OUTPUT; the file the outputs are appended to")
    args = parser.parse_args(argv)

    with open(args.event_path) as handle:
        event = json.load(handle)
    gate = Gate(
        client=Client(args.repository, args.token),
        check_name=args.check_name,
        paths=args.paths,
        workflow_ref=args.workflow_ref,
        run_attempt=args.run_attempt,
    )
    decision = gate.decide(args.event_name, event)
    lines = [
        f"run={'true' if decision.run else 'false'}",
        f"state={decision.state}",
        f"check_name={decision.check_name}",
        f"verdict={decision.verdict}",
    ]
    if args.output:
        with open(args.output, "a") as handle:
            handle.write("\n".join(lines) + "\n")
    print(f"::notice title=approval gate::{decision.message}")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
