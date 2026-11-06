# Serious known issues and unverified areas

- Multiple unexpected Windows system reboots occurred during testing, cause unknown. Data loss or file corruption is possible. Warnings and stopping procedures do not guarantee safety.
- Dark/interior scenes can lose structure or direction. The saved church input and AI output show large brightness and structural changes, but it is unknown whether this is the same scene as the owner's reported basement.
- Inference and frame waits degrade performance. No improvement or target frame rate is claimed.
- GPUs other than those in the experiments are unverified; driver compatibility and long-term stability are not guaranteed.
- This candidate's synthetic fixture and recovery receipt differ from those used in historical Windows play. CPU mocks and cross-builds do not verify the new candidate on Windows/GPU/KF1 or its stop/recovery path.
- Model, game, Python/GPU environment and FFmpeg are not included. Obtain them through official sources with your own rights. The code license does not extend to weights or images; see [NOTICE](NOTICE.md) for the chain and component terms.
- Without a receipt after a crash, owned DLLs cannot be automatically identified and removed. Even with a receipt, concurrent changes, Windows ACLs and sudden termination can prevent complete restoration. Logs, IPC and markers remain.
- Recordings may be unplayable after abnormal termination. No new recording or game/GPU tests were performed.

See [Development Status](DEVELOPMENT_STATUS.md) for open work, evidence, unknowns and safe ways to contribute.
