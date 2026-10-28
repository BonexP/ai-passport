<p align="right">
  <a href="branch-model.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Branch and Sync Model

This document defines how this fork relates to `FoloToy/ai-passport`. It extends the upstream
[fork workflow](../../fork-guide.md) with one addition: a long-lived integration branch.

## 1. Remotes

| Remote | URL | Role |
| --- | --- | --- |
| `origin` | `https://github.com/BonexP/ai-passport` | Our fork. All pushes go here. |
| `upstream` | `https://github.com/FoloToy/ai-passport` | Read-only source of truth for the baseline. |

Fetch both before starting work:

```bash
git fetch origin
git fetch upstream
```

## 2. Branch layout

| Branch | Lifetime | Contents | Rules |
| --- | --- | --- | --- |
| `main` | permanent | Mirror of `upstream/main` plus fork-level files | Only the root `README.md` and `docs/assets/` may differ. Never carry feature code. |
| `feature/my-custom` | long-lived | Our integration branch: the all-in-one firmware | Receives upstream merges and finished topic branches. Always buildable. |
| `feature/<topic>` | short-lived | One fix or one feature | Created from `feature/my-custom`, merged back into it, then deleted. |

`feature/<topic>` branches are where actual code changes happen. Examples:
`feature/wifi-profiles`, `feature/stock-page`, `feature/weather-page`, `fix/wifi-reconnect-loop`.

## 3. Creating the integration branch

Create it from upstream, not from the fork's `main`, so a stale fork cannot pollute the
starting point:

```bash
git fetch upstream
git switch -c feature/my-custom upstream/main
git push -u origin feature/my-custom
```

## 4. Working on a topic

```bash
git fetch origin
git switch feature/my-custom
git pull --ff-only origin feature/my-custom
git switch -c feature/wifi-profiles
# ... work, commit ...
./tools/validate.sh --static
git push -u origin feature/wifi-profiles
```

Merge back into the integration branch with a merge commit (not a fast-forward squash) so the
topic boundary stays visible in history:

```bash
git switch feature/my-custom
git merge --no-ff feature/wifi-profiles
git push origin feature/my-custom
```

## 5. Absorbing upstream changes

`main` is kept current automatically by the fork's `Upstream Sync` workflow
(`.github/workflows/sync-main.yml`, guarded by `if: github.event.repository.fork`, daily at
00:00 UTC). GitHub Actions must be enabled manually in the fork for this to run.

Whether or not that workflow is active, the local procedure is the same:

```bash
git fetch upstream
git switch main
git merge --ff-only upstream/main       # should never conflict
git push origin main

git switch feature/my-custom
git merge main                            # may conflict; resolve here
git push origin feature/my-custom
```

Guidance:

- Prefer merging `main` into `feature/my-custom` over rebasing. Rebasing a pushed branch
  rewrites shared history for no benefit here.
- Resolve conflicts in favour of upstream for infrastructure files (`tools/`, `.github/`,
  `components/bsp/`, `docs/` outside `docs/assets/`). Re-resolve our application changes on
  top rather than editing upstream's tooling.
- After every upstream merge, re-run `./tools/validate.sh --static` before continuing.
- If upstream changes `components/bsp` APIs we already use, treat it as a normal refactor task
  on a topic branch, not an inline fix.

## 6. What stays out of the fork

- Credentials, API keys, device secrets, private keys. Provision into NVS at runtime.
- Generated build output (`build/`, `managed_components/`, `sdkconfig`).
- Fork-specific product documentation outside `docs/assets/`.
- Any modification to upstream files on `main` beyond the root `README.md`.

## 7. Contributing back

Improvements that benefit any AI Passport user, such as corrected hardware facts, reusable BSP
interfaces or build-flow fixes, are proposed upstream as pull requests from a topic branch cut
from `upstream/main`. Application archives go to `docs/reference/<username>/<app-name>/`.
Use the `experience-pr` and `plays-archive` skills for the post-release flow.

Fork-specific product decisions stay in this fork and never go upstream.

## 8. Triggering CI

CI is the primary verification path for a Windows-based workflow. Its triggers are specific, and
pushing alone is not enough.

| Workflow | Triggers | Runs |
| --- | --- | --- |
| `static-checks.yml` | pull request, push to `main`, manual dispatch | repository checks, actionlint, all host tests |
| `firmware-checks.yml` | pull request, push to `main`, manual dispatch | full ESP-IDF build and merged-image verification |
| `build-firmware.yml` | push of a tag, manual dispatch | build plus GitHub Release |

Consequences that matter in practice:

- A push to `feature/my-custom` or any other non-`main` branch triggers **nothing**. Only a pull
  request does.
- `firmware-checks.yml` ignores changes limited to `docs/**`, `skills/**` and `*.md`. A
  documentation-only pull request still runs `static-checks.yml`, but no firmware build.
- `workflow_dispatch` is available on all three workflows, so a manual run is possible from the
  Actions tab without opening a pull request. That is the escape hatch when a branch push needs
  verification but no review is wanted yet.

Recommended flow:

1. Commit on `feature/my-custom` (or a `feature/<topic>` branch) and push.
2. Open a pull request. For quick self-checks the base can be `feature/my-custom` itself; for
   real review, use a topic branch against `feature/my-custom`.
3. Wait for both `Static checks` and `Firmware checks`. Both must be green.
4. Merge with a merge commit, keeping the topic boundary visible.

CI compiles and runs host tests. It cannot observe the device. Screen, buttons, audio, battery and
Wi-Fi behaviour stay `Unverified` in the delivery report until someone with the board confirms
them.
