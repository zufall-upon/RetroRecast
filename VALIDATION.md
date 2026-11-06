# Validation scope and mapping to historical artifacts

## Reused evidence

Of 56 payloads in the historical Windows runtime, 54 matched the original stage and two (controller/capture) were later revisions; this distribution uses the latter. The revisions add a gate establishing Job ownership before a venv redirector spawns a child and bounded cleanup after encoder failure. Limited records show Windows same-frame AI rendering and owner-operated play; a separate test observed an OS reboot after 2,086 same-frame pairs. Distinguish successes from failures: neither establishes clean termination, OS safety or long-term stability.

## Checks performed for the release

- Pinned the public C++ include closure and build configuration. Cross-built hook/helper/smoke with LLVM-Mingw clang 23.1.4 and matched the historical binaries byte-for-byte; did not run them on Windows.
- CPU synthetic tests without model or game: BGRA/RGB, vertical padding, raw quantization, slot sequence/error, normal paths for two subsequent frames using input different from the startup oracle, controller ownership ordering, conflict refusal, recovery dry runs and changed-file refusal.
- At release creation, checked all payloads/hashes, excluded assets, source commit correspondence and CPU tests after extraction. None proves game success or safety.

| Artifact | SHA-256 |
|---|---|
| hook | `4096f1f380cb346f2418105b5b4b1617e7b71a5be49e1e38520bfe1da6b1486f` |
| helper | `43ff50ab95eb10c2b7fcbcde37a33c4d00adfa327307ca8befda8824376c2cba` |
| smoke | `3752972d5390f229dcce22fc728eb76575ffb49c876664781f94fca822ff2a37` |
| unmodified historical chain | `70571734e0758c9791f46d5b0d26a0cc0dd110e07093b5e82f6069da3fd4a2bc` |

## Important differences from the historical configuration

The original startup oracle required fixed hashes of three private real-game images. They are not distributed; the public path uses explicit `--reference-mode synthetic --comparison-mode measure` self-consistency instead. Unlike the old image-quality comparison, its report has `matches=null` and `visual_quality_assessed=false`. The model, preprocessing/postprocessing and slot contract for actual game frames are unchanged; after the oracle, sequence resets to 1 for arbitrary subsequent frames. CPU tests check that path but do not run GPU inference.

Installation adds rejection of linked paths and a recovery receipt recording hash/inode/mode for exclusively created DLLs. Existing files are not overwritten. This does not solve Windows ACLs, sudden power loss, OS restarts or concurrent changes; a dedicated copy and backups remain essential.

**The repackaged configuration as a whole has not been retested on Windows/GPU/game or for installation/recovery.** Historical hardware results are not a pass for the entire new candidate. No new game/GPU/Windows testing or recording retry was performed; unresolved risks remain in this Experimental Alpha. Do not extrapolate performance numbers to game FPS or untested GPUs.
