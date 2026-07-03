# XML操作系関数の実装入れ替え 分析レポート

> 作成: 2026-07-03（32bit フォーク / FMP11 対象）
> 提案: 「XML操作系の関数は、定番ライブラリを静的リンクし、API・入出力を維持したままコードを入れ替える」
> 関連資料: `ENCODING-AUDIT.md`（M-10〜M-14）、`BUFFER-AUDIT.md`（#9）、メモリ `be-plugin-xml-msvc-static` / `be-plugin-audit-fixes`

---

## 1. 結論（先に要点）

**「定番ライブラリ」= libxml2 / libxslt であり、本プラグインは既にそれを使用し、かつ 2026-07-02 の SESSION 6 で MSVC /MT の完全静的リンク化まで完了している。** したがって「ライブラリの入れ替え」は不要（他候補はすべて機能面で劣る）。この提案の実体価値は次の3点に絞られる。

1. **ラッパーコード（BE 側実装）の書き直し** — 現存バグの大半はライブラリではなく BE のラッパー層にある（グローバル状態汚染・エラー握りつぶし・`xmlCleanupParser()` 誤用）
2. **libxml2 2.9.10 → 2.13系 / libxslt 1.1.34 → 1.1.43 へのバージョン更新** — 2019年リリースのまま。xmlreader の use-after-free（CVE-2024-25062）等、本プラグインが実際に通るコードパスの CVE が複数未修正
3. **入出力互換性の意識的な維持** — バージョン更新・書き直しで「エラーメッセージ文字列」と「整形出力の空白」は変わり得る。関数の正常系出力は維持可能

推奨は **案A（libxml2/libxslt 維持 + ラッパー全面書き直し + 段階的バージョン更新）**。

---

## 2. 現状アーキテクチャ

### 2.1 対象関数（9関数）

| 関数 | 実装 | 使用ライブラリ |
|---|---|---|
| `BE_XPath` / `BE_XPathAll` | BEXSLT.cpp `ApplyXPathExpression` | libxml2 (XPath 1.0) |
| `BE_XSLT_Apply` / `BE_XSLT_ApplyInMemory` | BEXSLT.cpp `ApplyXSLTInMemory` | libxslt 1.0 + libexslt |
| `BE_XMLParse` | BEXMLTextReader (xmlreader) | libxml2 |
| `BE_XMLStripNodes` | BEXMLReader.cpp + TextReader/TextWriter | libxml2 (xmlreader/xmlwriter) |
| `BE_SplitBEFileNodes` | BEXMLReader.cpp + BEFileTextReader | libxml2 |
| `BE_XMLValidate` | BEXMLSchema.cpp `validate_xml` | libxml2 (XML Schema) |
| `BE_XMLCanonical` | BEXMLSchema.cpp `canonical_xml` | libxml2 (C14N 1.1) |
| `BE_XMLTidy` | BEXMLSchema.cpp `pretty_print_xml` | **Poco::XML::DOM**（唯一の例外） |
| `BE_XMLStripInvalidCharacters` | BEPluginFunctions.cpp 直書き | ライブラリ不使用（生バイト処理） |

### 2.2 リンク形態（SESSION 6 で確立済み）

- **libxml2 2.9.10 / libxslt 1.1.34 / libexslt / win_iconv 0.0.10** — MSVC /MT **静的リンク**（プラグイン本体、libcmt ヒープに統一）
- Poco XML（BE_XMLTidy 用）— Poco 本体と共に静的リンク
- belibs.dll（mingw 系）からは XML スタックを完全に引き剥がし済み。アロケータ境界クラッシュ（BE_XPath 即死）はこれで**根治済み・FMP11 実機実証済み**

つまり「定番ライブラリの静的リンク」という提案の前提条件は**既に達成されている**。

---

## 3. 現状の不具合

### 3.1 修正済み（実機検証済み or 作業ツリー）

| ID | 内容 | 状態 |
|---|---|---|
| — | BE_XPath ヒープ破壊（xmlFree すべき文字列を delete） | 修正済み `5c9b150e` |
| — | アロケータ境界クラッシュ（mingw DLL ⇔ MSVC 本体） | MSVC 静的化で根治（SESSION 6） |
| M-13 | BEXMLSchema に `XML_PARSE_IGNORE_ENC` 欠落（Shift_JIS 宣言 XML の誤再解釈） | 修正済み `02df26ef` |
| M-10 | XML ファイルを `_O_WTEXT` で開いて二重変換 | **修正済み・未コミット**（作業ツリー） |
| M-11 | `isspace` に負値を渡す UB + 全空白入力で `*end()` 参照 | **修正済み・未コミット** |
| M-12 | 空要素 `<a/>` で `ReadInnerXml` の NULL 戻り → クラッシュ | **修正済み・未コミット** |

### 3.2 未修正の不具合

#### (a) `xmlCleanupParser()` の呼び過ぎ【潜在クラッシュ・重要度高】

- `BEXMLTextReader` の**デストラクタ**（BEXMLTextReader.cpp:103）、`validate_xml`（BEXMLSchema.cpp:112）、`canonical_xml`（同:151）が毎回 `xmlCleanupParser()` を呼ぶ。
- libxml2 の公式ドキュメントは「**プロセス終了直前に1回だけ**。ライブラリ使用中に呼ぶと未定義動作」と明記。グローバルの encoding handler・dict 等が破棄されるため、別スレッド/後続呼び出しと競合すると解放済みメモリ参照になる。
- FMP11 は概ね単一スレッド評価だが、FileMaker Server / 並行評価では実クラッシュ要因。`BEPlugin.cpp` の `CleanupLibXSLT()`（シャットダウン時）に一本化すべき。

#### (b) グローバル状態の毎回書き換え【スレッド安全性 + XXE】

- `validate_xml` / `canonical_xml` が呼び出しごとに `xmlSubstituteEntitiesDefault(1)` / `xmlLoadExtDtdDefaultValue` / `xmlLineNumbersDefault(1)` を再設定。`canonical_xml` は `xmlLoadExtDtdDefaultValue = XML_DETECT_IDS|XML_COMPLETE_ATTRS` を設定したまま放置（他関数の挙動に波及）。
- `xmlSetGenericErrorFunc` はプロセスグローバル。XSLT 実行と Validate が交錯するとエラーが他方の thread_local バッファに混入する。
- **XXE**: エンティティ置換 + 外部 DTD ロードがグローバルに有効なため、悪意ある XML（`<!ENTITY x SYSTEM "file:///...">`）でローカルファイル内容が変換結果へ混入し得る。`document()` 関数等 XSLT の正当用途とのトレードオフだが、少なくとも XPath/Validate 系では `XML_PARSE_NONET` を付け、置換はパースオプションで明示制御（`XML_PARSE_NOENT` を付けない）に寄せるべき。
- `XML_PARSE_HUGE` は展開制限を**解除する**フラグなので、billion-laughs 型 DoS への耐性も下がっている（巨大 XML サポートとのトレードオフとして明文化が必要）。

#### (c) `xmlGetLastError()` による成否判定【エラー誤検出・結果破棄】

- `ApplyXSLTInMemory`（BEXSLT.cpp:196-197）、`XPathObjectAsXML`（同:367）、`inner_xml`/`outer_xml`（BEXMLTextReader.cpp:297,320）が「最後のエラーが残っているか」で成否を判定。
- libxml2 のエラーは**スレッドグローバルに蓄積**されるため、前の呼び出しの stale エラーや、リカバリ済みの警告で**成功した変換結果を捨てて誤ったエラーを返す**ことがある。判定は各 API の戻り値 + コンテキスト毎の structured error handler で行うべき。

#### (d) XSLT エラー処理の握りつぶし

- `ApplyXSLTInMemory` 末尾（BEXSLT.cpp:241-244）: `g_last_xslt_error != kNoError` で `throw exception()`（生の std::exception）→ 呼び出し側で `kErrorUnknown` に丸められ、**収集したエラーテキストが利用者に届かない**。
- BUFFER-AUDIT #9: `XSLTErrorFunction` / `XSDErrorFunction`（BEXSLT.cpp:69 / BEXMLSchema.cpp:38 の同型重複コード）が **10KB 固定バッファ**で、超過時は本文喪失 + `kLowMemoryError` と誤報告。`xmlStrVPrintf` はメモリ不足ではなく「バッファに収まらない」だけでも -1 を返す。

#### (e) M-14: `<xsl:output encoding="...">` 非 UTF-8 指定

- `xsltSaveResultTo` は stylesheet の出力エンコーディング指定を尊重するため、Shift_JIS 等を指定されると**非 UTF-8 バイト列がそのまま FileMaker へ**渡り、化けまたは `kInvalidUTF8` になる。UTF-8 へ強制するか、明示エラー（10101 系）にするかの設計判断が必要。

#### (f) `BE_XMLStripInvalidCharacters` の脆さ

- **UTF-16 固定・2バイト単位処理**の前提で、BOM が `FF FE`（LE）以外はすべて BE 扱い。**UTF-8 ファイルに使うと全損レベルの誤処理**（関数名からは分からない）。奇数長ファイルの末尾バイト、`i = -size` による unsigned ラップアラウンドのリセットハックなど、境界がすべて暗黙。FM 旧世代の UTF-16 エクスポート専用ツールであることをドキュメント化するか、エンコーディング検出を実装すべき。

#### (g) その他（小）

- `BEXMLTextReader::content()`（:340-341）: `xmlNodeGetContent` の NULL 戻りを `std::string(NULL, 0)` に渡す経路（`xmlStrlen(NULL)`=0 なので実害は出にくいが規格上グレー）。
- `BE_XMLStripNodes` の属性処理: `move_to_attribute` で番号順に回りながら `get_attribute(名前)` で引き直すため、**同名属性（名前空間違い）は最初の値に統一**される。
- `SplitBEXMLFiles`: 書き込み失敗を `g_last_error` に入れて**処理は続行**（部分成功が無言で起きる）。

### 3.3 ライブラリ自体の既知脆弱性（2.9.10 / 1.1.34 のまま静的リンクしている問題)

静的リンクの宿命として、**CVE 対応 = 再ビルド**。現行バージョンは 2019 年のもので、以後に修正された主な CVE のうち本プラグインのコードパスに関係するもの:

| CVE | 内容 | 該当パス |
|---|---|---|
| CVE-2022-23308 | ID/IDREF 属性の use-after-free | パーサ全般 |
| CVE-2022-29824 | xmlBuf 系の整数オーバーフロー | 出力バッファ（XSLT 結果等） |
| CVE-2022-40303/40304 | `XML_PARSE_HUGE` 時のパーサ整数オーバーフロー | **全関数**（HUGE を常用） |
| CVE-2023-28484 | XML Schema の NULL 参照 | `BE_XMLValidate` |
| CVE-2024-25062 | **xmlreader の use-after-free** | `BE_XMLParse` / `BE_XMLStripNodes` |
| CVE-2021-30560 (libxslt) | keys 処理の use-after-free | `BE_XSLT_Apply` |
| CVE-2024-55549 / CVE-2025-24855 (libxslt) | 名前空間/番号処理の use-after-free | `BE_XSLT_Apply` |

個人利用の 32bit フォークで、入力 XML が自己管理データなら実害確率は低いが、**外部由来 XML（HTTP 応答・受領ファイル）を BE_XPath/BE_XSLT に食わせるワークフローがあるなら更新価値は高い**。

---

## 4. 機能の不足（現状 API の限界）

| 領域 | 現状 | 不足 |
|---|---|---|
| XPath | 1.0（+ EXSLT 拡張） | XPath 2.0/3.1 の正規表現・シーケンス・`ends-with` 等は不可（libxml2 系の上限。C/C++ 定番で 2.0+ は事実上 Saxon-C しかない） |
| XSLT | 1.0（+ EXSLT） | XSLT 2.0/3.0 不可（同上） |
| 検証 | XML Schema (XSD) のみ | DTD / RelaxNG / Schematron は libxml2 が対応しているのに未公開 |
| HTML | なし | libxml2 の `htmlReadDoc`（寛容な HTML パーサ）を使えば「HTML に XPath」が実装可能だが未公開 |
| 変換 | XML→XML のみ | XML⇄JSON 変換関数なし |
| 整形 | `BE_XMLTidy`（Poco、挙動が他と別系統） | libxml2 側での整形/最小化（minify）なし。Tidy の「CANONICAL_XML」は W3C C14N ではなく Poco 独自 |
| 値リスト | `BE_XPathAll` は FileMaker 値リストで返す | **値に改行が含まれると区切りと区別不能**（仕様的欠陥、回避手段なし） |
| エラー | テキスト連結のみ | 行番号・カラム・エラーコードの構造化返却なし（JSON で返す選択肢） |
| 名前空間 | `prefix=uri prefix2=uri2` の空白区切り | **デフォルト名前空間（無接頭辞）が指定不能**（XPath 1.0 の仕様制約。ダミー接頭辞の案内が要る） |

---

## 5. 「入れ替え」候補ライブラリの比較

| 候補 | XPath | XSLT | XSD検証 | C14N | ストリーミング | 静的リンク | 評価 |
|---|---|---|---|---|---|---|---|
| **A. libxml2/libxslt（現行）** | 1.0 | 1.0 | ○ | ○ (1.1) | ○ (xmlreader) | **済み** | ◎ 唯一の全機能カバー |
| B. pugixml | 1.0 | × | × | × | ×（DOM のみ） | 容易 | △ XSLT/XSD/C14N が全滅。併用なら2パーサ体制になり本末転倒 |
| C. Poco::XML（統一） | × | × | × | × | ○ (SAX) | 済み | ✕ XPath すらない |
| D. MSXML6 | 1.0 | 1.0 | ○ | × | ○ | ×（OS の COM） | △ Windows 専用・COM 初期化が FMX スレッドモデルと干渉リスク・上流(mac/Linux)と乖離 |
| E. Xerces-C++/Xalan-C | 1.0 | 1.0 | ○ | ○ | ○ | 可（巨大） | ✕ Xalan-C は事実上開発停止。バイナリ肥大 |
| F. Saxon-C (SaxonC-HE) | **3.1** | **3.0** | ○(EE) | ○ | △ | ×（巨大ランタイム同梱） | △ 機能は唯一の上位互換だが、32bit Windows 対応なし・サイズ・ライセンス |

**判定: 入れ替え先は存在しない。** C/C++ 圏で XPath + XSLT + XSD + C14N + ストリーミングを1系統で賄える「定番」は libxml2/libxslt だけ（xsltproc・Python lxml・PHP・WebKit と同じスタック）。XPath/XSLT 2.0+ が要件にならない限り、これを外す理由がない。32bit 制約（Saxon-C は x86 なし）も決定的。

---

## 6. 推奨案: 案A「スタック維持・ラッパー全面書き直し・段階的更新」

### Phase 1 — ラッパー再設計（ライブラリ据え置き、API・入出力完全維持）

3.2 の不具合を設計レベルで潰す。関数シグネチャ・戻り値仕様は一切変えない。

1. **ライフサイクル一本化**: `xmlInitParser`/`xsltInit`/EXSLT 登録は `InitialiseLibXSLT()`（起動時1回）のみ。`xmlCleanupParser()` は `CleanupLibXSLT()`（シャットダウン）のみ。各関数・デストラクタからの呼び出しを全撤去。
2. **グローバル排除**: `xmlSubstituteEntitiesDefault`/`xmlLoadExtDtdDefaultValue` への依存をやめ、パースオプション（`XML_PARSE_NOENT` を関数ごとに明示、XSLT のみ DTD ロード許可）で制御。エラーは `xmlSetStructuredErrorFunc`（コンテキスト付き）または parser-ctxt 単位のハンドラで**呼び出しローカルに**収集。
3. **エラー収集の再設計**: 10KB 固定バッファ2重実装 → `std::string` に追記する共通クラス1つ（`BEXMLErrorCollector`）。成否判定は**各 API の戻り値**で行い、`xmlGetLastError()` の残留値には依存しない。`throw exception()` → エラーテキスト付き `BEPlugin_Exception` へ。
4. **RAII 徹底**: `xmlDocPtr`/`xmlXPathObjectPtr`/`xsltStylesheetPtr` 等を `unique_ptr` + カスタムデリータで統一（現状は手動 free の混在で、早期 return 経路のリーク余地がある）。
5. **セキュリティ既定値**: XPath/Validate/Canonical/Parse 系は `XML_PARSE_NONET` を追加（ネットワーク経由の外部実体取得を遮断）。XSLT のみ従来互換（`document()` 用）とし、挙動差は関数リファレンスに明記。
6. **M-14 対応**: XSLT 出力エンコーディングが UTF-8 以外なら ConvertTextEncoding（Batch 3 で整備済みの 10101 エラー系）を通して UTF-8 化、変換不能なら明示エラー。

**検証**: 既存のデーモンテスト基盤（`be-plugin-daemon-test`）で SESSION 6 の実証項目（XPath 日本語・XPathAll・XSLT・Validate）+ 今回の追加ケース（全空白入力・`<a/>`・不正 XML のエラーテキスト・Shift_JIS 宣言・巨大エンティティ）を FMP11 実機で回す。

### Phase 2 — libxml2 2.13.x / libxslt 1.1.43 への更新（任意・推奨）

- ビルド: 2.10 以降は `win32/configure.js` が縮退→廃止のため **CMake + MSVC x86 /MT** に移行（`cmake -A Win32 -DBUILD_SHARED_LIBS=OFF -DLIBXML2_WITH_ICONV=ON ...`）。SESSION 6 の nmake 手順は使えなくなる点に注意。
- コード側の主な追随: `xmlGetLastError()` が `const xmlError*` を返す（**BEXSLT.cpp:478-489 に対応コード実装済み**、他の `xmlErrorPtr` 受け・`xmlResetError` 呼びの箇所は要修正）。deprecated になったグローバル設定 API は Phase 1 の書き直しで既に排除されているはず。
- 効果: 3.3 の CVE 全部 + パーサの品質向上。副作用: **エラーメッセージの文言・書式が変わる**（後述の互換性リスク）。

### Phase 3 — 機能追加（提案スコープ外、候補のみ）

RelaxNG 検証・HTML パース（`BE_HTMLXPath` 相当）・XML⇄JSON・構造化エラー返却。いずれも libxml2 の既存機能の公開なので増分コストは小さい。

---

## 7. メリット / デメリット

### メリット

| # | 内容 |
|---|---|
| 1 | **クラッシュ残余リスクの根絶**: `xmlCleanupParser` 誤用・stale エラー判定・手動 free 漏れという「たまに死ぬ」系を設計で排除。FMP11 は例外・メモリ破壊に極端に弱い（実証済み）ため価値が大きい |
| 2 | **エラーの可視化**: 現在 `kErrorUnknown` に丸められる XSLT エラーが本文付きで FileMaker に届くようになる（デバッグ効率が段違い） |
| 3 | **セキュリティ**: XXE 遮断（NONET）+ CVE 解消（Phase 2）。外部由来 XML を扱うワークフローの前提が立つ |
| 4 | **API 完全互換**: 関数名・引数・正常系出力を維持するため、既存 FileMaker ソリューション側の変更ゼロ |
| 5 | **保守性**: エラー処理1系統・RAII 統一で、以後の監査(Batch 4 以降)・上流 PR 化が容易になる |
| 6 | **実証基盤が既にある**: デーモンテストで回帰を機械的に検証でき、書き直しのリスクを低コストで抑えられる |

### デメリット / リスク

| # | 内容 | 緩和策 |
|---|---|---|
| 1 | **エラーメッセージ文字列の互換性が壊れる**（Phase 2 で特に。文言・行番号書式が変わる） | エラー文字列をパースしている FM スクリプトがないか確認。「エラーの有無」判定のみなら影響なし |
| 2 | 書き直しによる**新規バグ混入**リスク | Phase 1 は関数単位で段階コミット + 実機回帰。SESSION 6/7 の検証項目を再走 |
| 3 | Phase 2 の**ビルド体系変更**（configure.js → CMake）で lib32-work の手順資産が一部無効化 | 手順をメモリ/HANDOVER に再文書化。Phase 1 だけでも成立する構成にする |
| 4 | `XML_PARSE_NOENT` 排除・NONET 追加で、**エンティティ置換に依存していた既存 XML の挙動が変わる**可能性 | XSLT 系は従来挙動を維持。XPath/Validate で実体参照込み XML を使う場合のみ影響 — リリースノートに明記 |
| 5 | BE_XMLTidy を libxml2 に寄せる場合、**整形出力（インデント・属性順）が変わる** | 出力差が許容できなければ Poco のまま据え置く（Tidy は入れ替え対象から除外可能） |
| 6 | 工数: Phase 1 ≒ 1〜2 セッション、Phase 2 ≒ 1 セッション（CMake ビルド + 追随修正 + 回帰） | 他の Batch 4 監査修正と同時に進めず、XML 系単独でコミットを分ける |

---

## 8. 入出力互換性の維持について（提案の「API、入出力を維持」への回答)

- **正常系**: 全9関数とも維持可能。XPath のキャスト規則・XSLT 変換結果・C14N 出力・検証の「valid=空文字」仕様はライブラリ仕様そのものなので、同一ライブラリ継続なら変化しない。
- **異常系（エラー文字列)**: 現状でも `ReportXSLTError` の書式は xsltproc 風の生テキストであり、**むしろ現在の方が「同じ入力でも stale エラーの混入で結果が揺れる」**。書き直し後は決定的になる＝「互換」ではなく「改善」だが、文言自体は変わる。
- **BE_XMLTidy のみ**、実装（Poco）を変えると出力書式が確実に変わるため、入れ替え対象から外すことを既定とする。

---

## 9. 推奨実行順序

1. 未コミットの M-10/M-11/M-12 修正を先にコミット（現作業ツリーに混在中）
2. Phase 1 を関数グループ単位で実施: ①エラー収集共通化 + ライフサイクル一本化 → ②XSLT/XPath 系 → ③Reader/Writer 系 → ④Schema/C14N 系。各段でデーモンテスト回帰
3. Phase 2（バージョン更新）は Phase 1 安定後に単独セッションで。CMake ビルド手順を確立してからリンク差し替え
4. `BE_XMLStripInvalidCharacters` は「UTF-16 専用」の明文化のみ先行し、作り直しは Phase 3 送り
