# 参加の入口

まず[BUILDING](BUILDING.md)のCPU合成契約テストを確認してください。[LICENSE_SCOPE](LICENSE_SCOPE.md)の条件でsourceを改変し、目的・変更内容・対象テストの結果を添えてPRを送れます。ゲームやモデルなしで取り組める最初の課題は次の3つです。

1. **合成frame契約**：色順・pad・sequence・失敗通知の小さな回帰テストを追加する。`tests/test_public_contract.py` が入口です。
2. **導入・復元境界**：既存fileを保つ衝突拒否やdry-runのエラーメッセージ/テストを改善する。OS再起動を安全とする課題ではありません。
3. **build再現性**：公開sourceからx86 hook/x64 helperを再buildし、compiler/flags/hash差を報告する。ゲーム・モデル不要です。

私有fixtureを必須にする参加課題は作りません。暗所の構造保持は難しい未解決の研究課題で、good first issue扱いや改善の約束はしません。追加ゲーム/GPU実験を参加の前提にせず、静的検討・CPUテスト・buildから始められます。

問題報告では秘密・ゲームdata・ローカル絶対pathを公開しないでください。暗所の構造保持は未解決の研究課題であり、改善済みではありません。Windows再起動等の安全性は未解決で、再現実験を要求しません。
