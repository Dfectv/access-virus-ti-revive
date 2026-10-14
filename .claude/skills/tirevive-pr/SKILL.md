---
name: tirevive-pr
description: Prepare a TI Revive pull request the way the maintainers expect - rule check (no Access material), build, hardware test notes, evidence entry, README update and PR description. Use before committing or opening a pull request in this repository.
---

# Prepare a TI Revive pull request

Work through the steps in order and report the result of each one to the human.

## 1. Rule check (stop if any fails)

- `git diff --stat` and `git status`: only files that belong to this change.
- No Access material: no binaries, firmware, PNG/XML resources, patch banks, decompiled Access code
  (`git diff | grep -iE "^\+.*(decompil|0x1800[0-9a-f]{5}.*\{)"` is a quick hint, not proof — read the diff).
- No personal data: user names, home paths from logs, serial numbers.
- No unrelated reformatting or refactoring.

## 2. Build what changed

- `src/broker/`, `src/wrapper/`: `make -C src/broker`, `make -C src/wrapper` (mingw-w64 on Linux).
- `src/mac/`: on a Mac, `scripts/mac-build-bridge.sh <repo> --install`.
- Installer scripts: run their syntax check (`bash -n`, PowerShell parser) at least.
Record which builds you ran and their result. If a platform could not be built, say so.

## 3. Test

- With hardware: describe the setup (platform, Virus model, DAW, TI Revive version) and the steps and results.
- Without hardware: write "not tested on hardware" and explain what a tester should check.
- Never send untested vendor requests or SysEx to a real Virus; no firmware/flash operations.

## 4. Document

- New knowledge about the device or the Access software: add a row to `findings/evidence.md` with the next free
  number: `| E### | YYYY-MM-DD | observation with numbers and conditions | source | file/function | HIGH/MEDIUM/LOW/open |`.
  Correct earlier entries with a new entry instead of editing history away.
- Changed user-visible behaviour: update `README.md` (installation, "Good to know", status table).

## 5. Pull request description

```
## Problem
<symptom, platform, issue link>

## Cause
<what the evidence shows; E-number if any>

## Change
<what the code does now>

## Tested
<builds run; hardware test with setup, or "not tested on hardware">

Written with the help of an AI assistant (<name>).
```

Keep the PR to one problem. Small, well-explained changes get merged fastest.
