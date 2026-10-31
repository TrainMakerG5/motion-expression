# 外部依存のライセンス確認

確認日: 2026年10月31日

対象は、現在のソース、固定済みの依存ファイル、CMakeのリンク設定と `dist` の配布内容です。依存先を更新した際は再確認が必要です。

## 配布プラグインの依存先

| 対象 | 確認したライセンス | 利用形態と対応 |
| --- | --- | --- |
| QuickJS-ng v0.17.0 | MIT | `Expression.mod2` に静的リンクします。著作権表示と許諾文を同梱します。 |
| QuickJS-ng内のUnicodeデータ | Unicode License V3 | `libunicode.c` が `libunicode-table.h` を取り込みます。データまたは付属文書への著作権表示と許諾文の保持が必要です。 |
| QuickJS-ng内の個別ソース | MIT | ルートのLICENSEと著作権者・年が異なる通知も原文で保持します。 |
| AviUtl ExEdit2 Plugin SDK | MIT | `module2.h`、`plugin2.h` 等をビルドに使用します。SDKの著作権表示と許諾文を同梱します。 |

これらの条件を守る限り、自作部分をMIT Licenseとして配布する方針と両立する内容と判断しました。商用利用・改変・再配布を許すライセンスですが、依存部分の表示を自作部分のLICENSEだけで置き換えることはできません。Unicodeの通知には、権利者名を宣伝に使用する際の条件も記載されています。

根拠:

- [QuickJS-ng v0.17.0 LICENSE](https://github.com/quickjs-ng/quickjs/blob/v0.17.0/LICENSE)
- [Unicodeデータの原文通知](https://github.com/quickjs-ng/quickjs/blob/v0.17.0/libunicode-table.h)
- [個別の著作権表示があるquickjs.c](https://github.com/quickjs-ng/quickjs/blob/v0.17.0/quickjs.c)
- [Marcin Kolny氏の通知があるquickjs-c-atomics.h](https://github.com/quickjs-ng/quickjs/blob/v0.17.0/quickjs-c-atomics.h)
- [AviUtl2公式SDK配布元](https://spring-fragrance.mints.ne.jp/aviutl/) の取得済みSDK内 `license.txt`。対象アーカイブのSHA-256は `7A8733D9611FE26FE42671CD9FCD93CA07A77D5A412441DCFDABA017401B9499` です。
- 本リポジトリの `CMakeLists.txt`、`scripts/bootstrap.ps1` と取得済みQuickJS-ngの `CMakeLists.txt`。

## 今回確認した不足と修正

確認前の配布フォルダにはQuickJS-ngとSDKのライセンスはありましたが、Unicode等の追加通知と本プロジェクト自身のLICENSEがありませんでした。

次のファイルをCMakeのインストール対象に追加しました。

- `LICENSE`: 自作部分のMIT Licenseです。
- `Script/Expression/licenses/QuickJS-ng-NOTICES.txt`: QuickJS-ngの4つのライブラリソースと、その引用符形式の `#include`（`#include "..."`）を再帰的にたどった18ファイルに含まれる許諾ブロックを、原文のまま収録しています。同一ブロックはまとめています。条件付きinclude先も対象にしています。

既存の `QuickJS-ng.txt` と `AviUtl2-SDK.txt` も引き続き配布します。この確認記録は開発用としてリポジトリ内にのみ保持し、配布物には含めません。QuickJS-ngを更新した際は、ルートLICENSEだけでなく追加通知も見直してください。

## 検証環境だけで利用するもの

検証用のAviUtl2 v2.1.15本体と、付属するLua・LuaJITは、配布フォルダには含めていません。LuaとLuaJITはホストが提供する実行環境を利用します。

Lua・LuaJITのMITライセンスは取得済み本体の `credits.txt` で確認しました。AviUtl2本体の `aviutl2.txt` と[公式ページの使用上の注意](https://spring-fragrance.mints.ne.jp/aviutl/)には、プログラムファイルの不特定多数への再配布を控えるよう記載されています。そのため、本体の検証用コピーを配布ZIPへ含めず、利用者には公式から入手してもらう方針です。SDKのMITライセンスとは別の条件です。

## Windowsと開発ツール

Win32 APIとC++標準ライブラリも使用します。現在のCMake設定はMSVCの静的ランタイムを選択しています。Microsoft製ランタイムの配布条件は、プロジェクトのMIT Licenseではなく利用しているVisual Studioのライセンス条項に従います。

[Microsoft公式の再配布説明](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files)を確認しました。ただし、インストール済みVisual Studio 2026の条項全体と適用資格までは、この確認では評価していません。この点を含め、成果物全体がMITだけで構成されるという意味ではありません。

CMake・Ninja・Visual Studioのツール本体やWindowsのDLLは、現在の配布フォルダには含めません。
