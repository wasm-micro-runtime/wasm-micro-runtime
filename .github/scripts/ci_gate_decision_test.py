#!/usr/bin/env python3
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Offline fixtures for ci_gate_decision.Gate.decide().

    cd .github/scripts && python3 -m unittest ci_gate_decision_test -v

Every API answer is canned, so each scenario the gate has to handle can be
exercised without waiting for a real event.
"""
import unittest

import ci_gate_decision as gate

try:  # the path filter parses the caller's workflow, which needs pyyaml
    import pr_touches_paths  # noqa: F401

    HAS_PYYAML = True
except ImportError:
    HAS_PYYAML = False


APPROVED_RULES = [
    {
        "type": "pull_request",
        "parameters": {"required_approving_review_count": 1, "require_last_push_approval": True},
    }
]


def check_run(conclusion="success", suite=1, started="2026-09-20T01:00:00Z",
              completed="2026-09-20T01:10:00Z", app="github-actions", status="completed"):
    return {
        "status": status,
        "conclusion": conclusion,
        "app": {"slug": app},
        "check_suite": {"id": suite},
        "started_at": started,
        "completed_at": completed,
    }


class FakeClient:
    """Canned API answers, plus the calls each test wants to go wrong.

    `failing` names the calls that raise ApiError (a read the gate expects to
    fail); `broken` names the calls that raise anything else (a gate bug).
    `calls` records every call, so a test can assert a read was never made.
    """

    def __init__(self, reviews=(), timeline=(), files=("core/iwasm/x.c",), rules=(),
                 review_decision="APPROVED", opinions=(), check_runs=(), suites=None, failing=(),
                 broken=()):
        self.answers = {
            "reviews": list(reviews),
            "timeline": list(timeline),
            "changed_files": list(files),
            "rules_for_branch": list(rules),
            "approval_state": (review_decision, list(opinions)),
            "check_runs": list(check_runs),
            "check_suite": None,
        }
        self.suites = suites or {}
        self.failing = set(failing)
        self.broken = set(broken)
        self.calls = []

    def _answer(self, name):
        self.calls.append(name)
        if name in self.failing:
            raise gate.ApiError(f"canned failure in {name}")
        if name in self.broken:
            raise RuntimeError(f"canned bug in {name}")
        return self.answers[name]

    def reviews(self, number):
        return self._answer("reviews")

    def timeline(self, number):
        return self._answer("timeline")

    def changed_files(self, number):
        return self._answer("changed_files")

    def rules_for_branch(self, branch):
        return self._answer("rules_for_branch")

    def approval_state(self, number):
        return self._answer("approval_state")

    def check_runs(self, head_sha, check_name):
        return self._answer("check_runs")

    def check_suite(self, suite_id):
        self._answer("check_suite")
        return self.suites.get(suite_id, {"conclusion": "success"})


def pr_event(number=7, head="headsha", base="main"):
    return {"pull_request": {"number": number, "head": {"sha": head}, "base": {"ref": base}}}


def review_event(state="approved", **kwargs):
    event = pr_event(**kwargs)
    event["review"] = {"state": state}
    return event


def decide(event_name, event, client, check_name="ubuntu CI", paths="core/**\n",
           run_attempt=1, workflow_ref=""):
    """One gate decision, with the caller's inputs defaulted to `ubuntu CI`.

    Returns (Decision, [warning messages]); the defaults describe a pipeline
    with a canonical name that cares about `core/**`.
    """
    warnings = []
    decision = gate.Gate(
        client=client,
        check_name=check_name,
        paths=paths,
        workflow_ref=workflow_ref,
        run_attempt=run_attempt,
        warn=warnings.append,
    ).decide(event_name, event)
    return decision, warnings


class PushEvents(unittest.TestCase):
    def test_fork_push_runs(self):
        decision, _ = decide("push", {"repository": {"fork": True}}, FakeClient())
        self.assertEqual((decision.run, decision.state, decision.check_name),
                         (True, "push", "ubuntu CI on push"))

    def test_upstream_push_does_not_run(self):
        decision, _ = decide("push", {"repository": {"fork": False}}, FakeClient())
        self.assertEqual((decision.run, decision.state, decision.check_name),
                         (False, "push", "ubuntu CI on push"))

    def test_other_events_always_run(self):
        decision, _ = decide("workflow_dispatch", {}, FakeClient())
        self.assertEqual((decision.run, decision.state, decision.check_name),
                         (True, "run", "ubuntu CI"))


class QuestionOneFullRun(unittest.TestCase):
    def test_review_comment_waits_when_current_state_is_not_approved(self):
        client = FakeClient(review_decision="CHANGES_REQUESTED")
        decision, _ = decide("pull_request_review", review_event("commented"), client)
        self.assertEqual((decision.run, decision.state, decision.check_name),
                         (False, "awaiting", "ubuntu CI awaiting approval"))
        self.assertIn("approval_state", client.calls)

    def test_approval_event_waits_when_current_state_is_not_approved(self):
        client = FakeClient(review_decision="CHANGES_REQUESTED")
        decision, _ = decide("pull_request_review", review_event("approved"), client)
        self.assertEqual((decision.run, decision.state, decision.check_name),
                         (False, "awaiting", "ubuntu CI awaiting approval"))

    def test_synchronize_without_approval_waits(self):
        client = FakeClient(review_decision="REVIEW_REQUIRED", rules=APPROVED_RULES)
        decision, _ = decide("pull_request", pr_event(), client)
        self.assertEqual((decision.run, decision.state), (False, "awaiting"))
        self.assertNotIn("check_runs", client.calls)

    def test_rebase_grid_runs(self):
        # require_last_push_approval dropped reviewDecision to REVIEW_REQUIRED,
        # but the reviewer's approval survived: that is the rebase case.
        client = FakeClient(review_decision="REVIEW_REQUIRED", opinions=["APPROVED"],
                            rules=APPROVED_RULES)
        decision, _ = decide("pull_request", pr_event(), client)
        self.assertEqual((decision.run, decision.state, decision.check_name),
                         (True, "run", "ubuntu CI"))

    def test_grid_without_require_last_push_approval_waits(self):
        rules = [{"type": "pull_request",
                  "parameters": {"required_approving_review_count": 1,
                                 "require_last_push_approval": False}}]
        client = FakeClient(review_decision="REVIEW_REQUIRED", opinions=["APPROVED"], rules=rules)
        decision, _ = decide("pull_request", pr_event(), client)
        self.assertEqual((decision.run, decision.state), (False, "awaiting"))

    def test_ruleset_approval_count_is_honoured(self):
        rules = [{"type": "pull_request",
                  "parameters": {"required_approving_review_count": 2,
                                 "require_last_push_approval": True}}]
        one = FakeClient(review_decision="REVIEW_REQUIRED", opinions=["APPROVED"], rules=rules)
        self.assertEqual(decide("pull_request", pr_event(), one)[0].run, False)
        two = FakeClient(review_decision="REVIEW_REQUIRED", opinions=["APPROVED", "APPROVED"],
                         rules=rules)
        self.assertEqual(decide("pull_request", pr_event(), two)[0].run, True)

    def test_rules_failure_falls_back_to_defaults(self):
        client = FakeClient(review_decision="REVIEW_REQUIRED", opinions=["APPROVED"],
                            failing=("rules_for_branch",))
        decision, warnings = decide("pull_request", pr_event(), client)
        self.assertEqual(decision.run, True)
        self.assertTrue(warnings)

    def test_approval_read_failure_lets_ci_run(self):
        client = FakeClient(failing=("approval_state",))
        decision, warnings = decide("pull_request", pr_event(), client)
        self.assertEqual(decision.run, True)
        self.assertTrue(warnings)

    def test_approval_on_an_unapproved_pr_runs(self):
        client = FakeClient(files=("core/iwasm/x.c",))
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual((decision.run, decision.state, decision.check_name),
                         (True, "run", "ubuntu CI"))


@unittest.skipUnless(HAS_PYYAML, "pyyaml is needed to read path globs")
class QuestionTwoPaths(unittest.TestCase):
    def test_path_skip_still_publishes_the_canonical_name(self):
        client = FakeClient(files=("docs/readme.md",))
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual((decision.run, decision.state, decision.check_name),
                         (False, "skipped", "ubuntu CI"))
        self.assertIn("check_runs", client.calls)
        self.assertNotIn("check_suite", client.calls)

    def test_relevant_path_runs(self):
        client = FakeClient(files=("core/iwasm/x.c",))
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual(decision.run, True)

    def test_path_glob_failure_treats_the_pr_as_relevant(self):
        client = FakeClient(failing=("changed_files",))
        decision, warnings = decide("pull_request_review", review_event(), client)
        self.assertEqual(decision.run, True)
        self.assertTrue(warnings)


class QuestionThreeStandingVerdict(unittest.TestCase):
    def test_existing_verdict_is_reused_before_path_filtering(self):
        client = FakeClient(files=("docs/readme.md",), check_runs=[check_run("success")])
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual((decision.run, decision.state, decision.check_name, decision.verdict),
                         (False, "concluded", "ubuntu CI", "success"))
        self.assertNotIn("changed_files", client.calls)

    def test_review_comment_replays_verdict_while_current_state_is_approved(self):
        client = FakeClient(check_runs=[check_run("success")])
        decision, _ = decide("pull_request_review", review_event("commented"), client)
        self.assertEqual((decision.run, decision.state, decision.check_name, decision.verdict),
                         (False, "concluded", "ubuntu CI", "success"))

    def test_existing_success_is_published_again(self):
        # The canonical name has to be written by this run too: GitHub reads a
        # required check from the latest run of the workflow that reports it,
        # so an alias here would leave the required check at "Expected".
        client = FakeClient(check_runs=[check_run("success")])
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual((decision.run, decision.state, decision.check_name, decision.verdict),
                         (False, "concluded", "ubuntu CI", "success"))

    def test_existing_failure_is_published_again_as_a_failure(self):
        # A red must stay red: the aggregation job fails on this verdict
        # instead of publishing its own (green) conclusion.
        client = FakeClient(check_runs=[check_run("failure")])
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual((decision.run, decision.state, decision.check_name, decision.verdict),
                         (False, "concluded", "ubuntu CI", "failure"))

    def test_killed_run_leaves_no_verdict(self):
        # #5083: the record says failure, but its check suite was cancelled.
        client = FakeClient(check_runs=[check_run("failure", suite=2)],
                            suites={2: {"conclusion": "cancelled"}})
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual((decision.run, decision.state, decision.check_name),
                         (True, "run", "ubuntu CI"))

    def test_older_failure_survives_a_killed_residue(self):
        client = FakeClient(
            check_runs=[
                check_run("failure", suite=2, started="2026-09-20T02:00:00Z"),
                check_run("failure", suite=1, started="2026-09-20T01:00:00Z"),
            ],
            suites={1: {"conclusion": "failure"}, 2: {"conclusion": "cancelled"}},
        )
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual((decision.run, decision.state, decision.check_name, decision.verdict),
                         (False, "concluded", "ubuntu CI", "failure"))

    def test_only_actions_records_are_verdicts(self):
        client = FakeClient(check_runs=[check_run("success", app="some-other-app")])
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual(decision.run, True)

    def test_dismissal_after_the_verdict_invalidates_it(self):
        client = FakeClient(
            reviews=[{"state": "DISMISSED"}],
            timeline=[{"event": "review_dismissed", "created_at": "2026-09-20T02:00:00Z"}],
            check_runs=[check_run("success")],
        )
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual((decision.run, decision.state), (True, "run"))

    def test_dismissal_before_the_verdict_keeps_it(self):
        client = FakeClient(
            reviews=[{"state": "DISMISSED"}],
            timeline=[{"event": "review_dismissed", "created_at": "2026-09-19T23:00:00Z"}],
            check_runs=[check_run("success")],
        )
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual((decision.run, decision.state), (False, "concluded"))

    def test_dismissed_verdict_without_a_timestamp_is_not_reused(self):
        client = FakeClient(reviews=[{"state": "DISMISSED"}], check_runs=[check_run("success")])
        decision, _ = decide("pull_request_review", review_event(), client)
        self.assertEqual(decision.run, True)

    def test_no_dismissed_review_skips_the_timeline(self):
        client = FakeClient(reviews=[{"state": "APPROVED"}], check_runs=[check_run("success")])
        decide("pull_request_review", review_event(), client)
        self.assertNotIn("timeline", client.calls)

    def test_explicit_rerun_invalidates_the_verdict(self):
        client = FakeClient(check_runs=[check_run("success")])
        decision, _ = decide("pull_request_review", review_event(), client, run_attempt=2)
        self.assertEqual((decision.run, decision.state), (True, "run"))
        self.assertNotIn("check_runs", client.calls)

    def test_check_run_read_failure_runs(self):
        client = FakeClient(failing=("check_runs",))
        decision, warnings = decide("pull_request_review", review_event(), client)
        self.assertEqual(decision.run, True)
        self.assertTrue(warnings)


class GateFailure(unittest.TestCase):
    def test_a_gate_bug_still_lets_ci_run(self):
        # Fail-open: an unexpected exception must never leave a PR without CI.
        client = FakeClient(broken=("approval_state",))
        decision, warnings = decide("pull_request", pr_event(), client)
        self.assertEqual((decision.run, decision.state, decision.check_name),
                         (True, "run", "ubuntu CI"))
        self.assertTrue(warnings)


class PipelinesWithoutCanonicalName(unittest.TestCase):
    def test_runs_and_publishes_no_name(self):
        client = FakeClient(check_runs=[check_run("success")])
        decision, _ = decide("pull_request_review", review_event(), client, check_name="")
        self.assertEqual((decision.run, decision.state, decision.check_name), (True, "run", ""))
        self.assertNotIn("check_runs", client.calls)


class CheckNames(unittest.TestCase):
    def test_canonical_states(self):
        self.assertEqual(gate.check_name_for("run", "ubuntu CI"), "ubuntu CI")
        self.assertEqual(gate.check_name_for("skipped", "ubuntu CI"), "ubuntu CI")
        # concluded republishes the canonical name, see _standing_verdict
        self.assertEqual(gate.check_name_for("concluded", "ubuntu CI"), "ubuntu CI")

    def test_aliases(self):
        self.assertEqual(gate.check_name_for("push", "ubuntu CI"), "ubuntu CI on push")
        self.assertEqual(gate.check_name_for("awaiting", "ubuntu CI"), "ubuntu CI awaiting approval")

    def test_unknown_state_stays_an_alias(self):
        self.assertEqual(gate.check_name_for("interrupted", "ubuntu CI"), "ubuntu CI interrupted")
        self.assertNotEqual(gate.check_name_for("something-new", "ubuntu CI"), "ubuntu CI")

    def test_without_a_canonical_name_there_is_no_name(self):
        self.assertEqual(gate.check_name_for("awaiting", ""), "")


if __name__ == "__main__":
    unittest.main()
