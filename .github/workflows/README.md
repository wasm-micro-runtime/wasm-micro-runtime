# 📌 CI and Ruleset Gates Architecture

This document specifies the GitHub Actions workflow triggers, approval gates, and branch ruleset configurations for the WebAssembly Micro Runtime (WAMR) repository.

### 🛡️ Repository Ruleset Policies

| Policy                       | Configuration & Enforcement                                                                                            |
| ---------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| **Pull Request Reviews**     | Mandatory before merging.                                                                                              |
| **Commit-Level Approval**    | Requires an explicit approval targeting the **latest head commit** of the PR.                                          |
| **Approval Invalidation**    | Dismisses stale reviews when a push **changes the code**; a rebase keeps them and only needs a fresh approval for the new head (`require_last_push_approval`). |
| **Status Check Enforcement** | Requires canonical status checks to pass before merging. Current mandatory gates: `ubuntu CI` and `coding guidelines`. |
| **Merge Queue**              | **Disabled** (standard PR merge flow).                                                                                 |

> ℹ️ **Note on Check Evaluation:** GitHub evaluates required checks by their **exact check-run name**, and it reads that name from the **newest run of the workflow that reports it** - once a newer run of the same workflow exists, the older record no longer keeps the required check satisfied. Three gate outcomes therefore publish the pipeline's canonical name: a run that really executed CI, a path-filter skip, and a standing verdict that is published again (`success` as it is, `failure` by failing the aggregation job on purpose). Every other outcome publishes an alias (`... awaiting approval`, `... interrupted`), so a gate that decided not to run - or was cancelled - can never satisfy the required check by accident. The aggregation job always runs and takes its name from the gate's `check_name` output, falling back to `... interrupted` when the gate job itself was cancelled.

### ⚙️ CI Approval & Execution Logic

Real upstream CI execution is strictly **approval-gated** to optimize runner resources and enhance security. The gate answers three questions per event, in this order:

1. **Is this PR cleared for a full run?** The PR must be approved for the head commit.
2. **Does this head commit already have a verdict that still stands?** If it does, the pipeline republishes it without checking paths or rerunning CI.
3. **Does the change touch this pipeline's paths?** If not, the pipeline just passes.

The decision is stateless - it reads the current approval state and the check runs published on this head commit, never what happened earlier. That is what lets any later event heal a pipeline whose run was interrupted. The decision itself lives in [`.github/scripts/ci_gate_decision.py`](../scripts/ci_gate_decision.py); `gate.yml` only wires the event and the caller's inputs into it.

| Operational State                                  | Upstream CI Behavior                                                        | Check Name / Status                                           |
| -------------------------------------------------- | --------------------------------------------------------------------------- | ------------------------------------------------------------- |
| **PR Lifecycle Events** (`opened` / `reopened`)    | Not registered, so nothing runs.                                            | None (awaits review).                                         |
| **Approval on an unapproved PR** (relevant paths)  | Runs the pipeline for the head commit.                                      | canonical `ubuntu CI` (`success` / `failure`)                 |
| **Approval without relevant changes**              | Path filtering skips the jobs.                                              | canonical `ubuntu CI` (`success`)                             |
| **Push / rebase / "Update branch"** (`synchronize`) | Runs for the new head commit while the approval survived. A push that changes the code is dismissed by GitHub, so it waits for a fresh approval instead. | canonical when it runs, otherwise `ubuntu CI awaiting approval` |
| **Review event / unapproved sync**                 | Rechecks current approval state, not the submitted review's state. If not approved, no expensive compute is triggered; if approved, the standing verdict is reused or CI runs. | awaiting alias when unapproved; canonical when approved |
| **Same head commit already has a verdict**         | Bypasses redundant CI execution, and **publishes that verdict again** - an older record on its own is no longer read. | canonical `ubuntu CI` (the standing `success`, or `failure` replayed) |
| **Interrupted** (the gate job was cancelled)       | No new record on the canonical name; the next event re-runs the pipeline.   | alias `ubuntu CI interrupted` (`success`)                     |

#### What Counts as a Verdict

| Counts                                                                    | Does not count                                                                                                                     |
| ------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| `success` (including a path-filter skip) and `failure` on the head commit. | `cancelled`; the `failure` a killed run left behind, detected through its check suite; a verdict published before an approval was dismissed; a verdict an explicit re-run was asked to redo (`run_attempt > 1`). |

That last row is what makes an explicit re-run the manual fallback below.

Reusing a verdict means **writing it again, not writing an alias**: GitHub has to see the name in the newest run of that workflow. A gate that only published `ubuntu CI approved` left the required check at *Expected - waiting for status to be reported* even though a green `ubuntu CI` record was still on the commit (fork PR #33, 2026-09-22). A replayed `failure` is published the same way it was earned - the aggregation job fails on purpose - so a red can never be turned green by the reuse path.

#### Manual Fallbacks

The gate is stateless, so the normal repair is to produce a new event: push, or approve again. Reach for a manual fallback only when no such event is available or wanted.

| Situation                                                               | What to do                                                                                                                                                                     |
| ----------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **A required check is red or missing on the current head commit**, and the code has not changed | **Actions → the failed run → Re-run all jobs** (`gh run rerun <run-id>`). This bumps `run_attempt`, which makes the gate ignore the standing verdict and publish a new one under the canonical name. |
| **Only some jobs failed** (flaky runner, transient download)            | **Re-run failed jobs** (`gh run rerun <run-id> --failed`). The aggregation job republishes the canonical name on its own; the gate is not involved.                            |
| **CI is wanted on a branch that is not approved yet**, or on a pipeline with no required check | **Actions → the workflow → Run workflow** on that branch. A `workflow_dispatch` always runs. For a branch in this repository the result lands on the same head commit as the PR, so it can satisfy the required check; a fork branch can only be dispatched from the fork. |
| **The gate itself was cancelled** (`... interrupted`)                   | Nothing. The canonical name was left untouched, so the next push or approval re-runs the pipeline.                                                                             |
| **An approval was dismissed and re-approved**                           | Nothing. The dismissal invalidates the verdict on that head commit, and the new approval runs CI again.                                                                        |

> ⚠️ Do not publish a check result by hand. A manually created check run is not owned by the `github-actions` app, so the gate ignores it as a verdict while the ruleset may still count it as green.

#### Ref Resolution & Invalidation Rules

- **Target Ref:** PR jobs checkout `refs/pull/<number>/merge`, validating the projected merge commit formed by the PR head and target base branch.
- **Stale Review Semantics:** Governed directly by GitHub Ruleset settings; workflow triggers strictly consume the resulting approval state without attempting diff heuristics. This is also how a rebase is told apart from a code change: GitHub dismisses the approval for the latter and keeps it for the former, and the gate just counts the approvals that survived.
- **Verdict Scope:** A verdict belongs to one head commit. A new head commit starts without one, which is exactly how a rebase or "Update branch" gets its automatic run without asking for a new approval.

### 💾 Actions Cache Warmup

Actions cache entries are scoped by the ref of the run that wrote them: an entry written by a PR run is readable only by that PR, while an entry written on the default branch is readable by every branch and PR. Since upstream CI does not run on pushes to `main`, the default branch scope would otherwise stay empty and every PR would rebuild LLVM from scratch.

`ci_cache_warmup.yml` fills that scope on `main` (on relevant pushes, weekly, or manually) for the LLVM libraries, the `ocaml/setup-ocaml` opam root, and the `raven-actions/actionlint` binary.

| Situation                                            | What to do                                                                                                                                                                        |
| ---------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Working in a fork**                                | The push and schedule triggers are inert on forks. Run **Actions → `ci cache warmup` → Run workflow** on the fork's default branch to populate the fork's own scope; repeat if the entries age out. |
| **Need Windows / macOS / NuttX LLVM libraries**      | Those matrix entries are commented out to stay within the repository-wide 10 GB quota. Uncomment the entry in `ci_cache_warmup.yml`, warm it, then comment it back out.            |
| **Cache disappeared after a few quiet days**         | Entries are evicted after 7 days without access, and sooner when the 10 GB quota is exceeded. Re-run the warmup manually.                                                          |

> ℹ️ The `setup-ocaml` and `actionlint` keys are generated by third-party actions. The warmup jobs must keep the same runner, action pin and version inputs as their consumers in `compilation_on_ubuntu.yml` and `coding_guidelines.yml`, or the keys will not match.

### 🚀 Push Event Triggers & Branch Filtering

Push workflows apply differentiated execution paths based on repository origin and branch patterns:

| Context             | Branch Matching Rule           | Execution Outcome                       | Check Name & Status                         |
| ------------------- | ------------------------------ | --------------------------------------- | ------------------------------------------- |
| **Branch Filtered** | `main`, `release/**`, `dev/**` | Workflow skipped via `branches-ignore`. | No check produced.                          |
| **Fork Push**       | Non-filtered branches          | Executes full CI matrix.                | `ubuntu CI on push` (`success` / `failure`) |
| **Upstream Push**   | Non-filtered branches          | Gate job skips expensive execution.     | `ubuntu CI on push` (`success`)             |
