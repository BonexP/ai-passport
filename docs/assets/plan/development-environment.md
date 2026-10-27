<p align="right">
  <a href="development-environment.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Development Environment

Status: prepared and verified on 2026-10-27. This file records what is installed, how to
activate it, and the boundaries that keep it reproducible. It does not replace upstream's
environment guide; it records this machine's concrete result.

## 1. What is installed

| Component | Version | Location | Note |
| --- | --- | --- | --- |
| ESP-IDF | v5.5.3 | `~/esp/esp-idf-v5.5.3` | Gitee mirror clone; tag commit `b31fcc7a314a44ad992b58f589f7d1d8a4fadff6` matches upstream |
| Toolchain | riscv32-esp-elf 14.2.0, openocd-esp32, esp-rom-elfs | `~/.espressif/tools` | 2.2 GB |
| IDF Python env | `idf5.5_py3.12_env` (Python 3.12.3) | `~/.espressif/python_env` | Created by `install.sh`; includes `idf-component-manager` |
| ccache | 4.9.1 | system | Host package |
| gperf | 3.1 | system | Host package, required by the IDF build |
| uv | 0.11.7 | `~/.local/bin/uv` | Repository-side tooling only, see section 4 |

Verified with `. ~/esp/esp-idf-v5.5.3/export.sh && idf.py --version`, which reports
`ESP-IDF v5.5.3-dirty`.

## 2. Why the version string says "dirty"

`idf.py --version` reports `v5.5.3-dirty`. The source tree itself is unmodified
(`git status --porcelain` shows no top-level changes). The suffix comes from 22 submodule
pointers that differ from the pinned values, because the Gitee mirror serves each submodule
from its own mirror repository at a different head commit.

Consequences:

- Builds are unaffected; the difference is submodule bookkeeping, not source content.
- `git describe` appends `-dirty`, so tooling that parses the version must accept the suffix
  instead of matching `v5.5.3` exactly.
- Do not "fix" it by resetting submodules to the pinned commits unless a specific failure
  requires it; the mirror heads are what the mirror intends to deliver.
- If a clean version string is ever required, re-clone from GitHub when connectivity allows and
  re-run `install.sh`. Treat that as a separate, justified task.

## 3. Activation

The environment is not injected into the shell profile. Activate per session:

```bash
. ~/esp/esp-idf-v5.5.3/export.sh
idf.py --version    # expect: ESP-IDF v5.5.3-dirty
```

No `export.sh` call was added to `.bashrc` or `.profile` on purpose: it changes `PATH` for
every shell, and upstream's guide forbids editing shell startup files without explicit
authorisation.

## 4. uv: scope and boundaries

`uv` is available and useful, but it does **not** replace the ESP-IDF Python environment.

- ESP-IDF's `install.sh` calls `python -m venv` to create
  `~/.espressif/python_env/idf5.5_py3.12_env`, and `export.sh` puts that environment first on
  `PATH`. `idf_tools.py` manages it directly. Redirecting it to uv would mean patching IDF's
  installer and re-applying the patch on every upstream update.
- Therefore: **IDF keeps its official venv; uv serves repository-side Python work** — host-test
  helper scripts, formatting and static checks, and any future build automation that lives in
  this fork rather than in ESP-IDF.
- When a repository-side Python dependency appears, pin it in a root `pyproject.toml` and run it
  with `uv run`, so no ambient environment is required.
- Never let `uv run` shadow the IDF environment: run IDF commands where `export.sh` was
  sourced, and repository tooling separately.

This split is a decision, not a temporary state. Changing it costs a patch to `idf_tools.py`,
and that cost must be argued explicitly.

## 5. Validation entry points

Upstream's gate script has three modes with different meanings:

```bash
./tools/validate.sh --static     # repo checks, actionlint, all host tests
./tools/validate.sh --firmware   # ESP-IDF build plus merged-image verification
./tools/validate.sh              # both
```

`--firmware` requires an activated environment. It builds into a temporary directory and copies
`build/FoloToy-AI-Passport-full.bin` on success, so a long-lived build directory does not pollute
the working tree.

Delivery reports keep four separate lines, per upstream policy: Build, Host tests, Device tests,
Unverified. A passing build is not a device result.

## 6. Device access

Flashing and monitoring are not implied by a working build. The board connects over native
USB Serial/JTAG (GPIO18/19); UART0 is unusable because its default TX pin collides with the
display backlight. Serial access, port permissions (`dialout`) and the physical connection are
separate checks, and the device must be powered on and visible before any flash operation.
No flash or monitor operation has been performed so far.

## 7. Known limitations

- Network path to GitHub is unstable from this host: direct clones stall or drop TLS, which is
  why the Gitee mirror was used. Tool archives came from `dl.espressif.cn`.
- The checkout is a full clone rather than shallow, so IDF Git history is present; IDF plus
  tools use about 3.2 GB.
- Free disk space was about 28 GB at setup time. Build artifacts and `managed_components` consume
  the remainder over time; monitor before adding large assets or fonts.
