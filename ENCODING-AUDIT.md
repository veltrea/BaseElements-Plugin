# 文字列操作の型・エンコード監査（いい加減な箇所の一覧）

> **対応状況 (2026-07-03):** 修正済み = H-4, H-5, H-6, H-7, H-8, H-9, M-1, M-2, M-13, M-16（commit `02df26ef` + `62675de2`、FMP11実機で日本語パス・日本語データの実弾検証済み）。
> 未対応（設計判断待ち）= H-1, H-2, H-3, H-10, H-11, M-3〜M-12, M-14以降と L 群。詳細はメモリ `be-plugin-audit-fixes` 参照。

対象: `BaseElements-Plugin/Source`（duktape 等のサードパーティ同梱コードは除外）
調査日: 2026-07-02
観点: **文字列の「型」と「エンコード」の扱いがいい加減な箇所** — UTF-8 / CP932(ANSI) / UTF-16 / UTF-32 の境界を素通しする、バイト数と文字数(コード単位数)を混同する、NUL 終端を仮定して埋め込み NUL でデータを失う、変換失敗を黙殺する、ロケール依存 API に UTF-8 を直渡しして UB を踏む、といったパターン。
姉妹編: メモリ確保の監査は [BUFFER-AUDIT.md](BUFFER-AUDIT.md)（重複する指摘は相互参照で示す）。

---

## 根本原因パターン（全体を貫く 7 つの型）

| 記号 | パターン | 典型箇所 |
|------|----------|----------|
| A | **ANSI(CP932)⇔UTF-8 境界の素通し** — Windows で UTF-8 バイト列を narrow API / narrow path に渡す（逆も） | クリップボード、boost::filesystem::path 直構築、`.string()` 劣化 |
| B | **バイト数 vs コード単位数の混同** — UTF-8 のバイト数を UTF-16 の添字に使う等 | Sub_LoadString (Linux)、BESQLCommand セパレータ |
| C | **NUL 終端仮定** — `string(char*)` / `assign(char*)` / `c_str()` で長さ情報を捨てる → 埋め込み NUL で無通知切断、非終端バッファで領域外リード | SetResult 系、ReadFileAsUTF8 |
| D | **エンコード推測ガチャの黙殺** — 変換元を UTF-8→UTF-16 の順に試し、失敗しても「偶然通った解釈」や空文字列を成功として返す | ConvertTextEncoding、ReadFileAsUTF8、BE_FileWriteText |
| E | **サロゲートペア／非 BMP 無視** — UTF-16 コード単位を 1:1 で UTF-32 に拡張する | ParameterAsWideString (mac/Linux) |
| F | **ロケール依存 API への UTF-8 直渡し** — 符号付き char を `::tolower`/`isspace` に渡す UB、`mbstowcs`/`std::locale()` 依存 | BEValueList::to_lower、BEXMLTextReader |
| G | **fmx::Text::Assign のデフォルト引数事故** — `kEncoding_Native`（Win では ANSI）のまま UTF-8 を渡す | BESQLCommand、BEJavaScript |
| H | **型幅・符号の無頓着** — size_t↔int/long/uint32 の黙殺切り詰め（32bit ビルド直撃）、bool と double の取り違え | easy_setopt の va_arg、file_size、ParameterAsDouble |

---

## サマリ（全件）

| # | 深刻度 | ファイル:行 | 関数 | パターン | 問題 |
|---|--------|-------------|------|----------|------|
| H-1 | ★★★ | BEPluginUtilities.cpp:771 | `ConvertTextEncoding` | D | CP932 を黙って UTF-16 と誤認、BOM なし UTF-16LE は逆エンディアン |
| H-2 | ★★★ | BEPluginUtilities.cpp:151 | `SetResult(vector<char>&)` | C | サイズ引数がコメントアウト → 埋め込み NUL で無通知切断 |
| H-3 | ★★★ | BEPluginUtilities.cpp:354 | `ParameterAsWideString` | E | サロゲートペア非合成 → 非 BMP 文字でパス破壊 (mac/iOS/Linux) |
| H-4 | ★★★ | BEPluginFunctions.cpp:853 ほか | `BE_FilePatternCount` 等 | A | UTF-8 string → path 直構築（Win で CP932 解釈） |
| H-5 | ★★★ | BEPluginFunctions.cpp:809 ほか | `BE_FileImport` 等 | A | `path.string()` で ANSI バイト列を UTF-8 として FM へ |
| H-6 | ★★★ | win/BEWinFunctions.cpp:379/484 | クリップボード読み書き | A+C | CF_TEXT の CP932⇔UTF-8 素通し + ヒープ 4 バイト書き越し |
| H-7 | ★★★ | BEValueList.h:448 | `to_lower` | F | 符号付き char を `::tolower` に直渡し（UB）+ UTF-8 バイト単位処理 |
| H-8 | ★★★ | Net/BESMTPEmailMessage.cpp:145 | `add_attachments` | — | RFC 2047 エンコード済み文字列をファイルパスとして open |
| H-9 | ★★★ | BESQLCommand.cpp:34 / BEJavaScript.cpp:104 | コンストラクタ / JS→FM 計算 | G | `Assign` エンコード未指定 → 日本語 SQL/式が Win で化ける |
| H-10 | ★★★ | BEPluginFunctions.cpp:604 | `BE_FileWriteText` | D | 変換全滅時に 0 バイトを error=0 で書く（trunc なら既存内容消失） |
| H-11 | ★★★ | apple/BEIOSFunctions.mm:48 | `ClipboardText` (iOS) | C | nil チェック欠落 → `std::string(NULL)` の UB |
| M-1 | ★★☆ | win/BEWinFunctions.cpp:73 | `InitialiseForPlatform` | — | FileNameMapW 登録がコピペミスで別形式 ID → UTF-16 が narrow 解釈 |
| M-2 | ★★☆ | win/BEWinFunctions.cpp:62 | (グローバル) | — | 形式 ID が thread_local → スレッド次第でエンコード判定崩壊 |
| M-3 | ★★☆ | BEPluginUtilities.cpp:753 | `ReadFileAsUTF8` | C+D | 非終端バッファへの `assign` で領域外リード + UTF-16 誤認 |
| M-4 | ★★☆ | BEPluginUtilities.cpp:830 | `ConvertTextEncoding(string&)` | H | `size()-1` で末尾 1 バイト欠落、空文字列で SIZE_MAX（現状未使用の地雷） |
| M-5 | ★★☆ | BERegularExpression.h / BEPluginFunctions.cpp:4693 | `BE_RegularExpression` 等 | B | PCRE/boost::regex がバイトモード（RE_UTF8 未指定） |
| M-6 | ★★☆ | Net/BECurl.cpp:121-156 | `ReadMemoryCallback`/`SeekFunction` | — | `origin` を毎回上書き → 認証付き PUT(>64KB) 再送で破損データ送信 |
| M-7 | ★★☆ | Net/BECurl.cpp:796 | `easy_setopt` | H | 全オプションを `void*` で varargs 中継 → 32bit で `curl_off_t` がずれる |
| M-8 | ★★☆ | Net/BESMTPEmailMessage.cpp:42,59,121 | From/Reply-To/カスタムヘッダ | — | RFC 2047 未適用の生 UTF-8 をヘッダへ（Subject だけ対応済み） |
| M-9 | ★★☆ | Net/BESMTPContainerAttachments.cpp:41 | 一時添付ファイル | A | UTF-8 文字列連結で narrow path 構築（Win で ACP 解釈） |
| M-10 | ★★☆ | BEXMLTextReader.cpp:66 | コンストラクタ | — | XML を `_O_WTEXT` で開いて libxml2 へ（バイト列が二重変換） |
| M-11 | ★★☆ | BEXMLTextReader.cpp:43 | (先頭判定) | F | 負の char を `isspace` に渡す UB + 全空白入力で `*end()` |
| M-12 | ★★☆ | BEXMLTextReader.cpp:295,316 | `ReadInnerXml`/`ReadOuterXml` | C | NULL 戻りを `std::string` に代入 → クラッシュ |
| M-13 | ★★☆ | BEXMLSchema.cpp:76,93,129 | `BE_XMLValidate` 等 | D | `XML_PARSE_IGNORE_ENC` 欠落（他ファイルは付いている） |
| M-14 | ★★☆ | BEXSLT.cpp:216 | `ApplyXSLTInMemory` | A | `<xsl:output encoding≠UTF-8>` のバイト列をそのまま UTF-8 経路へ |
| M-15 | ★★☆ | BEJavaScript.cpp:66 | `Evaluate_JavaScript` | E | Duktape の CESU-8 出力（非 BMP 文字）が不正 UTF-8 として流出 |
| M-16 | ★★☆ | Net/BECurlOption.cpp:139,198 | 型テーブル | H | HTTPGET を string 型/TLSAUTH_TYPE を long 型と誤登録 → クラッシュ |
| M-17 | ★★☆ | Net/BECurl.cpp:238 | `trace_callback` | C | NUL 終端非保証の `char*` をストリームへ（VERBOSE 常時 ON） |
| M-18 | ★★☆ | linux/BELinuxFunctions.cpp:226 | `Sub_LoadString` | B | UTF-8 バイト数を UTF-16 単位の添字に使用（[BUFFER-AUDIT #3](BUFFER-AUDIT.md) の別側面） |
| M-19 | ★★☆ | linux/BELinuxFunctions.cpp:178 | `get_machine_name` | F | `mbstowcs` のロケール依存 + int/size_t 符号混同 |
| M-20 | ★★☆ | win/BEWinFunctions.cpp:1009 | `Sub_LoadString` | H | 置換で伸びた文字列を resultsize 無視で書き戻し + `find` npos 例外（[BUFFER-AUDIT #1](BUFFER-AUDIT.md)） |
| M-21 | ★★☆ | apple/BEMacFunctions.mm:85 | `ClipboardText` | C | `cStringUsingEncoding:` の NULL 戻り未処理 |
| M-22 | ★★☆ | apple/BEAppleFunctionsCommon.mm:57 | `WStringFromNSString` | D | `dataUsingEncoding:` の nil を握り潰し → 失敗が静かな空文字に |
| M-23 | ★★☆ | BEValueList.h:151 | 区切りコンストラクタ | B | `is_any_of` がマルチバイト区切りをバイト分解 → UTF-8 破壊 |
| M-24 | ★★☆ | BEValueList.h:504 | `trim_values` | F | グローバルロケール依存 trim で UTF-8 末尾バイト削りの可能性 |
| M-25 | ★★☆ | BEPluginUtilities.cpp:710,738 | `ReadFileAsBinary/UTF8` | H | `uintmax_t→size_t` で 32bit ビルド 4GB 超切り詰め |
| M-26 | ★★☆ | BEPluginUtilities.cpp:853 | `IsValidUTF8` | — | 末尾切断 UTF-8 (EINVAL) が kInvalidUTF8 でなく errno 生値例外に |
| M-27 | ★★☆ | BEPluginUtilities.h:71 | `ParameterAsDouble` | H | デフォルト値引数の型が `bool` → 0/1 以外は 1.0 に潰れる |
| M-28 | ★★☆ | BEPluginFunctions.cpp:3279 | `BE_BackgroundTaskAdd` | C | バイナリレスポンスを文字列化して SQL へ無エスケープ埋め込み |
| M-29 | ★★☆ | BEPluginFunctions.cpp:4072 | `BE_ScriptStepPerform` | C | コンテナをテキスト化して計算式へ無エスケープ連結（式インジェクション） |
| M-30 | ★★☆ | BEPluginFunctions.cpp:499,525 | `BE_FileReadText` | H | 負の from が uint32 キャスト後に判定され巨大開始位置に |
| L-1〜L-20 | ★☆☆ | 各所 | — | — | 下記「低」の表を参照 |

---

## 高（現実的入力でデータ破壊・UB・文字化けが発生）

### H-1. `ConvertTextEncoding` — エンコード推測フォールバックが CP932 を黙って UTF-16 として解釈
**場所:** `Source/BEPluginUtilities.cpp:771-824`

```cpp
vector<string> codesets;
if ( from != UTF8 ) codesets.push_back ( from );
codesets.push_back ( UTF8 );
codesets.push_back ( UTF16 ); // backwards compatibility with v1.2
...
while ( error_result == EILSEQ && it != codesets.end() ) { ... }
```

変換元エンコードを「順に iconv で試して EILSEQ でなければ採用」する設計。最後の砦が `"UTF-16"` で、**偶数長のバイト列はほぼ何でも UTF-16 として「変換成功」してしまう**（孤立サロゲートを構成しない限り EILSEQ にならない）。

- `BE_SetTextEncoding` 未設定で CP932 の日本語テキストを `BE_FileReadText` すると、UTF-8 判定は正しく落ちるが UTF-16 解釈が「成功」し、**エラーではなく漢字混じりの完全な文字化けが黙って返る**。「Shift_JIS のファイルを読んだら中国語みたいになる」の典型原因。
- `#define UTF16 "UTF-16"` はエンディアン無指定。iconv は BOM なしをビッグエンディアン既定で解釈するため、Windows 由来の **BOM なし UTF-16LE ファイルも逆エンディアンで化ける**。BOM 検出も `UTF-16LE`/`UTF-16BE` の明示もない。

**影響範囲:** `ReadFileAsUTF8` → `BE_FileReadText` / `BE_FileReplaceText` / `BE_XSLT_Apply` / `BE_FilePatternCount`、および HTTP レスポンスのテキスト化経路（H-2 参照）。

### H-2. `SetResult ( vector<char>& )` — サイズ引数を意図的に外した埋め込み NUL 切断
**場所:** `Source/BEPluginUtilities.cpp:151-156`

```cpp
void SetResult ( vector<char>& data, Data& results )
{
    data.push_back ( '\0' );
    const std::string data_string ( data.data() );//, data.size() );
    SetResult ( data_string, results );
}
```

`std::string(const char*)` は最初の NUL で停止する。コメントアウトされた `//, data.size()` が「サイズ指定版から意図的に退化させた」ことを物語る。0x00 は正当なバイナリにも U+0000 として正当な UTF-8 にも現れるので、**NUL 以降がエラーなしで消失**する。非 const 参照で受けて `push_back` するため呼び出し元バッファも変異させる。

**影響範囲（バイナリレスポンスをテキストとして返す全経路）:**
- `BE_HTTP_GET`（filename 省略時）、`BE_HTTP_POST_PUT_PATCH`（**PATCH/PUTData/PUTFile は常にこの経路**）、`BE_HTTP_DELETE`、`BE_FTP_Upload`、`BE_FTP_Delete`
- `BE_DecryptAES`（復号結果に 0x00 が含まれれば切断）
- さらに `BECurl` は `CURLINFO_CONTENT_TYPE` を取得しているのに **charset を一切参照せず**、非 UTF-8 レスポンスは変換ガチャ（H-1）行き。UTF-16 誤認 / 無言の空文字列 / `kInvalidUTF8` 例外のいずれかになる。

同型の問題が `SetResult ( const std::string& )`（:126-139）にもある — `IsValidUTF8` 通過後に `Assign ( text.c_str(), ... )` するため、U+0000 を含む正当な UTF-8 も切れる。

### H-3. `ParameterAsWideString` — サロゲートペア非合成の UTF-16→UTF-32 キャスト拡張（mac/iOS/Linux）
**場所:** `Source/BEPluginUtilities.cpp:347-370`

```cpp
// wchar_t is 4 bytes on OS X and 2 on Windows
#if defined FMX_MAC_TARGET || defined FMX_IOS_TARGET || defined FMX_LINUX_TARGET
    wchar_t * parameter = new wchar_t [ text_size + 1 ];
    for ( long i = 0 ; i <= text_size ; i++ ) {
        parameter[i] = (wchar_t)text[i];   // UTF-16 コード単位を 1:1 で UTF-32 へ
    }
```

wchar_t のサイズ差は認識している（コメントあり）のに、変換は**コード単位の単純キャスト**でサロゲートペアを合成しない。非 BMP 文字（絵文字 U+1F600、「𠮷」U+20BB7 など日本人の氏名で現実的な JIS X 0213 非 BMP 漢字）は孤立サロゲート 2 個の不正 UTF-32 になる。

**影響範囲:** この関数は `ParameterAsPath`（:459-467）の下請けで、**ファイルパス取得の唯一の経路（BEPluginFunctions.cpp 内 52 箇所）**。mac/Linux で非 BMP 文字入りパスのファイル操作が全滅する。`BE_FileSelect`/`BE_DialogDisplay`/`BE_OpenURL` 等のワイド文字列系にも波及。付随して `result.assign ( parameter )` の NUL 終端仮定（U+0000 で切れる）、例外時の `text`/`parameter` リークもある。Windows は wchar_t=UTF-16 なので無害＝**プラットフォーム差バグ**。

### H-4. UTF-8 の std::string から `boost::filesystem::path` を直接構築（Windows で日本語パス崩壊）
**場所:** `Source/BEPluginFunctions.cpp:853` ほか

リポジトリ内に `boost::filesystem::path::imbue` / UTF-8 codecvt の設定は一切ない。したがって Windows では `std::string→path` の暗黙変換は **ANSI コードページ（CP932）経由**になる。`ParameterAsPath` はワイド経由で正しいのに、「値リストで複数パスを受ける」系だけがこの経路をバイパスしている:

```cpp
auto file_list = ParameterAsStringValueList ( parameters );  // UTF-8
boost::filesystem::path text_file_path ( *it );              // UTF-8 → CP932 解釈
```

- `BE_FilePatternCount` :853
- `BE_Zip` :2101（値リスト分岐）/ `BE_PDFAppend` :3826（同）
- `BE_FileMakerSQL` :4487-4489（出力ファイルパスが `write_to_file(const path&)` へ暗黙変換）
- `BE_Unzip`/`BE_Zip` :2039,2049,2083 — UTF-8 string のまま BEZlib へ渡り、`std::ofstream ( path.toString().c_str() )` の narrow API で open

**実害:** 日本語 Windows + 日本語パスで「ファイルが見つからない」「化けた名前のファイルが作られる」。同じ関数が macOS では動くため「Windows でだけ壊れる」報告になる典型。

### H-5. `path.string()` による逆方向の劣化 — ANSI バイト列を UTF-8 として FileMaker へ
**場所:** `Source/BEPluginFunctions.cpp:809` ほか

正しくワイドで得たパスを `.string()` で narrow 化（Win では CP932）し、UTF-8 前提の経路に流す:

```cpp
SetResult ( from.filename().string(), file_data, data_type, results );  // BE_FileImport:809
```

`SetResult` は filename を `Assign ( ..., kEncoding_UTF8 )` で**無検証に UTF-8 として** FM に渡す（string 版 SetResult にある `IsValidUTF8` 検査を filename 引数は通らない）。

- `BE_FileImport` :809（コンテナのファイル名）
- `BE_Gzip` :2365 / `BE_UnGzip` :2401
- `BE_Zip` :2102-2103（`filename().string()` を ZipFiles へ）
- `BE_SMTPAddAttachment` :3492（`ParameterAsPath(...).string()`）
- `BE_FTP_Upload` :3160 — `file_to_upload.string()` を BECurl の `path` 引数へ渡す **wide→narrow→path の往復劣化**（すぐ上の `BE_HTTP_GETFile` :2841 は path を直接渡しており不整合）

**実害:** Windows で日本語ファイル名のコンテナ返却・ZIP・メール添付・FTP が文字化け／失敗。ACP に無い文字（絵文字ファイル名）は即死。

### H-6. Windows クリップボード — CF_TEXT の CP932⇔UTF-8 素通し + ヒープ書き越し
**場所:** `Source/win/BEWinFunctions.cpp:379-407`（読み）/ `467-490`（書き）

**読み (`UTF8ClipboardData`):** 関数名に反して、CF_TEXT / CF_OEMTEXT の生バイト列（日本語環境では CP932）を**一切変換せず** `std::string` に入れて返す。`MultiByteToWideChar(CP_ACP,...)` 相当の変換が丸ごと欠落。`BE_ClipboardGetText("CF_TEXT")` で他アプリが置いた日本語が確実に化ける。

**書き (`DataForClipboardAsUTF8`):** 2 つの問題の複合:

```cpp
SIZE_T clipboard_size = data_size + offset;
...
memcpy_s ( clipboard_contents + offset, clipboard_size, data.c_str(), clipboard_size );
```

1. UTF-8 のまま CF_TEXT に置く（読みの鏡像。受け取り側アプリで化ける）。NUL 終端も付けない。
2. FileMaker 形式（offset=4）のとき、書き込み先の残容量は `data_size` なのにコピー長に `data_size+4` を渡す → **ヒープ末尾 4 バイト書き越し + `data.c_str()` の終端の先 3 バイト読み越し**（UB）。`memcpy_s` の宛先容量引数にも同じ嘘の値を渡しているためガードが効かない。`&data_size` は x64 では 8 バイトの SIZE_T で先頭 4 バイトだけコピーされる（LE なので偶然動くだけ）。

既知の `_popen` 0x3F 化け（BE_ExecuteSystemCommand、再設計済み）と同根の「ANSI と UTF-8 の境界を素通しにする」パターン。

### H-7. `BEValueList::to_lower` — 符号付き char を `::tolower` に直渡し（UB）+ UTF-8 のバイト単位処理
**場所:** `Source/BEValueList.h:443-451`、`Source/BEPluginFunctions.cpp:837,856`

```cpp
std::transform ( it->begin(), it->end(), it->begin(), ::tolower );        // BEValueList.h:448
std::transform ( hay.begin(), hay.end(), hay.begin(), ::tolower );       // BEPluginFunctions.cpp:856
```

`char` は符号付きなので UTF-8 の非 ASCII バイト（0x80–0xFF）は負値として `::tolower(int)` に渡り、**C 標準上の UB**（EOF でも unsigned char 表現値でもない）。**MSVC のデバッグ CRT はここで assert する**（この 32bit MSVC フォークに直撃）。リリースでもテーブル外参照の可能性。仮に動いても UTF-8 のバイト単位処理なので非 ASCII の大小無視は成立せず、ロケール次第では構成バイトが置換されて文字列自体が壊れる。

**発火条件:** `BE_FilePatternCount` に日本語や絵文字を含む検索語を渡すと即 UB 入力。

### H-8. SMTP 添付 — ファイルパスを RFC 2047 エンコードしてから open
**場所:** `Source/Net/BESMTPEmailMessage.cpp:145-147`

```cpp
auto file_name = Poco::Net::MailMessage::encodeWord ( attachment.filename().string() );
auto path = Poco::Net::MailMessage::encodeWord ( attachment.string() );
message.addAttachment ( file_name, new Poco::Net::FilePartSource ( path, new_attachment.second ) );
```

`encodeWord` はメールヘッダ用の `=?UTF-8?q?...?=` 符号化。それを**ファイルパスに適用してから open** している。純 ASCII パスでは encodeWord が素通しなので露見しないが、非 ASCII（日本語ファイル名、日本語ユーザー名を含む `%TEMP%`）を 1 文字でも含むと符号化文字列をパスとして open → Poco の `OpenFileException`。`BESMTP::send` は `BEPlugin_Exception` しか catch しない（BESMTP.cpp:111）ため例外がそのまま上へ抜ける。**日本語環境で添付付きメールが確実に失敗。**

### H-9. `fmx::Text::Assign` のデフォルト `kEncoding_Native` に UTF-8 を渡している
**場所:** `Source/BESQLCommand.cpp:34-35`、`Source/BEJavaScript.cpp:104`

```cpp
expression->Assign ( _expression.c_str() );   // encoding 指定なし = kEncoding_Native
filename->Assign ( _filename.c_str() );
```

`Assign` のデフォルトは `kEncoding_Native`（Windows では ANSI）。渡している文字列は `ParameterAsUTF8String` / duktape 由来の **UTF-8**。Windows(CP932) で日本語テーブル名・フィールド名・文字列リテラルを含む SQL（`BE_FileMakerSQL`、バックグラウンドタスク SQL）や JS からの FM 計算式が**化けたまま評価される**。修正は `Assign ( s.c_str(), fmx::Text::kEncoding_UTF8 )` の 2 箇所 + 1 箇所で済む。

### H-10. `BE_FileWriteText` — エンコード変換の全滅を「成功・0 バイト」として書く
**場所:** `Source/BEPluginFunctions.cpp:604-627`（+ H-1 の ConvertTextEncoding）

```cpp
vector<char> out = ConvertTextEncoding ( (char *)text_to_write.c_str(), text_to_write.size(), g_text_encoding, UTF8 );
error = write_to_file ( path, out, mode );
```

`ConvertTextEncoding` は全候補 EILSEQ のとき**空 vector を返しエラーにしない**。`BE_SetTextEncoding("CP932")` 等の設定下で CP932 に無い文字（絵文字、"–"等）を 1 文字でも含むテキストを書くと、**0 バイトのファイルを書いて error=0 を返す**（`trunc` モードなら既存内容も消える）。UTF-16 解釈が偶然通れば別の化けバイト列を書く。iconv の `//TRANSLIT` 等の緩和も部分変換の続行もない。
※ 過去に解決済みの「H2: BE_FileWriteText 0 バイト問題」（リンカ起因、belibs 隔離で解決）とは**別物**の潜在バグ。

### H-11. iOS `ClipboardText` — nil チェック欠落 → `std::string(NULL)` の UB
**場所:** `Source/apple/BEIOSFunctions.mm:43-52`

Mac 版（BEMacFunctions.mm:81-83）にある `nil → @""` ガードが iOS 版には無い。ペーストボードのデータが UTF-8 として不正（UTF-16 テキスト・バイナリ）だと `initWithData:` が nil → nil への `cStringUsingEncoding:` が NULL → **NULL から `std::string` を構築（UB、通常クラッシュ）**。Mac 版にある utf16 タイプ分岐も無く UTF-8 決め打ち。

---

## 中（特定条件で発生）

### Windows クリップボード形式判定の崩壊（M-1, M-2）
- **M-1** `win/BEWinFunctions.cpp:73` — コピペミス: `BE_CF_FileNameMapW = RegisterClipboardFormat ( CFSTR_FILEDESCRIPTORW );`（正しくは `CFSTR_FILENAMEMAPW`）。`IsUnicodeFormat` が false になり、**FileNameMapW の UTF-16 データが narrow 経路で処理され先頭 1 文字で切断**される（UTF-16LE の ASCII は 1 バイトおきに 0x00）。関連して `ClipboardFormatIDForName` :291-296 は `to_upper_copy` した名前を混在ケース定数と比較する**到達不能分岐**（291 行と 295 行は同一条件の重複）で、フォールバックで偶然動いているだけ。
- **M-2** 同 :62-64 — 形式 ID が `thread_local`。`InitialiseForPlatform` は 1 スレッドでしか呼ばれないため、**別スレッドでは ID が 0 のまま** → Wide 形式が narrow 扱いに。プロセス不変値を thread_local にする理由がなく、エンコード判定がスレッド依存で変わる。

### コア変換層（M-3, M-4, M-25〜M-27）
- **M-3** `BEPluginUtilities.cpp:746-754 ReadFileAsUTF8` — 変換全滅時のフォールバック `result.assign ( buffer.data() )` が **NUL 終端されていない** `vector<char>` の生ポインタに対する NUL 探索 → ヒープ領域外リード（UB）。加えて H-1 の UTF-16 誤認の主戦場。
- **M-4** 同 :828-833 `ConvertTextEncoding(std::string&)` — `in.size() - 1` で**常に最終バイト欠落**、空文字列なら `0-1 = SIZE_MAX` を iconv に渡す完全 UB。現状呼び出しゼロの**未起爆の地雷**（ヘッダで公開されたまま）。
- **M-25** 同 :710,738 — `(size_t)file_size(path)` — 32bit ビルド（このフォークの主目的）で 4GB 超ファイルが下位 32bit に切り詰められ、途中までを「全体」として黙って読む。
- **M-26** 同 :853-865 `IsValidUTF8` — マルチバイト列が末尾で切れた文字列は iconv が EILSEQ でなく **EINVAL** を返すため、`kInvalidUTF8`(10100) ではなく **errno 生値（macOS では 22）が FM のエラーコードとして返る**。バイト境界無視で切り出した UTF-8 は現実的入力。
- **M-27** `BEPluginUtilities.h:71` — `ParameterAsDouble ( ..., const bool default_value = 0.0 )`。デフォルト値の型が **bool**。`30.0` を渡しても 1.0 に潰れる（宣言・定義とも同じ typo）。

### 正規表現・XML・XSLT（M-5, M-10〜M-14）
- **M-5** `BERegularExpression.h` — Poco 1.10(PCRE) に **RE_UTF8 を渡さずバイトモード**で使用。`.` が日本語 1 文字の 1/3 バイトにマッチ、`[あ-ん]` は無意味なバイトクラス、量指定子でマルチバイト文字の途中を切った不正 UTF-8 が生成される。`BE_RegularExpression` :4693、**`BE_FileReplaceText` :915（ファイル全体を置換して書き戻すので影響大）**、`BE_FileReadText` :513 の `boost::regex` デリミタ分割も同様。
- **M-10** `BEXMLTextReader.cpp:66` — XML ファイルを `_wopen ( ..., O_RDONLY | _O_WTEXT )` で開いて `xmlReaderForFd` へ。libxml2 は生バイトを期待して自前でエンコード検出するのに、CRT のテキストモード変換（CRLF→LF、BOM に応じた変換）を挟むため二重変換・オフセット狂い。`_O_BINARY` であるべき。
- **M-11** 同 :43-44 — `find_if_not ( ..., [](int c){ return isspace(c); } )` — 先頭が UTF-8 マルチバイト（日本語パス等の 0xE3）だと **負値を isspace に渡す UB**（MSVC debug で assert）。全空白入力では `it == end()` のまま `*it` 参照。
- **M-12** 同 :295-299,316-320 — `xmlTextReaderReadInnerXml` は空要素（`<a/>`）でエラーなしに NULL を返すが、NULL チェックなしで `std::string` に代入 → クラッシュ。
- **M-13** `BEXMLSchema.cpp:76,93,129` — このファイルだけ `XML_PARSE_IGNORE_ENC` が無い。FM から来る文字列は常に UTF-8 なのに、文書が `encoding="Shift_JIS"` を宣言していると libxml2 がそれを信じて再解釈 → 化け・偽の検証エラー。BEXSLT.cpp(161,448) と BEXMLTextReader.cpp(40) は同じ理由でフラグを付けており、抜けているのはここだけ。
- **M-14** `BEXSLT.cpp:216-217` — `xsltSaveResultTo` は `<xsl:output encoding="...">` を尊重するため、UTF-8 以外を指定したスタイルシートでは非 UTF-8 バイト列がそのまま `std::string`→`SetResult` へ → `kInvalidUTF8` 例外か文字化け。

### curl / SMTP（M-6〜M-9, M-16, M-17）
- **M-6** `Net/BECurl.cpp:121-156` — read コールバックが `userdata->origin` を**毎回現在位置で上書き**。認証チャレンジ等で curl が `SEEK_SET 0` に巻き戻すと最後のチャンク位置からしか再送されない。コメント「required when doing authenticated http put」のまさにそのケース（認証付き PUT >64KB）で**破損データを送信**。
- **M-7** 同 :796-811 `easy_setopt` — 全オプションを `va_arg ( ..., void* )` 1 本で中継。**32bit ビルドでは `void*`=4B vs `curl_off_t`=8B で va_list がずれる**（`CURLOPT_*_LARGE` 系、`BE_CurlSetOption` から到達可能）。x64 Windows では `long`=4B なので `CURLOPT_POSTFIELDSIZE, parameters.length()` :592 等が 4GB 超で切り詰め。
- **M-8** `Net/BESMTPEmailMessage.cpp:42,121,59-61` — Subject だけ `encodeWord` 対応済みで、**From/Reply-To の表示名（`"山田太郎 <t@example.jp>"`）とカスタムヘッダは生 UTF-8** のままヘッダへ。SMTPUTF8 非対応サーバで化け・拒否。
- **M-9** `Net/BESMTPContainerAttachments.cpp:41` — `temporary_attachments_directory() + file_name`（UTF-8 の std::string 連結）で narrow path 構築。Windows では ACP 解釈でディスク上のファイル名が化け、逆に `filename().string()` は ACP バイト列なのに `encodeWord` の charset ラベルは "UTF-8" → 受信側で添付名が化ける。
- **M-16** `Net/BECurlOption.cpp:139,198` — 型テーブルの誤登録: `CURLOPT_HTTPGET` を string 型（実際は long）、`CURLOPT_TLSAUTH_TYPE` を long 型（実際は char*）。後者は curl が小整数をポインタとして strdup しようとして**クラッシュ**。
- **M-17** `Net/BECurl.cpp:238-241` — `CURLINFO_TEXT` の `char* data` を size 無視でストリームへ。CURLOPT_DEBUGFUNCTION のバッファは **NUL 終端非保証**（curl docs 明記）で、`CURLOPT_VERBOSE` は Init() で常時 1 なので全リクエストで領域外リードの可能性。`std::string(data, size)` にすべき。

### プラットフォーム層（M-18〜M-22）
- **M-18** `linux/BELinuxFunctions.cpp:221-227 Sub_LoadString` — `intoHere[wanted.size()] = 0x0000` の `wanted.size()` は **UTF-8 バイト数**、`intoHere` は **UTF-16 単位の配列**。単位の混同（現在は ASCII 文字列のみなので潜在）。加えて `Assign` のデフォルト `kEncoding_Native` 依存。バッファ長無視は [BUFFER-AUDIT #3](BUFFER-AUDIT.md) 参照。
- **M-19** 同 :170-188 `get_machine_name` — `mbstowcs` は**現在の C ロケール**依存。FM Server(Linux) で setlocale されていなければ "C" ロケールで非 ASCII ホスト名が `(size_t)-1` → 例外。`gethostname` の戻り値 `int(-1)` を `size_t` に入れて比較する符号混同も。
- **M-20** `win/BEWinFunctions.cpp:1001-1017 Sub_LoadString` — `%@`→バージョン置換・`\n`→`\r\n` 置換で**伸びた**文字列を `resultsize` 無視で `copy` 書き戻し（[BUFFER-AUDIT #1](BUFFER-AUDIT.md)）。`find(L"%@")` が npos なら `replace` が `std::out_of_range` を FM 本体へ送出。`(LPWSTR)intoHere` は wchar_t=2B 前提の無言キャスト。
- **M-21** `apple/BEMacFunctions.mm:85` — `cStringUsingEncoding:` 自体が変換不能時（utf16 分岐で読んだ孤立サロゲート入りデータ）に NULL を返すケース未処理 → `std::string(NULL)` UB。
- **M-22** `apple/BEAppleFunctionsCommon.mm:55-61 WStringFromNSString` — `allowLossyConversion:` なしの `dataUsingEncoding:` は孤立サロゲート入り NSString で nil を返す。nil へのメッセージで length=0/bytes=NULL → 空文字列に化けて**失敗と空が区別不能**。この層の全戻り値（ClipboardFormats / SelectFileOrFolder / get_machine_name）に影響。

### 値リスト・その他（M-23, M-24, M-28〜M-30）
- **M-23** `BEValueList.h:135-155` — `boost::is_any_of ( delimiter )` は区切りを**バイトの集合**として扱う。UTF-8 マルチバイト区切り（「、」= E3 80 81）は各バイト単体で分割され、E3/80/81 は日本語の構成バイトとして頻出するため**無関係な文字の内部で切断**される。現呼び出しは ASCII のみ（","・"\r"）だが公開 API として非 ASCII を受ける形。
- **M-24** 同 :502-506 `trim_values` — `boost::trim ( ..., std::locale() )` は**グローバルロケール依存**。シングルバイト系ロケール下では 0xA0(NBSP) が空白分類され、「à」(C3 A0) など **0xA0 で終わる UTF-8 文字の末尾バイトだけが除去**されて不正 UTF-8 になる。`BE_ValuesTrim` の実体。
- **M-28** `BEPluginFunctions.cpp:3279 BE_BackgroundTaskAdd` — バイナリ/非 UTF-8 レスポンスを `std::string` 化して JSON→SQL 文字列に**無エスケープ**で `replace_all` 埋め込み。（付記: 同 :3271 の detached スレッドが `&environment` を**参照キャプチャ**しているのは呼び出し元スタック消滅後のダングリング参照 = UB。エンコード外だが重大）
- **M-29** 同 :4072-4087 `BE_ScriptStepPerform` — コンテナ（kDTBinary）を `AtAsText` でテキスト化し、`"` を含むパラメータも無エスケープで計算式に文字列連結 → 化け + **式インジェクション相当**。
- **M-30** 同 :495-535 `BE_FileReadText` — テキスト分岐は元の `long from` で負値判定するのに、デリミタ分岐は `static_cast<fmx::uint32>(from)` 後に判定 → **負の from が巨大開始位置**になり何も返らない。:550 の `AssignWithLength` は 4GB 超で切り詰め。

---

## 低（理論上・作法・限定条件）

| # | 場所 | 内容 |
|---|------|------|
| L-1 | win/BEWinFunctions.cpp:990-998 `utf16ToUTF8` | API 失敗時に空 vector へ `&buf[0]`（UB）。`-1` 長指定で埋め込み NUL 以降を破棄。`WC_ERR_INVALID_CHARS` 未指定で不正 UTF-16 が黙って U+FFFD 化 |
| L-2 | win/BEWinFunctions.cpp:968-987 `utf8toutf16` | `MB_ERR_INVALID_CHARS` 未指定・戻り値未検査。不正 UTF-8 が黙って置換される |
| L-3 | win/BEWinFunctions.cpp:947-955 `get_system_drive` | ファイル内唯一の明示的 A 系 API（`GetSystemDirectoryA`）。取り出すのがドライブ文字だけなので実害ほぼ無し |
| L-4 | apple/BEAppleFunctionsCommon.mm:29-34 `NSStringFromString` | `stringWithCString:` は不正 UTF-8 で nil、埋め込み NUL で切断。nil が `setObject:nil`（NSInvalidArgumentException）へ伝播する経路あり |
| L-5 | apple/BEAppleFunctionsCommon.mm:142-147 `GetPreference` | plist 値を NSString と決め打ち（`isKindOfClass:` 検査なし）。他ツールが NSNumber/NSData を書いていると例外 |
| L-6 | apple/BEAppleFunctionsCommon.mm:85-86 `Sub_LoadString` | `getCharacters:range:` がバッファ長を検査しない（[BUFFER-AUDIT #2](BUFFER-AUDIT.md)）。`(unichar*)intoHere` キャスト自体は両者 16bit で正当 |
| L-7 | apple/BEMacFunctions.mm:77-79 | `public.utf16-plain-text` を無条件に LE・BOM なしと決め打ち。BOM 付きデータで U+FEFF 混入 |
| L-8 | BEPluginUtilities.cpp:894-902 `TextAsUTF8String` | `GetBytes` の実書き込み数を捨てて NUL 終端仮定の `assign(text)`（U+0000 で切れる）。`GetSize()*4+1` の uint32 溢れは約 10 億文字超で理論上。全例外を握り潰し空文字列を返すため失敗と空が区別不能（[BUFFER-AUDIT #15](BUFFER-AUDIT.md)） |
| L-9 | BEQuadChar.cpp:127-133 `is_type` | 長さ未検査の `operator[]`（4 バイト未満で UB）。長さ検査は「4 文字」でなく「4 バイト」なので非 ASCII 混じりが QuadChar として通る |
| L-10 | BEQuadChar.cpp:42-62 `as_string` | バイト値を UTF-16 コード単位へゼロ拡張（暗黙 Latin-1 解釈）。0x80 以上を含むストリーム型の表示が化ける程度 |
| L-11 | BEPluginUtilities.cpp:198,208,218 / :535 | `(FMX_UInt32)output.size()` の 4GB 切り詰め / `LoadFromBuffer ( ..., (long)pdf.size() )` は LLP64 で 2GB 超 PDF が負値化 |
| L-12 | BEPluginUtilities.cpp:303-310 `ParameterAsLong` | デフォルト値は `unsigned long`、戻り値は `long`。`kBE_Never = -1` を渡す BE_ExecuteSystemCommand :4314 は符号往復で偶然動いているだけ。2^31 超は Windows でのみ壊れる非対称 |
| L-13 | BEValueList.h:32-40 `convert_to_double` | パース失敗を無言で 0 に（"abc" も "12abc"→12 も黙認）。`BE_ValuesSort`(numeric)・`BE_VectorDotProduct` が静かに狂う |
| L-14 | BEValueList.h:239,314,518 | case-insensitive 比較がグローバルロケールのバイト単位変換。非 ASCII の大小同一視は不成立、非 C ロケールでは `BE_ValuesUnique` が別値を同一視する可能性 |
| L-15 | BEPluginFunctions.cpp:2583-2585 | `boost::algorithm::unhex` の例外（奇数長・非 hex）を汎用 catch が `kErrorUnknown` に黙殺 |
| L-16 | BEPluginFunctions.cpp:3998,4002,4030 | `ParameterAsLong` の long を `(short)id` に無検査切り詰め（32768 以上で別 ID に折り返し） |
| L-17 | BEPluginFunctions.cpp:679-689 `BE_XMLStripInvalidCharacters` | `(unichar16*)codepoint` の C キャスト（strict aliasing）。BOM なしはビッグエンディアン仮定。奇数長ファイルの最終 read 未チェック |
| L-18 | Net/BECurl.cpp:1074 / :931-941 / :302 | `std::stoi` が getinfo 失敗（空文字列）で `invalid_argument` 送出、catch 経路なし / `release_memory` の free 条件が**論理逆転**（非 NULL 時に解放されない）/ `curl_off_t→unsigned long` で 32bit の進捗表示が 4GB 超で破綻 |
| L-19 | Net/BECurlOption.cpp:102 | 未知の定数名を `atol` フォールバック（エラー検出なし）→ タイポが黙って 0（認証無効等）に |
| L-20 | BEJSON.cpp:73,127 / BESQLCommand.cpp:197-215 / BEZlib.cpp:214,255 / BEXSLT.cpp:78-88 | `(int)length` で 2GB 超 JSON が負値化・`strerror` の CP932 バイトが UTF-8 経路に混入 / 行・列セパレータを先頭 UTF-16 単位 1 つに切り詰め（絵文字指定で孤立サロゲート）/ `(unsigned int)size` の 4GB 切り詰め + 途中で切れた gzip を**エラーなしで部分データ返却** / catch パスで消費済み va_list を再利用（bad_alloc 時のみの UB） |

---

## 問題なしと判断した箇所（対比）

- **BEShellExec.cpp（このフォークでの新実装）** — 指摘ゼロ。CP932/OEM 境界、UTF-16LE の奇数バイト、DBCS のチャンク分割（全読み取り後にデコード）、GetTickCount ラップアラウンドまで正しく処理。既存コードとの品質差が明確。
- **ParameterAsPath**（単一パス引数の経路）— ワイド文字列経由で構築しており Windows の日本語パスに正しく対応。問題は H-4/H-5 のようにこの経路を**バイパスしている箇所**に集中している。
- **BEFileSystem.cpp** — wstring ベースで一貫（Windows は Poco::UnicodeConverter 使用）。
- **SetResult(std::string) の IsValidUTF8 検査** — 不正 UTF-8 で FileMaker が無限ループする事故の防波堤として妥当（ただし filename 引数はこの検査を通らない = H-5 の穴）。
- **wchar_t のサイズ差自体**の扱いは、各プラットフォームで変換関数を分けており設計として正しい（壊れているのは H-3 のサロゲート合成だけ）。

---

## 修正優先度の提案

1. **1〜2 行で直せて日本語環境の実害が消えるもの（最優先）**
   - H-9: `Assign` に `kEncoding_UTF8` を明示（BESQLCommand.cpp 2 行 + BEJavaScript.cpp 1 行）
   - H-8: SMTP 添付の `encodeWord(path)` を撤去（ファイル名の encodeWord は M-9 と併せて要検討）
   - M-1: `CFSTR_FILENAMEMAPW` への 1 語修正
   - M-13: `XML_PARSE_IGNORE_ENC` の追加 3 箇所
   - M-16: 型テーブル 2 行の修正
2. **このフォーク（32bit MSVC・日本語 Windows）に直撃するもの**
   - H-7: `::tolower` → `[](unsigned char c){ return std::tolower(c); }` 化（MSVC debug assert の地雷）
   - H-4/H-5: パス変換の 2 系統の穴 — `boost::filesystem::path::imbue ( std::locale ( std::locale(), new boost::filesystem::detail::utf8_codecvt_facet ) )` を初期化時に 1 回呼ぶのが最小修正（narrow path の解釈が UTF-8 に統一される）
   - H-6: クリップボードの CP_ACP⇔UTF-8 変換の追加と memcpy_s サイズ修正
   - M-7: `easy_setopt` の型別ディスパッチ（32bit で curl_off_t がずれる）
3. **設計対処が必要なもの**
   - H-1/H-10/M-3: 「UTF-16 フォールバックの廃止 or BOM 検出 + 変換失敗を明示エラー化」— ConvertTextEncoding の仕様変更なので互換性判断が要る
   - H-2: `SetResult(vector<char>&)` にサイズ引数を復活（`AssignWithLength` 系へ）— NUL 入りデータの互換性確認
   - H-3: サロゲートペア合成の実装（mac/Linux の ParameterAsWideString）
4. **低優先** — L 群は挙動変更リスクと相談しつつ随時。

---

*生成: 4 並列の精査エージェント（コア変換層 / プラットフォーム層 / BEPluginFunctions.cpp / Net・XML・シェル層）による全 18,691 行の監査結果を統合。深刻度「高」の全項目はソースコードで裏取り済み。*
