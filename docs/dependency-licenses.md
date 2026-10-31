# 外部依存のライセンス一覧

本プロジェクトの自作部分は [MIT License](../LICENSE) で提供しています。ビルドおよび配布物に含まれる外部依存のライセンスは以下の通りです。

## 配布プラグインの依存先

| 対象 | ライセンス | 利用形態と配布時の対応 |
| --- | --- | --- |
| QuickJS-ng v0.17.0 | MIT License | `Expression.mod2` に静的リンクしています。`Script/Expression/licenses/QuickJS-ng.txt` に著作権表示と許諾文を同梱しています。 |
| QuickJS-ng内のUnicodeデータ・個別ソース | Unicode License V3 / MIT License | QuickJS-ngのソースおよびヘッダーに含まれる個別の著作権表示・許諾通知を `Script/Expression/licenses/QuickJS-ng-NOTICES.txt` に原文のまま収録しています。 |
| AviUtl ExEdit2 Plugin SDK | MIT License | `module2.h`、`plugin2.h` 等をビルドに使用しています。`Script/Expression/licenses/AviUtl2-SDK.txt` に著作権表示と許諾文を同梱しています。 |

出典:

- [QuickJS-ng v0.17.0 LICENSE](https://github.com/quickjs-ng/quickjs/blob/v0.17.0/LICENSE)
- [QuickJS-ng v0.17.0 libunicode-table.h](https://github.com/quickjs-ng/quickjs/blob/v0.17.0/libunicode-table.h)
- [QuickJS-ng v0.17.0 quickjs.c](https://github.com/quickjs-ng/quickjs/blob/v0.17.0/quickjs.c)
- [QuickJS-ng v0.17.0 quickjs-c-atomics.h](https://github.com/quickjs-ng/quickjs/blob/v0.17.0/quickjs-c-atomics.h)
- [AviUtl2公式SDK配布元](https://spring-fragrance.mints.ne.jp/aviutl/)（SDK内 `license.txt`）

## ホスト環境・ランタイムについて

- AviUtl2本体および本体付属の `lua.dll`・`luaJIT.dll` は配布物には含めず、実行時にホスト側が提供する環境を利用します。
- Windows向けのビルドではMSVCランタイムおよびWin32 APIを使用します。
