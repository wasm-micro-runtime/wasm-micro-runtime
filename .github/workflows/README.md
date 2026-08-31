# 📌 CI and Ruleset Gates Architecture

This document specifies the GitHub Actions workflow triggers, approval gates, and branch ruleset configurations for the WebAssembly Micro Runtime (WAMR) repository.

### 🛡️ Repository Ruleset Policies

| Policy                       | Configuration & Enforcement                                                                                            |
| ---------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| **Pull Request Reviews**     | Mandatory before merging.                                                                                              |
| **Commit-Level Approval**    | Requires an explicit approval targeting the **latest head commit** of the PR.                                          |
| **Approval Invalidation**    | Automatically dismisses stale reviews upon new commit pushes (`synchronize`).                                          |
| **Status Check Enforcement** | Requires canonical status checks to pass before merging. Current mandatory gates: `ubuntu CI` and `coding guidelines`. |
| **Merge Queue**              | **Disabled** (standard PR merge flow).                                                                                 |

> ℹ️ **Note on Check Evaluation:** GitHub evaluates required checks by their **exact check-run name**. The aggregation job always runs after the gate has a decision so dynamic check names are evaluated before GitHub publishes the check run; `state=skipped` falls through to the canonical required check name while the expensive jobs underneath stay skipped.

### ⚙️ CI Approval & Execution Logic

Real upstream CI execution is strictly **approval-gated** to optimize runner resources and enhance security:

| Operational State                                  | Upstream CI Behavior                                                        | Check Name / Status                                           |
| -------------------------------------------------- | --------------------------------------------------------------------------- | ------------------------------------------------------------- |
| **PR Lifecycle Events** (`opened` / `reopened`)    | Real CI is **not** triggered.                                               | None (awaits review).                                         |
| **Initial Review Approval**                        | Triggers full CI on the approved `head SHA` if relevant files are modified. | `ubuntu CI` (`success` / `failure`)                           |
| **PR Sync with Valid Approval**                    | Triggers full CI only if approval remains active on the new commit.         | `ubuntu CI` (`success` / `failure`)                           |
| **PR Sync without Approval / Non-Approval Review** | No expensive compute triggered.                                             | Non-required check `ubuntu CI awaiting approval` (`success`). |
| **Approved PR without Relevant Changes**           | Path filtering bypasses test jobs.                                          | `ubuntu CI` (`success`).                                      |
| **Duplicate Approval on Same SHA**                 | Bypasses redundant CI execution.                                            | Non-required check `ubuntu CI approved` (`success`).          |
| **Duplicate Sync Run on Same SHA**                 | Bypasses redundant CI execution.                                            | `ubuntu CI` (`success`).                                      |

#### Ref Resolution & Invalidation Rules

- **Target Ref:** PR jobs checkout `refs/pull/<number>/merge`, validating the projected merge commit formed by the PR head and target base branch.
- **Stale Review Semantics:** Governed directly by GitHub Ruleset settings; workflow triggers strictly consume the resulting approval state without attempting diff heuristics.

### 🚀 Push Event Triggers & Branch Filtering

Push workflows apply differentiated execution paths based on repository origin and branch patterns:

| Context             | Branch Matching Rule           | Execution Outcome                       | Check Name & Status                         |
| ------------------- | ------------------------------ | --------------------------------------- | ------------------------------------------- |
| **Branch Filtered** | `main`, `release/**`, `dev/**` | Workflow skipped via `branches-ignore`. | No check produced.                          |
| **Fork Push**       | Non-filtered branches          | Executes full CI matrix.                | `ubuntu CI on push` (`success` / `failure`) |
| **Upstream Push**   | Non-filtered branches          | Gate job skips expensive execution.     | `ubuntu CI on push` (`success`)             |
