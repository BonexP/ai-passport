<p align="right">
  <a href="readme.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# docs/assets: Fork-Specific Documentation

This directory holds documentation that belongs to this fork and not to upstream. Upstream
`main` keeps it empty except for `.gitkeep`; everything here exists only in this fork and is
never proposed upstream.

## Layout

```
docs/assets/
  readme.md, readme.zh_CN.md      This file: purpose and conventions.
  plan/
    project-plan.md               Scope, hardware envelope, feature assessment, phases.
    branch-model.md               Remotes, branch layout, upstream sync procedure.
```

## Conventions

- Every document is bilingual: an English `.md` default plus a `.zh_CN.md` peer, each linking
  to the other at the top. `tools/check_repo.py` enforces this pairing.
- The English default must contain no Chinese prose.
- Facts about the board follow the upstream source-of-truth order: measured product data,
  then `components/bsp/include/bsp_pins.h`, then BSP headers, then the hardware guide, then
  README and demos.
- Anything that would also help an unrelated AI Passport user does not belong here; propose it
  upstream instead.

## Boundary

Files in this directory may be changed on `main`, alongside the root `README.md`. That is the
only exception to the rule that `main` mirrors upstream. Application code and anything that
affects the build stays on `feature/*` branches.
