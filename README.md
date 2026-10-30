# Expression — AviUtl2向けJavaScriptエクスプレッション

パラメーターを右クリックしてJS式を付け、その数値を計算する試作です。
座標だけでなく、ぼかしの強さなど、トラックバーで設定するエフェクトの数値を対象にします。
QuickJS-ngの計算部分とAviUtl2の接続部分を分けています。

## 検証環境

- Windows x64
- AviUtl2 2.1.15（SDK 2026/10/24）
- Visual Studio 2022以降のC++開発環境、CMake 3.24以上、Ninja
- QuickJS-ng v0.17.0

## ビルド

作業フォルダでPowerShellを開きます。

```powershell
./scripts/bootstrap.ps1
./scripts/build.ps1
```

`bootstrap.ps1` はSDKとQuickJS-ngを取得し、SHA-256を確認します。
`build.ps1` はx64 Releaseをビルドし、単体検証を実行して `dist` に配置します。
`tools/aviutl2` に `lua.dll`・`luaJIT.dll` があれば、それぞれと実際の `.mod2`・`.anm2` を使った連携検証も実行します。
CMakeはPATH、またはVisual Studio付属のものを探します。

## 検証用AviUtl2へ配置

ZIP版を `tools/aviutl2` に展開してから実行します。

```powershell
./scripts/install-local.ps1
```

検証用本体の `data/Script/Expression` と `data/Plugin/Expression` に配置します。
AviUtl2本体は終了した状態で配置し、その後 `tools/aviutl2/aviutl2.exe` を起動します。
初回は追加したプラグイン・スクリプトの信頼確認が表示されます。

## パラメーターごとの式（0.2.0）

1. X座標やぼかしの「範囲」など、数値の項目を右クリックします。
2. 「JSエクスプレッションを編集」を選びます。
3. 式を入力し、「計算を確認」で現在時刻の計算結果を確認します。
4. 「適用して閉じる」で対象のパラメーターに結び付けます。

編集作業なしで試す場合は、更新した検証用AviUtl2で `examples/Expression-demo.aup2` を開きます。
X座標に `value + time * 60`、ぼかしの範囲に `value + time * 30` を設定済みです。
対象の数値を右クリックすると、登録済みの式を編集できます。

```js
value + Math.sin(time * 2) * 100
```

`value` は式を適用する前の数値、`time` はオブジェクト開始からの秒数、`frame` はフレーム番号です。
これらは読み取り専用です。最後に評価された式の数値が結果になります。

```js
const amplitude = 100;
const frequency = 2;
value + Math.sin(time * frequency) * amplitude;
```

トップレベルの `return` は使いません。結果は有限の数値である必要があります。
「式を有効にする」を外して適用すると、式を保持したまま元の値・直線移動を使います。
「元設定に戻す」で式の結び付けを外せます。元の移動方法へ戻し、編集した基準値は保持します。

式は `.aup2` のプラグインデータとして保存します。対象パラメーターには式IDを保存します。
同じ式の付いたオブジェクトを複製しても、片方の式の編集はもう片方へ影響しません。
編集のたびに新しい式IDを割り当て、以前の式もプロジェクトに残すことでUndo先の式を保持します。

初版では固定値と、加減速・時間制御・参照式を使わない直線移動に対応します。
それ以外の移動方法を設定済みの項目には適用せず、エディターで理由を表示します。
色・文字列・チェックボックスの制御や、AEのAPI互換には対応しません。

## オブジェクト単位の旧試作

1. 図形やテキストをタイムラインへ置きます。
2. アニメーション効果から、Expressionの「JS制御」を追加します。
3. 初期コードでは左右に揺れます。Aが振幅、Bが速さです。
4. 「JavaScript」のテキスト欄を編集して動作を変えます。

### 書き方

```js
const amplitude = controls.a;
const speed = controls.b;

obj.x += Math.sin(time * speed) * amplitude;
obj.rotation += time * 30;
```

| 名前 | 意味 |
| --- | --- |
| `time` | オブジェクト開始からの秒数 |
| `frame` | オブジェクト基準の現在フレーム番号 |
| `controls.a`, `.b`, `.c` | 効果にあるA・B・Cトラックバーの値 |
| `obj.x`, `.y`, `.z` | 相対座標。Luaの `obj.ox`・`oy`・`oz` に対応 |
| `obj.rotationX`, `.rotationY`, `.rotation` | X・Y・Z軸回転。単位は度 |
| `obj.scaleX`, `.scaleY`, `.scaleZ` | 軸ごとの拡大率。1が等倍 |
| `obj.opacity` | 不透明度。0〜1 |

`obj` の各項目は効果に入った時点の値で初期化されます。
計算結果は描画時の値へ反映します。標準描画などの設定欄を書き換えるものではありません。
式はプロジェクト内の効果のテキスト設定として保存されます。

`const`、`let`、関数、配列、条件分岐、`Math` を使えます。
ブラウザーやNode.jsのAPIは提供しません。
`return` の値は使わず、`obj` のプロパティへの代入を反映します。

## 共通の制限

- 毎回独立したJS実行環境を作ります。前フレームの変数やグローバル変数は保持しません。
- 別時刻のキーフレーム参照、別オブジェクト参照、AEのAPI互換はありません。
- コードは64 KiB、JSのメモリは16 MiB、JSの実行には約50 msの打ち切りを設定しています。
- オブジェクト単位の旧試作では、不透明度は0〜1で指定します。
- パラメーターごとの数値の範囲や精度は、対象トラックバー側の制限にも従います。
- 適用前の現在時刻の確認に加え、再生中の式の失敗はLuaエラーとして通知します。エラー時の任意フレームへのフォールバックは未対応です。
- エラーには `expression.js` の位置が含まれる場合があります。旧試作では内部ラッパーにより行番号が入力欄より2行多くなります。
- 1プロジェクトの式履歴は最大4096件、保存データは最大16 MiBです。
- 別プロジェクトへオブジェクトを持ち込む用途や、式IDだけを持つエイリアスの持ち運びには未対応です。
- 映像の目視確認と、書き出し結果の一致は別途確認が必要です。

## 自動検証

`build.ps1` でJSの評価・エラー・タイムアウト・保存形式と、付属Lua/LuaJITの連携を確認します。
実機検証は次のコマンドです。

```powershell
./scripts/test-host.ps1
```

`tools/parameter-test` に別の検証用コピーを作り、そのプロセスだけを起動・終了します。
自作の検証ファイルだけの信頼確認を処理します。現在起動中の通常の検証用本体は操作しません。
X座標とぼかしの強さ、式の無効化、エディターでの追加・編集・計算確認・適用・エラー表示、プロジェクト保存・再読み込みを実際のホストで検証します。
結果は `tools/parameter-test/host-check.log`、開いて試せるプロジェクトは `tools/parameter-test/Expression-demo.aup2` に出力します。
検証用 `ExpressionProbe.aux2` は `dist` には含めません。

## 参照

- [AviUtl2・公式SDK](https://spring-fragrance.mints.ne.jp/aviutl/)
- [QuickJS-ng](https://github.com/quickjs-ng/quickjs/tree/v0.17.0)

配布物には両依存先のライセンスを同梱します。
