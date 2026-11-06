# 公開ソースの由来と留保

`upstream/read.js` は TheBestPlan/killingfloor-map-importer の固定revision
`88d4b6376a8cc8775d207ad9e5d831992c4b0022`、`src/unreal/read.js`。
Copyright (c) 2026 TheBestPlan, SPDX-License-Identifier: GPL-3.0-or-later。
原licenseを `upstream/LICENSE` に保持する。

`map_bsp.py` はそのpackage/UModel読み取りlayoutをPythonへ適用した派生で、
GPL-3.0-or-later。追加した厳密範囲・版・参照検査も同licenseである。
この実験variantはローカル私有利用のみで、公開・配布・license整合性の最終判断は
行っていない。元ゲームのdataや公開sourceに新たな権利を付与するものではない。
RTX Remix/KF1互換処理の前段固定revision・noticeは既記録を継承する。

過去試験の限定Shader選別は実utxのpropertiesを解析し、Opacity=None、
OutputBlending=OB_Normal、Diffuseが確認済みplain opaque Textureの場合に限っていた。
020候補はplain Texture限定で、Shaderは未知面としてray遮蔽物に留めた。
この新候補は既保存019と020のシーンを照合して019だけの確認済Shader
三角形を元Drawと厳密照合する。新たなBSP解析ではない。両保存シーンには
node別の材質名がないため、個別のLondonWetPavement識別は未確認である。
過去のenum/zero defaultの検討では以下の公開textと比較した（コードの転記はしていない）。
- https://www.ericdives.com/UT2004-UnCodex/Source_engine/shader.html
- https://github.com/gildor2/UEViewer/blob/a0bfb468/Docs/material.txt
この一般UE2知見だけで現KF1の実描画一致を証明せず、可視maskを別途判定する。
