# RetroRecast

### Experimental neural rendering for classic PC games — demonstrated on AMD Radeon.

**English** · [日本語](README.ja.md) · [简体中文](README.zh-CN.md)

[![Pre-release: v0.1.0-alpha](https://img.shields.io/badge/pre--release-v0.1.0--alpha-orange)](#download)
[![AMD Radeon: limited proof of concept](https://img.shields.io/badge/AMD_Radeon-limited_PoC-ED1C24)](#evidence)
[![First target: Killing Floor 2009](https://img.shields.io/badge/first_target-Killing_Floor_2009-blue)](#demonstration)
[![Code: GPL-3.0-or-later and component licenses](https://img.shields.io/badge/code-GPL--3.0--or--later-617C42)](LICENSE_SCOPE.md)

[**GAMEPLAY**](#gameplay) · [**DOWNLOAD ALPHA**](#download) · [**SOURCE / BUILD**](BUILDING.md) · [**KNOWN ISSUES**](KNOWN_ISSUES.md)

RetroRecast explores bringing new visuals to old games without replacing their gameplay. **Killing Floor (2009), or KF1, is the first and only demonstrated target.** The contribution is real-game integration on AMD hardware: a Direct3D 9 hook, same-frame inter-process communication (IPC), neural-output compositing and a supervised experimental runtime—not a new AI model or a finished graphics mod.

<a name="demonstration"></a>
## Demonstration

![KF1 church: original world input on the left; quantized CARLA2Flux output for the same request on the right](assets/church-input-output.png)

**Actual saved input / output from a Windows experiment.** Left: KF1 world input. Right: quantized AI output for the **same request**. These are **not two final composited game screens**. Brightness and surfaces change substantially; structure and material fidelity are not guaranteed. This still does not demonstrate a fix for dark scenes or temporal stability. Killing Floor © Tripwire Interactive and other rights holders; [image attribution and scope](NOTICE.md#紹介画像).

<a name="gameplay"></a>
## Gameplay — 60-second preview

<!-- gameplay-video -->

https://github.com/user-attachments/assets/795368ae-fec0-4f0c-b331-bb530b4d1fe4

[**▶ Watch / download the gameplay PV (MP4, 60 seconds, silent)**](https://github.com/zufall-upon/RetroRecast/releases/download/v0.1.0-alpha/RetroRecast-KF1-gameplay-preview.mp4)
<!-- /gameplay-video -->

Edited **real KF1 gameplay recordings**, including experimental AI-rendered visuals—not fictional gameplay generated for this page. Visual artifacts and performance issues remain. The two source recordings have different viewpoints/equipment and unidentified processing conditions: this is **not a controlled before/after comparison, a continuous three-minute session, or a benchmark of the released model**. Video frame rate/resolution are not AI inference performance. The historical “private preview / in development” title remains in the preserved PV; it is now shared with the owner's authorization. [Video provenance, credits and rights](NOTICE.md#gameplay-video).

<a name="download"></a>
## Download & try

> [!WARNING]
> **Windows has unexpectedly rebooted during earlier experiments; the cause is unresolved. Data loss or file corruption is possible.** Dark/interior scene structure collapse and performance degradation remain. Do not run on a daily-use PC or a system containing important data. Other GPUs are unverified. **This repackaged configuration has not been re-tested on Windows/GPU/KF1, including ACLs and recovery after abnormal termination.** Supervision and recovery instructions do not guarantee safety. **This is a high-risk research Pre-release, not a stable release or finished mod.**

| Get started | What you get |
|---|---|
| [**v0.1.0-alpha Pre-release**](https://github.com/zufall-upon/RetroRecast/releases/tag/v0.1.0-alpha) | [**Windows runtime ZIP**](https://github.com/zufall-upon/RetroRecast/releases/download/v0.1.0-alpha/RetroRecast-v0.1.0-alpha-windows.zip), [SHA256SUMS](https://github.com/zufall-upon/RetroRecast/releases/download/v0.1.0-alpha/SHA256SUMS), package manifest and corresponding source |
| [**Requirements · install · stop · disable · restore**](QUICKSTART.md) | English instructions; [Japanese](QUICKSTART.ja.md) is also available. Read the risks first; offline single-player, dedicated game copy, finite manual WestLondon testing at 960×540 only. |
| [**Source at the runtime tag**](https://github.com/zufall-upon/RetroRecast/tree/v0.1.0-alpha) · [build / CPU tests](BUILDING.md) | The automatic “Source code” archives are source-only, **not the executable ZIP**. Build and CPU-test entrypoints are provided. |

The game, model weights, Python/PyTorch/NumPy environment, FFmpeg and drivers are **not included**. Obtain them legitimately under their own terms. The recorded Windows environment used **RX 6950 XT / gfx1030 and TheRock/ROCm PyTorch**, not a general AMD compatibility promise. No automatic CPU, DirectML or other fallback is provided.

## Help improve RetroRecast

[Development Status & open problems](DEVELOPMENT_STATUS.md) · [Issues](https://github.com/zufall-upon/RetroRecast/issues) · [Contributing](CONTRIBUTING.md). Priorities describe importance, not first-PR order: CPU frame-contract tests are the safer starting point. Do not reproduce OS reboots or require private assets to participate.

<a name="evidence"></a>
## What is ours, and what is demonstrated?

| Layer | Scope |
|---|---|
| **Reused: [REGEN / CARLA2Flux](https://github.com/stefanos50/REGEN)** | Stefanos Pasios's generator and unmodified pretrained FP32 weights. Credit remains upstream; no new model or training is claimed. |
| **RetroRecast integration** | KF1 D3D9 frame extraction, same-frame IPC between game and worker, output compositing, Windows process supervision, public source/build and recovery entrypoints. This is the project's contribution, not a claim of superiority over REGEN. |
| **Limited historical demonstration** | Linux/Wine + Radeon RX 7900 XTX rendering/inference integration; Windows + RX 6950 XT GPU inference, same-frame KF1 display and owner-operated play records. Windows records also include abnormal OS reboots and incomplete clean shutdown. Not certification for all Radeon GPUs. |
| **Offline release checks** | Cross-built hook/helper/smoke matched existing binaries byte-for-byte; CPU compositing/IPC and install/recovery boundary checks. Public packaging replaced the private-image startup oracle with explicit `synthetic + measure` self-consistency checks and added link rejection/recovery receipts. These are not hardware, image-quality or safety passes. |
| **Unresolved / unverified** | Dark-scene structure, visual quality, temporal stability, performance targets and Windows reboot cause; other GPUs/games; the repackaged Windows/GPU/game/ACL/recovery path. No new game/GPU/Windows tests or recording retries were performed for this page. |

[Validation scope](VALIDATION.md) · [known issues](KNOWN_ISSUES.md) · [release/source mapping](release-manifest.json) · [contribute](CONTRIBUTING.md). Quick Start and Build are available in English and Japanese; some other technical and licensing documents remain in Japanese.

## Credits & rights

Killing Floor © Tripwire Interactive and other rights holders. Code: **GPL-3.0-or-later for the integration, with original component licenses retained**; see [NOTICE](NOTICE.md) and [LICENSE_SCOPE](LICENSE_SCOPE.md). Code, game footage/images, model weights, teacher/data and third-party dependencies have different rights: the code license does not relicense them. No game assets or weights are redistributed in the runtime. Not endorsed or supported by Tripwire, Valve, AMD, NVIDIA or the REGEN authors.
