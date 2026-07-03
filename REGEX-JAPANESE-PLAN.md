# BE 正規表現関数の日本語対応計画（JpRegex 実装の移植）

作成日: 2026-07-03
対象: veltrea フォーク版 BaseElements-Plugin（FM11 32bit Windows を主対象、他プラットフォームは追従）
参考実装: `/Volumes/2TB_USB/dev/filemaker-plugin/regex/`（JpRegex — ICU ベース、FMP11/FMP19 実機検証済み）

## 結論

**可能。** BE の正規表現 API（関数名・引数・戻り値・オプション文字）と入出力を一切変えずに、
実装エンジンだけを JpRegex 方式（ICU）へ差し替えられる。変更は実質 `BERegularExpression.h`
1 ファイルに閉じる（呼び出し側 3 箇所は無修正）。ただしコストの異なる 2 段階があるため、
先に現状エンジン（Poco 同梱 PCRE）の実力を実機検証してから投資判断するのが合理的。

---

## 1. 現状分析（BE 側）

### 1.1 正規表現が使われている箇所（全 3 箇所 + 実装本体）

| 箇所 | 内容 |
|---|---|
| `Source/BERegularExpression.h` | 実装本体。`regular_expression<T>()`（find/replace）と `regular_expression_split()` の 2 関数。Poco::RegularExpression（PCRE1 ラッパー）使用 |
| `BEPluginFunctions.cpp:4717` `BE_RegularExpression` | 2〜4 引数（text; expression; options; replaceWith）。4 引数目の有無で置換/検索を切替。オプション `v` で入力を ¶ 区切りの値リストとして 1 行ずつ処理 |
| `BEPluginFunctions.cpp:922` `BE_FileReplaceText` | ファイル/コンテナ内容全体に対する置換（既定オプション `"gi"`） |
| `BEPluginFunctions.cpp:515` `BE_FileReadText` | delimiter 指定時の `regular_expression_split`（監査 M-5 で UTF-8 対応済み） |

登録: `BEPlugin.cpp:370` — `RegisterFunction ( kBE_RegularExpression, BE_RegularExpression, 2, 4 )`

### 1.2 現状の日本語対応レベル

**対応済み（過去の監査で修正済み）:**
- `RE_UTF8` フラグ付与 — マルチバイト文字の中間バイトへの誤マッチは解決済み
- `regular_expression_split` の UTF-8 aware 化（旧 boost::regex のバイトモードを置換）
- Poco 例外 code 0 の kErrorUnknown マッピング

**未対応（残る問題）:**

| # | 問題 | 影響 |
|---|---|---|
| J-1 | `\w` `\b` `\d` `\s` が ASCII 限定。Poco は PCRE の UCP オプションを公開していない | `\w+` が「日本語abc」の日本語部分にマッチしない。`\d` が全角数字にマッチしない |
| J-2 | `i`（RE_CASELESS）が非 ASCII に効かない可能性（UCP 未有効時の PCRE1 の仕様） | 全角英字 Ａ/ａ 等の大小無視が不完全 |
| J-3 | `\p{Hiragana}` `\p{Katakana}` `\p{Han}` 等の Unicode スクリプトプロパティが、Poco 同梱 PCRE のビルドフラグ（SUPPORT_UCP）依存で動くか不明 | 日本語テキスト処理の基本イディオムが使えない可能性 |
| J-4 | 全角/半角・ひらがな/カタカナ・互換文字（NFKC）の同一視手段が無い | 「ｱｲｳ」で「アイウ」を検索する類の曖昧検索が不可能。これは正規表現エンジン単体では原理的に解決できない |
| J-5 | PCRE1（Poco 同梱）自体が古く、Unicode バージョンも古い | 新しい絵文字・拡張漢字のプロパティが不正確 |

> J-1〜J-3 は「PCRE がどうビルドされているか」で実態が変わるため、**Phase 0 の実機検証が必須**。
> J-4 はエンジン差し替えでも解決せず、JpRegex 方式の「明示的正規化ヘルパー」が唯一の解。

---

## 2. 参考実装（JpRegex）の分析

### 2.1 構成と設計思想

- `src/regex_core.{h,cpp}`（約 500 行）— **FileMaker 非依存の ICU コア**。ICU と標準ライブラリのみに依存し、`tests/test_regex.cpp` で FM 抜きの単体テスト可能（テスタビリティ最優先の DI 設計）
- `src/JpRegex.cpp` — 薄い FM グルー（fmx::Text ⇄ icu::UnicodeString の UTF-16 無損失橋渡し）
- 設計の核: **エンジンは常にリテラル照合**。全角半角・かな・NFKC の同一視は「フォールドフラグ」ではなく、呼び出し側が `Width`/`Kana`/`Normalize` ヘルパーで subject（必要ならパターンも）を明示的に正規化してから渡す。これにより抽出結果・置換結果・位置が**原文どおり正確**になる

### 2.2 BE へそのまま流用できる資産

| 資産 | 流用方法 |
|---|---|
| `regex_core.{h,cpp}` | ほぼコピーで `Source/` に移植可。パターンキャッシュ（mutex 保護、RegexPattern 共有 + RegexMatcher 使い捨て）は FMS のマルチスレッドにも安全 |
| `third_party/icu/` 一式 | トリム済み static ICU のビルドスクリプト（win x86/x64 の bat、macOS、Linux、`icu-data-filter.json`）が揃っている |
| `tests/test_regex.cpp` + `tests/run.sh` | FM 非依存テストハーネス。半角カナ/NFKC/かな/サロゲートペア/CR 改行/後方参照の全ケース PASS 済み |
| FM11 32bit ノウハウ | **vcpkg static full ICU（x86）+ 旧 PE フラグで FMP11 実機検証済み**。「旧 PE + modern static ICU の相性」問題はクローズ済み |
| `ClearCache()` | BE の kFMXT_Shutdown に接続するだけ |

### 2.3 両者の設計対比

| 項目 | BE（現状） | JpRegex |
|---|---|---|
| エンジン | Poco::RegularExpression（PCRE1） | ICU RegexPattern/RegexMatcher |
| 内部エンコーディング | UTF-8（std::string） | UTF-16（icu::UnicodeString） |
| オプション文字 | `i m s x g`（+`v` は呼び出し側） | `i m s x g`（**完全に同じ文字割当**） |
| 空マッチ | RE_NOTEMPTY で抑止 | 素の ICU（find は空マッチを許す） |
| 置換後方参照 | `$1..$n`（PCRE subst） | `$1..$n` `${name}`（ICU） |
| パターンキャッシュ | なし（毎回コンパイル） | あり（mutex 保護） |
| エラー | Poco 例外 → BEPlugin_Exception | Result 構造体（メッセージ + オフセット） |

オプション文字の意味が完全に一致しているため、**API 互換の差し替えは自然に成立する**。

---

## 3. 移植案（3 案、併用可）

### 案 B: PCRE の UCP をインライン有効化（最小・ゼロコスト）

`BERegularExpression.h` でパターン先頭に `(*UCP)` を自動付与する（PCRE 8.10+ のインライン指定）。

```cpp
// constructor_options 組み立ての後
T prefixed_expression = T("(*UCP)") + expression;
```

- **前提**: Poco 同梱 PCRE が `SUPPORT_UCP` 付きでビルドされていること → **Phase 0 で実機検証**
- 効果: J-1（`\w` 等）と J-2（非 ASCII caseless）と J-3（`\p{Hiragana}`）が解決する可能性
- 利点: バイナリサイズ増ゼロ、変更 1 ファイル数行、リンク構成不変
- 限界: J-4（同一視）と J-5（古い Unicode）は解決しない。SUPPORT_UCP が無ければ何も得られない（コンパイルエラーになるので判定は容易）
- 互換性注意: 既存ソリューションで `\w` を「ASCII のみ」の意図で使っている式の挙動が変わる。
  常時付与ではなく**オプション文字 `u` の新設**（`options.find("u")` で付与）にすれば既存挙動をå®全維持できる。
  API に文字を 1 つ足すだけで、既存の入出力は不変

### 案 A: `BERegularExpression.h` の中身を ICU に差し替え（本命・JpRegex 同等）

`regular_expression<T>()` / `regular_expression_split()` の**シグネチャと呼び出し側 3 箇所は一切変更しない**。
内部だけを `UTF-8 → icu::UnicodeString → ICU regex → UTF-8` に置き換える。JpRegex の `regex_core.{h,cpp}`
を `Source/BERegexCore.{h,cpp}` として移植し、`BERegularExpression.h` はその薄いアダプタにする。

**互換レイヤーとして再現が必要な差分（ここが移植の本体作業）:**

1. **RE_NOTEMPTY 相当** — ICU の find は空マッチを許すため、`match-all`・`replace(global)`・`split`
   の各ループで「長さ 0 のマッチはスキップ（split は 1 進める）」を明示実装する。
   これを怠ると `a*` のようなパターンで出力が変わり、既存ソリューションを壊す
2. **global match の出力形式** — マッチ文字列を `FILEMAKER_END_OF_LINE`（= `"\r"`）で連結（現行と同一）
3. **split の末尾片** — 空でも常に含める（現行 `regular_expression_split` のコメントに明記された仕様）
4. **置換の後方参照** — `$0..$n` は両エンジン共通。リテラル `$` のエスケープ規則（ICU は `\$`）の差を
   テストで確認し、差があれば置換文字列の前処理で吸収
5. **オプションパース** — 現行と同じく「文字列中に文字が含まれるか」方式（`i m s x g` + 未知文字無視）
6. **エラー伝搬** — `U_REGEX_*` エラー → `BEPlugin_Exception(kErrorUnknown)`。現行も Poco の code 0 を
   kErrorUnknown に落としているので、`BE_GetLastError` の観測値は実質互換
7. **キャッシュ破棄** — `ClearCache()` を kFMXT_Shutdown（`BEPlugin.cpp` の Shutdown ハンドラ）へ接続

**効果**: J-1/J-2/J-3/J-5 を完全解決。おまけにパターンキャッシュで繰り返し呼び出しが高速化
（BE_RegularExpression は Loop 内で同一パターンを大量に呼ぶ典型ユースケースがある）。

**サイズ影響（最重要トレードオフ）:**

| ICU 構成 | .fmx への追加 | 検証状態 |
|---|---|---|
| vcpkg static full（x86） | +25〜30MB（JpRegex x86 = 33MB 実績） | **FMP11 実機検証済み**（確実だが巨大） |
| トリム済み static（icu-data-filter.json: uprops + nfc/nfkc + 全半/かな translit のみ） | +5〜8MB 程度（JpRegex Linux trim = 9.4MB 全体の実績から推定） | mac/Linux は検証済み。**win x86 trim は未検証**（既知リスク: 旧 PE フラグとの相性） |

現行 .fmx は 3.7MB（belibs 隔離後）なので、trim でも 2〜3 倍になる。許容判断はユーザー要確認。

**リンク構成の整合性:**
- vcpkg triplet `x86-windows-static` は /MT → 本フォークの「ピュア MSVC /MT」方針と整合。
  belibs.dll のような隔離は不要で、.fmx に直接静的リンクできる
- ICU は C++ ライブラリだが MSVC ビルド（vcpkg）なので ABI 問題なし（mingw 系を混ぜない鉄則を維持）

### 案 C: 日本語ヘルパー関数の追加（J-4 の解決、既存 API は不変）

J-4（全角半角・かな・互換文字の同一視）はどのエンジンでも正規表現単体では解けない。
JpRegex と同じく**明示的正規化ヘルパー**を新関数として追加する:

```
BE_TextNormalize ( text ; form )      form = NFC / NFD / NFKC / NFKD
BE_TextWidth     ( text ; mode )      mode = full / half   （半角⇔全角、NFC 前処理込み）
BE_TextKana      ( text ; mode )      mode = kata / hira   （ひらがな⇔カタカナ）
```

- 実装は `regex_core.cpp` の `Normalize`/`Width`/`Kana`（ICU Normalizer2 + Transliterator）をそのまま流用
- **既存関数のシグネチャ・挙動には一切触れない**（純粋な追加）ため「API・入出力そのまま」の条件を満たす
- 使い方（FM 計算式側）: `BE_RegularExpression ( BE_TextWidth ( text ; "full" ) ; "アイウ" ; "g" )` のように
  subject を正規化してから照合する。抽出位置が原文とズレる点は JpRegex と同じ設計判断
  （曖昧検索したい人が明示的に選ぶ。既定はリテラル照合で原文正確）
- 案 A の ICU が前提（案 B 単独では実装不可 — Poco に translit は無い）

---

## 4. 推奨ロードマップ

```
Phase 0 (検証・半日) ─→ Phase 1 (案B・半日) ─→ [判断] ─→ Phase 2 (案A・2〜3日) ─→ Phase 3 (案C・1日) ─→ Phase 4 (回帰・1日)
```

### Phase 0: 現状エンジンの実力を実機検証

既存の FMP11 デーモンテスト基盤（fmd11.mjs + submit.sh + BE_HTTP 往復）で以下を評価:

```
BE_RegularExpression ( "日本語abc123" ; "\w+" ; "g" )          → 現状の \w の範囲を確認
BE_RegularExpression ( "日本語abc123" ; "(*UCP)\w+" ; "g" )    → UCP インラインが通るか（SUPPORT_UCP 判定）
BE_RegularExpression ( "ひらがなカタカナ" ; "\p{Hiragana}+" ; "" )  → プロパティ対応判定
BE_RegularExpression ( "ＡＢＣ" ; "ａｂｃ" ; "i" )              → 非 ASCII caseless 判定
BE_RegularExpression ( "アアア" ; "ア*" ; "g" )                 → 空マッチ挙動の現状記録（互換レイヤーの基準値）
```

### Phase 1: 案 B 適用（Phase 0 で SUPPORT_UCP 確認できた場合のみ）

- オプション文字 `u` 新設方式で `(*UCP)` 付与（既存挙動を変えない）
- `docs/Functions/` の該当ドキュメントに `u` オプションを追記
- これだけで J-1/J-2/J-3 が解決するなら、案 A はサイズコストと相談して延期可能

### Phase 2: 案 A（ICU 差し替え）

1. WORK1 の vcpkg で `icu:x86-windows-static` を導入（full。まず確実に動かす）
2. `regex_core.{h,cpp}` → `Source/BERegexCore.{h,cpp}` 移植（jregex → be 名前空間、コメント整合）
3. `BERegularExpression.h` を ICU アダプタ化（§3 案 A の互換レイヤー 7 項目を実装）
4. `tests/test_be_regex.cpp` を JpRegex の `test_regex.cpp` から移植し、**互換性ケースを追加**
   （空マッチ・`$` エスケープ・末尾片・¶ 連結・`v` オプション経路）— FM 非依存で単体実行
5. ビルド → FMP11 実機（デーモンテスト基盤）で BE_RegularExpression / BE_FileReplaceText /
   BE_FileReadText(delimiter) の 3 経路を検証
6. サイズが問題なら `build-trim-icu-win.bat` で trim 版 x86 を試す（旧 PE との相性は要実機確認）

### Phase 3: 案 C（ヘルパー関数 3 つ追加）

- `BEPluginFunctions.{h,cpp}` に 3 関数追加、`BEPluginGlobalDefines.h` に関数 ID、
  `BEPlugin.cpp` に RegisterFunction、Resources の関数定義文字列を追加
- FM11 の関数登録上限・プレフィックス規約は既存パターンに従う

### Phase 4: 回帰テスト

- 既存の実弾検証セット（Cipher/PDF/SQL/ConvertContainer 等）を再実行し副作用が無いことを確認
- 正規表現互換ケース: Phase 0 で記録した現状挙動と比較し、**意図した改善以外の差分ゼロ**を確認
- FMS on Linux を視野に入れるなら、パターンキャッシュのスレッド安全性は JpRegex 設計を踏襲済みなので追加作業なし

---

## 5. リスクと注意事項

| リスク | 対策 |
|---|---|
| .fmx サイズが 3.7MB → 10MB 超（trim）/ 30MB 超（full） | ユーザー判断を仰ぐ。まず案 B で凌げるか Phase 0/1 で見極める |
| FM11 32bit（旧 PE フラグ）+ trim ICU static の相性が未検証 | full（実証済み）で先に動かし、trim は後から差し替え。JpRegex で「旧 PE + modern static ICU」自体は検証クローズ済み |
| 空マッチセマンティクス差で既存 FM ソリューションの結果が変わる | 互換レイヤー必須（§3 案 A-1）。Phase 0 の基準値と突き合わせ |
| CRT 混在（このフォークで過去に BE_FileWriteText 0 バイト問題を起こした根本原因） | ICU は vcpkg /MT（MSVC）でリンク。mingw 系は一切混ぜない。/FORCE:MULTIPLE を復活させない |
| BE_FileReplaceText は巨大ファイル全体を UTF-16 化するためメモリ 2 倍 | 実用上は許容範囲（現行も全体を std::string に読む）。ドキュメントに注記 |
| `i` の意味が「ASCII のみ大小無視」→「Unicode 大小無視」に広がる（案 A） | 改善として仕様化しドキュメント化。厳密互換が必要なら Phase 0 の記録と比較して判断 |

## 6. 判断が必要な点（実装開始前にユーザー確認）

1. **サイズ許容度**: .fmx が 10MB 前後（trim）になっても ICU 化を進めるか、案 B（+`u` オプション、サイズ増ゼロ）で十分か
2. **`(*UCP)`/ICU の適用方式**: 常時適用（挙動改善だが微妙な非互換）か、オプション文字 `u` 新設（完全互換維持）か
3. **案 C のヘルパー関数**: 追加するか（関数追加は「既存 API 不変」の範囲内と解釈できるが、純増ではある）
