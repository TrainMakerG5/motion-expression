# 開発ガイド

この文書は、Expressionをソースコードからビルドし、動作を確認するための手順です。
コマンドはリポジトリのルートでPowerShellから実行します。
利用方法は [README](../README.md) を参照してください。

## 開発環境

- Windows x64
- Visual Studio 2022以降のC++開発環境、CMake 3.24以上、Ninja
- AviUtl2 SDK 2026/10/31
- QuickJS-ng v0.17.0

SDKとQuickJS-ngは `bootstrap.ps1` で取得します。
本体を使うテストには、AviUtl2のZIP版を別途用意します。

## ビルド

作業フォルダでPowerShellを開き、以下のコマンドを実行します。

```powershell
./scripts/bootstrap.ps1
./scripts/build.ps1
```

- `bootstrap.ps1`: SDKとQuickJS-ngを取得し、SHA-256ハッシュを確認します。
- `build.ps1`: x64 Release構成でビルドし、単体テストを実行した上で成果物を `dist` に配置します。
- `tools/aviutl2` に `lua.dll`・`luaJIT.dll` がある場合は、それぞれと実際の `.mod2`・`.anm2` を使った連携テストもあわせて実行します。
- CMakeは環境変数 `PATH`、またはVisual Studio付属のものを自動的に探します。

## 検証用AviUtl2への配置

あらかじめAviUtl2のZIP版を `tools/aviutl2` に展開してから実行します。

```powershell
./scripts/install-local.ps1
```

検証用本体の `data/Script/Expression` と `data/Plugin/Expression` にファイル一式を配置します。
AviUtl2本体を終了した状態で実行し、配置後に `tools/aviutl2/aviutl2.exe` を起動してください。
初回起動時は、追加したプラグイン・スクリプトの信頼確認ダイアログが表示されます。

## 自動検証

`build.ps1` では、JS式の評価・エラー処理・タイムアウト・保存形式の単体テストと、付属のLua/LuaJITを用いた連携テストを実行します。
AviUtl2実機を用いた自動検証は、以下のコマンドで実行できます。

```powershell
./scripts/test-host.ps1
```

別の本体を使用する場合は、`-HostDirectory tools/aviutl2-2.1.16` のように配置先のフォルダを指定できます。

このスクリプトは `tools/parameter-test` に検証用の別コピーを作成し、そのプロセスのみを起動・終了します。
このコピー内に配置する自作ファイルだけを信頼済みに設定し、通常作業用に起動しているAviUtl2本体には干渉しません。

実機検証では、以下の項目を実際のホスト上で自動確認します。

- X座標および「ぼかし」の強さ（範囲）の式による制御
- 式の無効化と再有効化
- 組み込みパネルでの式の追加・編集・計算確認・適用・エラー表示
- 未適用コードの保持と破棄、パネルサイズ変更への追従
- 編集カーソル移動に連動した「現在値」「未適用の式」の自動プレビュー
- プロジェクトの保存と再読み込み
- JSテキストの文字列・文字色・表示状態と、JS制御のエフェクトの色・チェック項目・表示状態の実際の描画

検証結果のログは `tools/parameter-test/host-check.log` に、そのまま開いて試せるデモプロジェクトは `tools/parameter-test/Expression-demo.aup2` に出力されます。
文字列・色・表示状態のデモは `tools/parameter-test/Expression-extended-demo.aup2` に出力されます。
なお、実機検証用のプラグイン `ExpressionProbe.aux2` は配布用の `dist` フォルダには含まれません。

## 実装と保存形式

QuickJS-ngによる式の評価を `src/engine.cpp`、AviUtl2との接続を `src/module.cpp`、
式編集パネルを `src/editor.cpp` に実装しています。

数値項目の式は `.aup2` のプラグインデータに保存し、対象のトラックバーに式IDを設定します。
編集ごとに新しい式IDを割り当て、変更前の式を残すことでUndoに対応しています。
JS制御とJSテキストのコードは、各エフェクトのテキスト設定として保存します。
