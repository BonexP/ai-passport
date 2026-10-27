<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# BonexP's AI Passport fork

Personal fork of [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) hosting a custom
all-in-one firmware for the AI Passport device (ESP32-C3, 8 MB flash, no PSRAM).

Upstream documentation is unchanged and remains authoritative: start at
[`docs/README.md`](docs/README.md) for the product overview, [`AGENTS.md`](AGENTS.md) for the
development rules, and [`docs/fork-guide.md`](docs/fork-guide.md) for the fork workflow.

## What this fork adds

| Path | Contents |
| --- | --- |
| [`docs/assets/plan/project-plan.md`](docs/assets/plan/project-plan.md) | Scope, hardware envelope, feature assessment, phased plan |
| [`docs/assets/plan/branch-model.md`](docs/assets/plan/branch-model.md) | Remotes, branch layout, upstream sync procedure |

## Status

Planning. No application code yet. Baseline is upstream `main`, unmodified.

## Branch model, in one paragraph

`main` mirrors `upstream/main` and carries only this file and `docs/assets/`. All firmware work
happens on `feature/my-custom`, a long-lived integration branch created from `upstream/main`.
Individual features and fixes branch off `feature/my-custom` and merge back into it. Upstream
changes are fetched, fast-forwarded into `main`, then merged into `feature/my-custom`.

## Getting started

```bash
git clone --recurse-submodules https://github.com/BonexP/ai-passport.git
cd ai-passport
python3 tools/install_passport_skills.py --check
```

Requires ESP-IDF v5.5.3; see
[`docs/development/engineering/environment-setup.md`](docs/development/engineering/environment-setup.md).
Run `./tools/validate.sh --static` before every commit and the full `./tools/validate.sh` before
delivery.

## Licence

Inherited from upstream; see [`LICENSE`](LICENSE).
