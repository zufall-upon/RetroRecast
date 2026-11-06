# Contributing

Start with [Development Status](DEVELOPMENT_STATUS.md) and its [tracked issues](https://github.com/zufall-upon/RetroRecast/issues). It separates observed evidence from unknowns and proposes safe next steps and completion criteria. Priority reflects importance, **not** suggested order of first contributions. The synthetic CPU frame-contract issue is a safer first PR than a Windows or image-quality experiment.

Read the [BUILDING](BUILDING.md) CPU synthetic contract checks and [LICENSE_SCOPE](LICENSE_SCOPE.md). A PR should explain its purpose, changes and relevant test results. Work that can start without game or model includes:

1. **Synthetic frame contract:** small regressions for channel order, padding, sequence or failure reporting; start with [`tests/test_public_contract.py`](tests/test_public_contract.py).
2. **Installation/recovery boundaries:** improve conflict refusal, dry-run diagnostics or tests that preserve existing files. This does not make OS reboots safe.
3. **Build reproducibility:** rebuild the x86 hook/x64 helper from public source with existing tools and report compiler, flags and hash differences; no game or model needed.

Do not make private fixtures mandatory for an easy participation task. Dark-scene structural fidelity is unresolved research, not a good-first-issue promise or a completed improvement. Additional game/GPU experiments are not a prerequisite for contributing: begin with static review, CPU tests or builds. Do not ask contributors to reproduce an OS reboot.

Do not post secrets, game data or local absolute paths in reports. Windows restart safety remains unresolved; see [known issues](KNOWN_ISSUES.md) and [validation scope](VALIDATION.md).
