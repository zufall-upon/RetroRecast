# 由来・ライセンス・非同梱資産

ライセンス本文は `third_party/licenses/`、適用範囲は[LICENSE_SCOPE](LICENSE_SCOPE.md)を参照してください。出典やhashは同一性の情報であり、ゲーム・学習データを含む包括的権利保証ではありません。

## ソースと変更

- **bridge**：`runtime/bridge/source/` のBSP/可視構造関連は TheBestPlan/killingfloor-map-importer、revision [`88d4b6376a8cc8775d207ad9e5d831992c4b0022`](https://github.com/TheBestPlan/killingfloor-map-importer/tree/88d4b6376a8cc8775d207ad9e5d831992c4b0022)、Copyright (c) 2026 TheBestPlan、GPL-3.0-or-laterに由来します。元layoutを使った派生の表示を保持し、hook/include閉包・ビルド入口をsourceとして提供します。過去の私有作業を記した[元notice](third_party/licenses/bridge-NOTICE.md)は由来記録であり、今回の独自部分のGPL許諾はLICENSE_SCOPEのとおりです。ゲームのROM・scene dataは同梱しません。
- **REGEN generator**：Stefanos Pasios、revision [`de240056522d066235b48b541e7d49f28c80f1ed`](https://github.com/stefanos50/REGEN/tree/de240056522d066235b48b541e7d49f28c80f1ed)の `generator.py` を未改変で保持。保存[REGEN-LICENSE](third_party/licenses/REGEN-LICENSE.txt)・[code license](third_party/licenses/REGEN-code-LICENSE.txt)はBSD-2-Clause系、NVIDIA pix2pixHD、CycleGAN/pix2pixの各copyright・条件を含みます。これらをGPLへ上書きしません。
- **独自統合**：D3D9 loader/hook、frame処理、単一slot、worker/controller/capture、atomic helper、テスト等の追加部分はGPL-3.0-or-later。2026年11月の公開用変更は、ゲーム画素を必要としない明示synthetic oracle、衝突・リンク拒否、復元receipt、公開build/検査/package入口です。画質・モデル・ゲームplayの改善ではありません。元の表示と凍結ファイルhashを保持します。

## Releaseのバイナリ

| ファイル | 出典・条件 |
|---|---|
| `runtime/bridge/windows/overlay/d3d9-overlay.dll` | 同梱GPL対応sourceからcross-build。既存実使用hookとbyte一致。manual上限200秒のvariant。 |
| `runtime/worker/windows/slot_atomic_x64.dll` | 同梱 `slot_atomic.c`、GPL-3.0-or-later、LLVM-Mingw cross-build。 |
| `runtime/bridge/windows/chain-smoke.exe` | 同梱 `chain-smoke.cpp`、GPL-3.0-or-later、LLVM-Mingw cross-build。今回native実行なし。 |
| `runtime/bridge/windows/d3d9_chain.dll` | **無改変のProton Experimental由来DXVK i386**。埋め込み版 `v3.1.1-97-ga78a467cadb22be`、上流commit [`a78a467cadb22bee928904290cf8e7921b121851`](https://github.com/ValveSoftware/dxvk/tree/a78a467cadb22bee928904290cf8e7921b121851)。zlib/libpng。保存実使用bytesを再利用し、別のDXVKへ交換していません。Proton全体やWineを同梱するものではありません。 |

DXVKのzlib/libpng条件は利用・改変・再配布を許可し、元作者の誤表示や改変sourceの偽装を禁止します。同一binaryの固定配布URLを許諾条件としてはいません。実使用hashと埋め込みrevisionは対応表に残しますが、DXVK自体の再現ビルドを達成したとは称しません。

関連表示を保持します：DXVK（Philip Rebohle、Joshua Ashton、Robin Kertels、Jeffrey Ellison）、dxbc-spirv（Philip Rebohle、MIT、commit `ecb8308fe03fcad695a6ebdacbef6e7f61e77484`）、libdisplay-info（contributors、MIT、commit `275e6459c7ab1ddd4b125f28d0440716e4888078`）、OpenVR headers（Valve、BSD-3-Clause）、Vulkan/SPIRV headers（Khronos、保存MIT/Apache等）、SHA-1（Steve Reid、public domain）。各本文は保存licenseを参照。GCC runtimeはGPLv3とGCC Runtime Library Exception 3.1の組合せで、FSFの表示を保持します。GCC/LLVM・MinGW本体やOS DLLは配布しません。

独自binaryの静的リンク部分についてLLVM Apache-2.0 WITH LLVM-exception、MinGW-w64 runtime、winpthreadsの本文も添付しています。コンパイラ版・flags・source hash・binary hashはReleaseの `build-receipts.json`、ソースtree識別は `package-manifest.json` に記録します。依存をまとめてMITにしません。

## モデル・ゲーム・環境は利用者が取得

重みは**非同梱**です。REGEN作者は公式READMEのTesting / Real-Time Inference / Integrationで公開pretrained modelsの取得・試験・統合を案内しています。その案内に従い、利用者本人が公式CARLA2Fluxを取得する研究用入口を提供します。コードのBSDを別配布checkpointへ一律適用せず、重み・教師FLUX・学習データの再配布権や出力の独占権を本プロジェクトから付与しません。権利・利用条件は提供元の案内を確認し、条件を満たせない場合は実行しないでください。固定hash・sizeはQUICKSTARTにあります。

正規KF1は各利用者の購入・利用許諾が必要です。ゲーム本体、Steam関連data、map/texture/audio、SDK、ゲーム由来の起動用fixture、Python/PyTorch/NumPy環境、FFmpeg、driver、私有ログ・相談・履歴・認証情報は同梱しません。

## 紹介画像

`assets/church-input-output.png` は本人が指定した既存Windows教会比較の一枚で、左がKF1のworld入力、右が同じrequestの量子化CARLA2Flux出力です。研究対象と構造保持の限界を論評するため、README本文と区別した比較引用として掲載します。最終合成画面・新しい実験・品質改善の証拠にはしません。

ゲーム画面の権利はTripwire Interactiveほかにあり、当projectのコードlicenseで再許諾しません。[TripwireのGameplay and Related Videos policy](https://tripwireinteractive.com/gameplay-and-related-videos-policy)は非商用・無料閲覧・ゲームへの関連・クレジットを条件として動画等の使用を案内しています。ただし、このポリシーを静止画・AI出力の包括的ライセンスと読み替えていません。ここでは無料・非商用の研究比較引用の最小限の一枚に限定し、ゲーム資産そのものの再配布や素材集にはしません。画像の再利用は各自の法域・提供元条件に従ってください。Tripwire/REGEN等の公認・権利侵害不存在・AI出力の権利確定を保証しません。問題の連絡はGitHub Issueへお願いします。

<a name="gameplay-video"></a>
## Gameplay video / 実プレイ動画 / 游戏视频

- **Killing Floor (2009) © Tripwire Interactive and other rights holders.** The 60-second KF1 Revival / RetroRecast PV is an owner-authorized, free-to-view, non-commercial research preview edited from two existing Windows gameplay recordings. Tripwire's [gameplay video policy](https://tripwireinteractive.com/gameplay-and-related-videos-policy) permits credited, game-related, non-commercial videos; this does not grant ownership of game assets or blanket rights to AI outputs/models. No music, voice or other audio stream is included; no new gameplay was recorded for this page.
- The original PV remains unchanged: **60.000 seconds, 1920×1080, encoded at 60 fps, 141,045,476 bytes; SHA-256 `58eda100bfc208671292d5c3728e772d8c6fe382ec4fece1df21accfa94cff50`**. The on-screen historical “private preview / in development” title is retained. A smaller web viewing copy may reduce resolution/frame rate; neither encoding is an AI inference benchmark.
- The PV uses six normal-speed cuts from two separate recordings. Their model/processing settings are unidentified; they are not “original versus AI,” a continuous three-minute playthrough, or proof of the released model's performance. It includes experimental AI-rendered visuals from real gameplay, not newly generated fictional gameplay. Visual artifacts, performance issues and dark-scene limitations remain.
- **日本語：** 本人が公開を依頼した既存60秒PVを、無料・非商用・出典付きで掲載します。音声streamはなく、原本・受領録画を保全しています。私有の会話・ログ・ソース履歴は公開しません。ゲーム映像・量子化AI画像・モデル・コードの権利は別で、コードlicenseの対象ではありません。
- **简体中文：** 该60秒PV来自所有者授权分享的既有录像，免费、非商业展示并保留游戏署名，无音频流。保留原件，不公开私有会话或历史。游戏视频、AI图片、模型和代码的权利各自独立，不能以代码许可证重新授权。
