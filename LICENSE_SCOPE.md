# ライセンスの範囲

RetroRecastの独自コード・テスト・文書、およびGPL由来bridgeの派生部分は **GPL-3.0-or-later** で提供します。改変・ビルド・再配布が可能です。[LICENSE](LICENSE)の条件、著作権表示と変更表示を保持してください。第三者部分には以下の元条件が優先し、包括的なMITへの置換はしません。

| 対象 | 条件と対応表示 |
|---|---|
| `runtime/bridge/source/*`、bridge側Python/C++、`runtime/worker/windows/*` | GPL-3.0-or-later。TheBestPlan由来部分の表示はNOTICEと保存bridge notice参照。独自の統合部分も同条件。 |
| `runtime/worker/generator.py` | REGENのBSD-2-Clause系および保存NVIDIA / pix2pix / CycleGAN条項。ファイルは未改変。 |
| その他の `runtime/*.py`、`runtime/worker/{core,slot,stage}.py`、`scripts/*`、`tests/*` | RetroRecast統合・公開用追加部分、GPL-3.0-or-later。変更概要はNOTICE。 |
| Releaseの `d3d9_chain.dll` | 独立したDXVK（zlib/libpng）、関連MIT/BSD部品およびGCC runtime例外。GPLへ再ライセンスしない。 |
| コンパイラの静的runtime部分 | LLVM Apache-2.0 WITH LLVM-exception、MinGW-w64、winpthreads等の保存条件。ツールチェーン本体は配布しない。 |
| `assets/church-input-output.png` | Killing Floorの画面と実験出力の研究比較引用。**コードのGPL許諾の対象外**。ゲームの権利はTripwire Interactive等、取得元と掲載目的はNOTICE。再利用の包括許諾を付与しない。 |
| 非同梱のモデル・ゲーム・Python/PyTorch/NumPy/FFmpeg/driver | それぞれの提供元の条件。コードのGPL/BSDを重みやゲームへ拡張しない。 |

本プロジェクトはTripwire Interactive、Valve、NVIDIA、AMD、REGEN作者の公認・サポート製品ではありません。ゲームの購入・利用権や画像・商標の権利を付与しません。ゲーム統合を有料配布・商用利用する案内でもありません。
