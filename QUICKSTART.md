# 導入・停止・無効化・復元（研究用の未検証手順）

> **重大な警告**: 過去のWindows試験中に原因未確定のOS全体の異常再起動が発生しました。データ消失・ファイル破損の可能性があり、警告は安全保証ではありません。暗所の構造崩壊と性能低下が残り、検証したGPU以外は未検証です。常用PCでは実行しないでください。

**開発者向けExperimental Alphaです。** Releaseの `RetroRecast-v0.1.0-alpha-windows.zip` を展開してください。GitHubの自動Source code ZIPには実行DLLがないため、実行ZIPとは異なります。再梱包後のWindows実行は未検証です。以下は重大なリスクを承知した人向けの手順であり、通常使用の推奨ではありません。

ZIPを入手したら、ReleaseのSHA256SUMSと `Get-FileHash <zip> -Algorithm SHA256` を照合。展開rootで `python -B scripts/verify_package.py` を実行します。これは全payloadのhash/sizeと混入を点検するだけで、安全性や実行成功を保証しません。

## 前提と非同梱物

- 自分で合法に入手したKF1、隔離した完全なゲームコピー、十分なバックアップと独立した実験用Windows環境。元ゲーム、OS DLL、常用環境は変更しません。オフラインの単独実験を想定します。
- Windows x64 Python、NumPy、対応するGPU版PyTorch、対応GPU/driver。既存実験はPython 3.12.16、NumPy 2.5.3、torch 2.14.0+rocm10.1.0、RX 6950 XTであり、他環境への互換保証ではありません。GPUが使えなければ失敗し、CPU等へ自動fallbackしません。
- 未改変CARLA2Flux FP32重みは同梱しません。[REGEN作者の公開モデル案内](https://github.com/stefanos50/REGEN/tree/de240056522d066235b48b541e7d49f28c80f1ed) と [作者ファイル](https://drive.google.com/file/d/1deJ7g4Lo52ZdeXIiEz10lZ2Gp2RC4mgR/view)を参照し、利用条件を自分で確認してください。**729795098 bytes、SHA-256 `0fbb08561b71ad759c4503a77233497f06c7b168ba8eaf023a35b214d71af254`** のみ受け付けます。リンクやハッシュは重み・教師・データの利用許諾の証明ではありません。
- Release ZIPはhook・x64 atomic helper・smoke・実使用と同一bytesの32-bit DXVK chainを含みます。別DXVKやWindows system DLLへ自動代替しません。[NOTICE](NOTICE.md)の部品別条件と[ビルド手順](BUILDING.md)を参照。Windowsのsystem DLLをコピー・置換する手順ではありません。
- 録画監督に利用者自身の [FFmpeg](https://ffmpeg.org/download.html#build-windows) が必要です。ビルドごとのGPL/LGPL等の条件を確認して取得してください。本projectはFFmpeg binaryを再配布しません。依存は自動ダウンロード・インストールしません。

### 独立したPython環境とGPUの指定

既存の成功環境やシステムPythonを置換しないでください。[Python公式](https://www.python.org/downloads/windows/)の64-bit Python 3.12系から新venvを作り、[AMD TheRock公式手順](https://github.com/ROCm/TheRock/blob/main/RELEASES.md)と[GPU表](https://github.com/ROCm/TheRock/blob/main/SUPPORTED_GPUS.md)を確認します。以下は**保存実績のversion指定例で、この手順を今回Windowsで再実行したものではありません**。gfx1030は開発用TheRockの経路であり、製品GPUの一般対応保証とは別です。

```powershell
py -3.12 -m venv C:\RetroRecast-env
C:\RetroRecast-env\Scripts\python.exe -m pip install --index-url https://stable.repo.amd.com/rocm/whl-next/ "torch[device-gfx1030]==2.14.0+rocm10.1.0"
C:\RetroRecast-env\Scripts\python.exe -m pip install "numpy==2.5.3"
```

この後の `python` はこのvenvの `C:\RetroRecast-env\Scripts\python.exe` に読み替えます。元のWindows実験はRX 6950 XT/gfx1030を明示選択しました。内蔵GPU等を持つPCではindexが異なります。利用者自身が公式runtimeの列挙結果とdriverを確認し、下記のplaceholderを実際のdGPU indexに置き換えて、**同じPowerShell内で比較とcontrollerを実行**してください。`1` を全PCへ決め打ちしません。選択できない場合は止め、CPU・DirectML・ZLUDA・偽装へ切り替えません。

```powershell
$env:HIP_VISIBLE_DEVICES = "<確認したdGPU index>"
```

この指定は現在のshellと子process向けです。終わったら専用shellを閉じます。環境全体の設定変更やdriver更新を本手順から行わないでください。torch側ではこの指定後の一意なGPUを `cuda` API経由で利用します（ROCmでもこのAPI名です）。

## 衝突の事前確認

独立したコピーの `System` と、まだ存在しないゲーム外の実行directoryを指定します。`<...>`は各自のpathへ置換してください。dry-runはコピー・起動・GPU検査を行いません。

```text
python -B scripts/dry_run.py --system <KF1-copy/System> --run-dir <new-run-directory>
```

既存hook、各marker、IPC等がある場合は失敗し、上書きしません。コピーやrunの経路にシンボリックリンクやreparse pointがある場合も拒否します。dry-runだけでは権利・DLL・モデル・GPUや安全性の確認にはなりません。既存コピーの権限・リンク・予期しないファイルも利用者が確認してください。

## 実験実行の入口

1. 同梱hook/helper/smoke/chain、公式取得した固定hash重み、FFmpegを使います。sourceからビルドする場合は[BUILDING](BUILDING.md)へ。モデル・依存の取得条件を満たせない場合は進めません。GPU選択を確認し、他GPUへの対応を約束しません。
2. GPUを使用する次の比較は合成入力での自己整合性確認です。過去のゲーム由来参照と同じ画質比較ではありません。出力先は新規directoryです。

```text
python -B runtime/worker/stage.py compare --model <weights> --output-dir <new-comparison> --reference-mode synthetic --comparison-mode measure
python -B runtime/bridge/windows/smoke.py run --run-dir <new-smoke> --hook runtime/bridge/windows/overlay/d3d9-overlay.dll --chain runtime/bridge/windows/d3d9_chain.dll
```

3. 上の同一hook/chainでnative smokeが成功し、合成比較を本人が確認した場合だけ、以下は所有ゲームコピーにexclusive作成して起動する入口です。候補では新Windows動作を確認していません。

```text
python -B runtime/controller.py run --private-system <KF1-copy/System> --run-dir <new-run> --model <weights> --chain runtime/bridge/windows/d3d9_chain.dll --smoke-dir <new-smoke> --comparison-dir <new-comparison> --ffmpeg <ffmpeg.exe> --confirm-private-copy --confirm-visual-review --reference-mode synthetic
```

本人が入場して `waiting-for-user.txt` が作られた後、別の端末から `python -B runtime/controller.py signal --run-dir <new-run> start`。途中停止は同じコマンドの `start` を `stop` に変更します。自動入力・自動focusはありません。OS再起動時やGPU呼び出し停止時は監督処理が動く保証はありません。

元の実使用と同じく、ゲームコピーの設定でD3D9、windowed、960×540を選び、WestLondonを手動で開始します。Steamの正規認証・利用条件を回避しません。オンライン/anti-cheat有効serverでは使用しないでください。slotは固定960×540で、別解像度への自動変更・高解像度対応はありません。既定の実験窓は有限で、`--duration` の上限は200秒です。長時間安定を目的とした手順ではありません。

## 無効化と復元

通常停止後も専用コピーを点検します。停止信号は上記 `stop`。クラッシュ後はゲームとcontrollerが本当に終了していることを利用者が確認し、同一runの `recovery.json` を使います。これはプロセスを検索・停止しません。

```text
python -B scripts/recover.py disable --system <KF1-copy/System> --run-dir <new-run>
python -B scripts/recover.py restore --system <KF1-copy/System> --run-dir <new-run>
```

どちらも既定ではdry-runです。明示的に実施する場合のみ `--apply --confirm-stopped` を追加します。disableは記録と同一のhookのみ、restoreは記録と同一のhook/chainのみを削除します。hash・inode・権限・リンク数が変わった場合は拒否し、元ファイルを書き戻しません。実施中の競合変更を安全に処理する保証やWindows ACLの保証はありません。中途失敗は非原子的で、結果を再点検してください。receipt発行前の停止では自動復元できません。IPC、marker、journal、録画等は自動削除しないため、専用コピーごと隔離して手動確認してください。元ゲームの上書きを復元する機能ではありません。
