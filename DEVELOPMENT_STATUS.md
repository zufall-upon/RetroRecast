# Development status and open problems

RetroRecast is a **high-risk Experimental Alpha**, not a finished mod. The first and only demonstrated game is KF1. The project integrates the existing REGEN/CARLA2Flux generator and unmodified pretrained weights with a D3D9 hook, same-frame IPC and compositing; it does not claim a new model. Historical hardware evidence is limited to AMD Radeon systems described in the [README](README.md#evidence). Preserving game-world structure while maintaining real-time performance remains unresolved. See [known issues](KNOWN_ISSUES.md) and [validation](VALIDATION.md) before considering any experiment.

The priorities below indicate **importance, not implementation order**. Do not reproduce system reboots, use a daily-use PC, or assume a warning makes experiments safe. Start with CPU-only synthetic tests for a safer first PR; no game, GPU, model or private fixture is required. [Contributing](CONTRIBUTING.md) explains how to share results without private data. Work is tracked in the [GitHub Issues list](https://github.com/zufall-upon/RetroRecast/issues).

## P0 — Unexpected Windows system reboots

[Issue #1: Investigate unexpected Windows system reboots](https://github.com/zufall-upon/RetroRecast/issues/1)

- **Symptom and evidence:** Historical Windows tests had multiple abnormal system restarts with unknown cause, risking data loss and file corruption. One separate run recorded 2,086 same-frame pairs before a reboot; the record does not establish a cause or safe duration. See [validation](VALIDATION.md#reused-evidence) and [known issues](KNOWN_ISSUES.md).
- **Unknown / hypotheses:** Whether the GPU, driver, model, game, process interaction, hardware or another factor contributed is unknown. Do not treat any hypothesis as established, or a normal stop as proof of OS safety.
- **Public code:** [controller](runtime/controller.py), [job ownership gate](runtime/job_start.py), [capture supervision](runtime/capture_window.py), and [worker](runtime/worker/stage.py).
- **Safe next work:** Review existing public control flow and failure boundaries; improve diagnostic design or synthetic/mocked tests without inducing a reboot or publishing private logs.
- **Completion criteria:** A documented, evidence-backed explanation and mitigation, reviewed against independent evidence and appropriate safety assessment; report residual uncertainty. CPU tests alone cannot close the hardware-safety question. No dangerous reproduction is requested.

## P1 — Indoor structural consistency

[Issue #2: Improve structural consistency in indoor scenes](https://github.com/zufall-upon/RetroRecast/issues/2)

- **Symptom and evidence:** Dark/interior scenes can lose structure or navigation cues. The published [church input and same-request AI output](README.md#demonstration) show substantial brightness and structural changes before final game compositing; they are not a final-screen comparison. The reported basement is not identified with that scene.
- **Unknown / hypotheses:** The contribution of training domain, model behavior, preprocessing, postprocessing or other factors is undetermined. A new model or training is not an established requirement. Church evidence alone does not characterize all indoor scenes.
- **Public code:** [frame conversion and inference](runtime/worker/core.py), [generator](runtime/worker/generator.py), [comparison/service](runtime/worker/stage.py).
- **Safe next work:** Propose a reproducible evaluation protocol using synthetic or appropriately licensed shareable inputs; review existing public processing and separate model-output from compositing effects. Do not demand private game images or a GPU run as a first contribution.
- **Completion criteria:** A reviewable evaluation on appropriately permitted, matched inputs demonstrates improved wall/ceiling/passage consistency without overstating generalization, and documents remaining failures and performance tradeoffs. No such improvement is claimed today.

## P1 — Installation, stopping and recovery reproducibility

[Issue #3: Make Windows installation and recovery reproducible](https://github.com/zufall-upon/RetroRecast/issues/3)

- **Symptom and evidence:** Historical Windows use and the repackaged candidate are not the same validation: link rejection and recovery receipts were added, while Windows/GPU/game/ACL and abnormal-stop recovery for the repackaged candidate were not retested. A missing receipt prevents automatic identification; recovery is not atomic. See [validation](VALIDATION.md#important-differences-from-the-historical-configuration) and [known issues](KNOWN_ISSUES.md).
- **Unknown / hypotheses:** Actual behavior under Windows ACLs, interruption, concurrent changes and real-world installations is unverified; CPU success cannot resolve those questions or prevent an OS reboot.
- **Public code and existing tests:** [controller](runtime/controller.py), [dry run](scripts/dry_run.py), [recovery](scripts/recover.py), [install/recovery tests](tests/test_public_contract.py).
- **Safe next work:** Review the documented commands and extend isolated synthetic collision, dry-run, failure and receipt tests. Preserve existing files; no game or Windows execution is needed for initial work.
- **Completion criteria:** Reproducible, documented boundaries with meaningful tests and clear failure/partial-recovery reporting; any later Windows/ACL claims must have separate, explicitly scoped evidence. This work alone does not establish OS safety.

## P2 — Synthetic frame-contract regressions

[Issue #4: Improve synthetic frame-contract regression tests](https://github.com/zufall-upon/RetroRecast/issues/4)

- **Symptom / gap:** CPU checks cover important boundaries but are not exhaustive; stronger coverage can catch integration regressions without real-game data. This is a testing gap, not an identified new runtime failure.
- **Evidence:** [`ProceduralFrameContract`](tests/test_public_contract.py) already checks color conversion/padding, sequence error publication, a synthetic startup oracle, later live-frame sequence reset, missing references and comparison integrity. Its service test checks that a call retains the modes captured at start even if the arguments change later. [Validation](VALIDATION.md#checks-performed-for-the-release) states the scope.
- **Unknown:** Additional edge cases and which will reveal a defect remain to be established. CPU mocks do not validate GPU inference, visual quality or actual Windows behavior.
- **Public code:** [core](runtime/worker/core.py), [slot](runtime/worker/slot.py), [stage](runtime/worker/stage.py), [tests](tests/test_public_contract.py).
- **Safe next work:** Add a focused deterministic synthetic regression for a real contract boundary, asserting result, error and state; use the existing CPU suite. Do not require a game, weights, GPU or private fixture.
- **Completion criteria:** The regression fails for a demonstrated contract break and passes for the intended behavior; document its scope and run the relevant CPU suite. Do not claim this fixes the P0/P1 issues or proves visual quality.
