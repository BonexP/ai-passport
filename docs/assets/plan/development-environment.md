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

## 8. Working from a Windows host

Building and flashing on Windows is officially supported and straightforward. The friction is
entirely in the repository's shell gate, not in the firmware.

### 8.1 What works as documented

- ESP-IDF Tools Installer for v5.5.3 installs the toolchain; `idf.py build`, `idf.py flash` and
  `idf.py monitor` behave as on Linux. Run them from the installer's ESP-IDF Command Prompt.
- Git operations and the Python test suite are portable. The Python tests use `pathlib`; no
  POSIX-only paths.
- The host tests themselves are portable C: `tests/*.c` include only `assert.h`, `stddef.h`,
  `stdio.h`, `stdlib.h` and `string.h`. Nothing POSIX-specific.

### 8.2 What the gate assumes

`tools/validate.sh` assumes a POSIX shell plus:

- bash supporting `set -euo pipefail`
- `mktemp -d /tmp/...` and `rm -rf`
- a host C compiler reachable as `cc` or via `$CC`
- `install -m 0644` in the firmware mode
- `actionlint`, either on `PATH` or through `ACTIONLINT_BIN`

On Windows these translate into four concrete gaps:

- **No host compiler.** Neither Git Bash nor ESP-IDF ships an x86 Windows compiler; the
  RISC-V cross toolchain cannot produce Windows test binaries. Use MinGW-w64 (MSYS2
  `mingw-w64-x86_64-gcc` or winlibs) or LLVM clang, and set `CC` to it.
- **actionlint installer has no Windows branch.** `tools/install-actionlint.sh` handles only
  Linux and Darwin; on Git Bash `uname -s` reports `MINGW64_NT-*` or `MSYS_NT-*`, so it exits
  with `Unsupported actionlint platform`. Install actionlint separately (winget, scoop, or the
  official `windows_amd64` archive) and either put it on `PATH` or set `ACTIONLINT_BIN`.
  `validate.sh` checks both before invoking the installer, so this workaround is supported.
- **`install -m 0644` is not guaranteed in Git Bash.** It is GNU coreutils; the firmware mode's
  copy step may fail with `install: command not found`. Verify before relying on `--firmware`, or
  run the gate from MSYS2.
- **Symlinked skills need elevation.** `tools/install_passport_skills.py` creates directory
  symlinks, which on Windows require Developer Mode or an elevated shell. On failure the script
  prints the canonical `skills/<name>/SKILL.md` paths and exits nonzero. Reading those files
  directly satisfies the "five skills available" requirement; the link is only convenience.

### 8.3 The line-ending trap

The repository has no `.gitattributes`. Git for Windows defaults to `core.autocrlf=true`, which
writes CRLF into the working tree and breaks every script whose first line is
`#!/usr/bin/env bash`, with the classic `/usr/bin/env: 'bash\r': No such file or directory`.

Before the first checkout on a Windows host:

```bash
git config core.autocrlf input
```

Setting it per repository is enough; no global change is needed.

### 8.4 Recommendation

Edit on Windows, verify on CI, flash on Windows. This is the primary path and it removes the need
for WSL2 entirely.

- **Editing:** Windows, with Git and an editor. Nothing from the gate toolchain is required.
- **Build and the gate:** CI on `ubuntu-latest`, triggered by a pull request. The Linux host with
the local toolchain is a fast pre-check and a fallback, not a requirement.
- **Flashing and on-device acceptance:** native Windows, from the ESP-IDF Command Prompt.
- **Synchronisation:** the remote repository; that is what makes the split work.

Consequence: none of the four gaps in 8.2 need fixing. No MSYS2 or winlibs compiler, no manual
actionlint, no portable `install`. The only hard prerequisite is section 8.3: set
`core.autocrlf input` before the first checkout, and rely on the repository `.gitattributes` that
pins LF for scripts.

One caveat that must not be lost: CI compiles, it does not observe the device. A green pull
request proves Build and Host tests; the screen, buttons, audio, battery and Wi-Fi behaviour
remain device results and stay `Unverified` until someone with the board confirms them.

If a Windows-native gate is ever required anyway, the work is bounded: a `cc` presence check, an
`actionlint` path check, and a portable replacement for `install -m 0644`. Those stay listed as
conditional tasks in `task-breakdown.md`.
