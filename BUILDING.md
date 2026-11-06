# ビルドとCPU検査

## 検査

Linuxの Python 3 と NumPy を利用する既存環境で、GPU・ゲーム・モデルなしで実行できます。依存を自動installせず、合成画素だけを使います。Windowsで同じテストを行うには、同梱していないx64 atomic helperの別途ビルドが必要です。

```text
python3 -B scripts/check_source.py --regression
```

RGB/BGRA、上下pad、slotのsequenceとエラー、合成fixtureのserve/prime、競合拒否、停止後のreceipt付き復元境界を検査します。CPU mockであってWindows、GPU、実ゲーム、画質、安全性の合格ではありません。

## クロスビルド

既存のllvm-mingwを明示してください。`output-dir` はソースtreeの外の未存在directoryです。インストールやdownloadは行いません。

```text
python3 -B scripts/build.py --compiler-i686 <i686-w64-mingw32-clang++> --compiler-x64 <x86_64-w64-mingw32-clang> --output-dir <new-build>
python3 -B runtime/bridge/windows/build_smoke.py --compiler <i686-w64-mingw32-clang++> --output-dir <new-smoke-build>
```

既存LLVM-Mingw clang 23.1.4、C++17 `-O2 -shared -static` と固定link flagsではhook SHA-256 `4096f1f380cb346f2418105b5b4b1617e7b71a5be49e1e38520bfe1da6b1486f`、helper `43ff50ab95eb10c2b7fcbcde37a33c4d00adfa327307ca8befda8824376c2cba` に一致しました。元の出力名 `d3d9.dll` でリンク後にrenameします。名前を変えてリンクするとPE内容が変わります。smokeは `3752972d5390f229dcce22fc728eb76575ffb49c876664781f94fca822ff2a37`。いずれも今回のbuild成功はWindows実行証明ではありません。compiler、フラグ、source hash、exit、output hashはbuild receiptで確認します。

結果の `d3d9-overlay.dll` を `runtime/bridge/windows/overlay/`、`slot_atomic_x64.dll` を `runtime/worker/windows/`、`chain-smoke.exe` を `runtime/bridge/windows/` に配置すると実行treeにできます。Release ZIPにはこれらと実使用chainを含みます。source checkoutへ手動配置した場合はsource-only監査とは別構成になります。重み、Windows GPU環境やFFmpegはビルドされません。実使用と異なるchainや新しい合成fixtureは別candidateです。

## 実行ZIPを作る

通常のsource checkoutはbinaryを含まず、Releaseのruntime ZIPはsourceとbinaryを一緒に含みます。`scripts/package.py` はcleanなsource commit、上記2つの新build directory、固定hashの実使用chainを明示して作成します。指定の出力directoryは新規でなければなりません。内部の履歴・ゲーム・モデル・環境を再帰的に収集しません。

```text
python3 -B scripts/package.py --hook-build <new-build> --smoke-build <new-smoke-build> --chain <matching-d3d9-chain.dll> --output-dir <new-release-dir>
```

チェーンを自作する場合の元ソースrevision・部品licenseはNOTICE参照。このAlphaのpackage入口は既存実使用hashだけを許可し、別buildを同一の実証済み物として受理しません。新しいbuildを研究する場合は候補identityと未検証範囲を分けてください。

配布ZIPの `python -B scripts/verify_package.py` は標準libraryだけで全payloadを点検します。CPUテストは `python -B -m unittest discover -s tests -p "test_*.py"`。source checkoutの `check_source.py --regression` は同じCPUテストに加え、選別source・index・内部path・秘密候補を検査する入口です。[LICENSE_SCOPE](LICENSE_SCOPE.md)を保ち、対応sourceとbuild入口を配布してください。
