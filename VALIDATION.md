# 検証範囲と実使用物の対応

## 再利用した実績

既存Windows実使用物の56 payloadは、元stageと54一致、controller/captureの2点が後の修正版でした。本配布は後者を基準とします。修正はvenv redirectorからの子生成前にJob所有を確定するgateと、encoder失敗時の有限回収です。Windowsの同一frame AI描画と本人操作の限定記録があり、別の試験では2,086同frame pair後にOS異常再起動が観測されました。成功・失敗を分け、正常終了・OS安全・長時間安定の証拠とはしません。

## 今回実施したこと

- 公開sourceのC++ include閉包とbuild設定を固定。LLVM-Mingw clang 23.1.4でhook/helper/smokeをcross-buildし、下の既存binaryとbyte一致。Windowsで実行してはいません。
- モデルやゲームを使わないCPU合成テスト：BGRA/RGB、上下pad、raw量子化、slotのsequence/error、起動前oracleから別入力2frameの通常経路、controllerの所有順、競合拒否、復元dry-runと変更file拒否等。
- Release生成時に全payload/hash、除外資産、source commitとの一致、展開後CPUテストを検査。これらはゲーム成功・安全性の証明ではありません。

| 対象 | SHA-256 |
|---|---|
| hook | `4096f1f380cb346f2418105b5b4b1617e7b71a5be49e1e38520bfe1da6b1486f` |
| helper | `43ff50ab95eb10c2b7fcbcde37a33c4d00adfa327307ca8befda8824376c2cba` |
| smoke | `3752972d5390f229dcce22fc728eb76575ffb49c876664781f94fca822ff2a37` |
| 無改変の実使用chain | `70571734e0758c9791f46d5b0d26a0cc0dd110e07093b5e82f6069da3fd4a2bc` |

## 元構成との重要な差

元の起動前oracleは私有の実ゲーム画像3点の固定hashを要求しました。これらは配布せず、明示 `--reference-mode synthetic --comparison-mode measure` の自己整合性検査へ切り替えます。旧oracleの画質比較とは異なり `matches=null`、`visual_quality_assessed=false` です。**実game frame処理のモデル・前後処理・slot契約は変えず**、oracle後はsequence 1へ戻って任意の次frameを処理します。CPUテストはこの到達性を確認しますがGPU推論を実行しません。

導入ではリンク経路拒否と、exclusive作成したDLLのhash/inode/modeを保持する復元receiptを追加しました。既存fileの上書きはしません。これはWindows ACL・突然の電源断・OS再起動・途中競合を解決するものではなく、専用コピーとバックアップを必須にします。

**この再梱包全体のWindows/GPU/ゲーム・導入復元は再検証していません。** 過去の実機結果を新candidate全体の合格へ転用しません。新しいゲーム/GPU/Windows試験・録画再試行は行わず、未解決リスクを残したExperimental Alphaです。性能数値をゲームFPSや未検証GPUへ外挿しません。
