# メモリ確保の手抜き箇所監査（固定長バッファ／動的確保すべき箇所）

> **対応状況 (2026-07-03):** 修正済み = #1 (Win Sub_LoadString クランプ+npos), #5 (new[]/free不一致→vector)。commit `62675de2`。
> 未対応 = #2/#3 (Mac/Linux Sub_LoadString、このフォークのWin32ビルドでは非実行), #4, #6〜#16。詳細はメモリ `be-plugin-audit-fixes` 参照。

対象: `BaseElements-Plugin/Source`（duktape 等のサードパーティ同梱コードは除外）
調査日: 2026-07-02
観点: **本来は必要サイズを測って動的確保（または std::string / std::vector）すべきところを、固定長スタックバッファや無検査コピーで済ませている箇所**。あわせて確保／解放ペアの不一致も記録する。

---

## サマリ

| # | 深刻度 | ファイル:行 | 関数 | 問題の型 |
|---|--------|-------------|------|----------|
| 1 | ★★★ 高 | Source/win/BEWinFunctions.cpp:1001 | `Sub_LoadString` (Win) | 呼び出し元バッファへの無検査コピー（オーバーフロー） |
| 2 | ★★★ 高 | Source/apple/BEAppleFunctionsCommon.mm:64 | `Sub_LoadString` (Mac) | `intoHereMax` 完全無視のコピー（オーバーフロー） |
| 3 | ★★★ 高 | Source/linux/BELinuxFunctions.cpp:205 | `Sub_LoadString` (Linux) | `intoHereMax` 無視 + 終端位置の単位誤り |
| 4 | ★★★ 高 | Source/linux/BELinuxFunctions.cpp:171 | `get_machine_name` (Linux) | NUL 終端非保証の固定バッファを後続 API に渡す |
| 5 | ★★★ 高 | Source/BEZlib.cpp:225,240 | `CompressContainerStream` | `new[]` を `free()` で解放（未定義動作） |
| 6 | ★★☆ 中 | Source/win/BEWinFunctions.cpp:221 | `ClipboardFormatNameForID` | 未初期化固定バッファ + API 戻り値無視 |
| 7 | ★★☆ 中 | Source/win/BEWinFunctions.cpp:660 | `SelectFolder` | MAX_PATH 制限 API + PIDL 解放漏れ |
| 8 | ★★☆ 中 | Source/BEPluginUtilities.cpp:1243 | `Do_GetString` | 4096 固定バッファへの付け替え（黙殺切り詰め） |
| 9 | ★★☆ 中 | Source/BEXSLT.cpp:76 / Source/BEXMLSchema.cpp:45 | `XSLTErrorFunction` / `XSDErrorFunction` | 10 KB 固定でエラー本文喪失 + 無意味な try/catch |
| 10 | ★★☆ 中 | Source/win/BEWinFunctions.cpp:949 | `get_system_drive` | API 戻り値未チェックで未初期化バッファ使用の可能性 |
| 11 | ★☆☆ 低 | Source/win/BEWinFunctions.cpp:937 | `get_machine_name` (Win) | 戻り値無視（現状は上限内で辛うじて安全） |
| 12 | ★☆☆ 低 | Source/win/BEWinFunctions.cpp:380-435 | `UTF8ClipboardData` / `WideClipboardData` | NULL 未チェック + 死んだ NULL 判定 + 生 new/delete |
| 13 | ★☆☆ 低 | Source/win/BEWinFunctions.cpp:968 | `utf8toutf16` (Win) | 生 `new WCHAR[]`（例外時リーク） |
| 14 | ★☆☆ 低 | Source/BEPluginUtilities.cpp:348-369 | `ParameterAsWideString` | 生 new 2 連発（例外時リーク） |
| 15 | ★☆☆ 低 | Source/BEPluginUtilities.cpp:894 | `TextAsUTF8String` | 生 new + catch 時リーク + 過大確保 |
| 16 | ★☆☆ 低 | Source/BEPluginUtilities.cpp:665 | `BinaryDataAsVectorChar` | 二重確保（new → vector コピー）+ 例外時リーク |

---

## 高（実際に書き壊す・未定義動作）

### 1. Win `Sub_LoadString` — 置換後文字列を `resultsize` 無視で書き戻す
**場所:** `Source/win/BEWinFunctions.cpp:1001-1017`

```cpp
LoadStringW((HINSTANCE)(gFMX_ExternCallPtr->instanceID), (unsigned int)stringID, (LPWSTR)intoHere, (fmx::uint32)resultsize);

if (kFMXT_AppConfigStr == stringID || PLUGIN_DESCRIPTION_STRING_ID == stringID) {
    std::wstring plugin_description_string = (LPWSTR)intoHere;
    plugin_description_string.replace(plugin_description_string.find(L"%@"), 2, WSTRING(VERSION_STRING));
    boost::replace_all(plugin_description_string, L"\n", L"\r\n");
    plugin_description_string.copy((WCHAR*)intoHere, plugin_description_string.length(), 0);
    intoHere[plugin_description_string.length()] = '\0';
}
```

- `LoadStringW` 自体は `resultsize` で切り詰めるが、その後 `%@` → バージョン文字列の置換と `\n` → `\r\n` の展開で**文字列が伸びる**。伸びた結果を `copy()` で `intoHere` に**サイズ無検査**で書き戻し、さらに末尾 +1 に NUL を書く。説明文がバッファ残量ギリギリだと呼び出し元バッファ（FileMaker から渡された領域、または #8 の 4096 固定バッファ）を書き壊す。
- おまけ: `find(L"%@")` の `npos` 未チェック。リソースに `%@` が無いと `std::out_of_range` が C コールバック境界を突き抜ける。

**修正方針:** `std::wstring` 上で加工（ここは既にできている）→ `copy` の長さを `min(length, resultsize - 1)` にクランプし、NUL もその範囲内に置く。`find` は `npos` チェック。

### 2. Mac `Sub_LoadString` — `intoHereMax` を一切使わない
**場所:** `Source/apple/BEAppleFunctionsCommon.mm:64-92`

```objc
[message getCharacters: (unichar*)intoHere range: {0, [message length]}];
intoHere[ [message length] ] = '\0';
```

- `intoHereMax > 1` の存在チェックだけして、コピー長のクランプに**まったく使っていない**。ローカライズ文字列が `intoHereMax` を超えた瞬間に呼び出し元バッファを書き壊す。書き込み側 API（FMX の `Do_GetString`）が最大長を渡してくる契約なのに無視している典型的手抜き。

**修正方針:** `NSUInteger n = MIN([message length], (NSUInteger)(intoHereMax - 1));` で `range:{0, n}` に制限し、`intoHere[n] = 0`。

### 3. Linux `Sub_LoadString` — `intoHereMax` 無視 + 終端位置の単位が UTF-8 バイト長
**場所:** `Source/linux/BELinuxFunctions.cpp:205-235`

```cpp
fmx::TextUniquePtr text;
text->Assign ( wanted.c_str() );
text->GetUnicode ( intoHere, 0, fmx::Text::kSize_End );
intoHere[wanted.size()] = 0x0000;
```

- `GetUnicode(..., kSize_End)` は文字列全長を書き込む。`intoHereMax` によるクランプなし → #2 と同型のオーバーフロー。
- さらに `wanted.size()` は **UTF-8 のバイト数**。非 ASCII を含むと UTF-16 単位数と一致せず、終端 NUL の位置がずれる（多バイト文字が多いほど後ろに書く＝範囲外書き込みを助長）。

**修正方針:** `GetUnicode` の第 3 引数に `intoHereMax - 1` を渡し、書き込んだ実長（`text->GetSize()` と `intoHereMax - 1` の小さい方）の位置に NUL を置く。

### 4. Linux `get_machine_name` — `gethostname` の NUL 終端非保証
**場所:** `Source/linux/BELinuxFunctions.cpp:171-188`

```cpp
char host_name [ HOST_NAME_MAX ];
const size_t error = gethostname ( host_name, HOST_NAME_MAX );
wchar_t w_host_name [ HOST_NAME_MAX * 4 ];
const size_t length = mbstowcs ( w_host_name, host_name, HOST_NAME_MAX * 4 );
```

- POSIX の `gethostname` は**ホスト名がちょうど収まらないとき NUL 終端を保証しない**。その未終端バッファを `mbstowcs` に渡すため、スタックを読み越す。
- `gethostname` は `int`（0 / -1）を返すのに `size_t` で受けて `error != kErrorUnknown` と比較しており、-1 の判定も型混乱している。

**修正方針:** 最低限 `host_name[HOST_NAME_MAX - 1] = '\0';` を挟む。本筋は `std::vector<char>` を `sysconf(_SC_HOST_NAME_MAX)+1` で確保し、戻り値は `int` で受ける。

### 5. `CompressContainerStream` — `new[]` した領域を `free()` で解放（未定義動作）
**場所:** `Source/BEZlib.cpp:225` と `Source/BEZlib.cpp:240`（`be_free` の実体は `Source/BECppUtilities.cpp:20` = `free()`）

```cpp
unsigned char * output_buffer = new unsigned char [ size_required ];
...
be_free ( output_buffer );   // 中身は free() — new[] と不一致
```

- `new[]` / `free()` の組み合わせは**未定義動作**。MSVC の CRT 混在ビルド（この 32bit フォークでまさに踏んでいる領域）ではヒープ破壊として顕在化しうる。
- そもそも固定量を一括 `new` する必要がなく、隣の `UncompressContainerStream` と違い動的コンテナを使っていない。

**修正方針:** `std::vector<unsigned char> output_buffer(size_required);` にして `be_free` 行を削除。`deflate` 失敗経路でも自動解放される。

---

## 中（切り詰め・未初期化・戻り値無視）

### 6. `ClipboardFormatNameForID` — 未初期化バッファ + 戻り値無視 + 定数の流用
**場所:** `Source/win/BEWinFunctions.cpp:219-224`

```cpp
wchar_t format[PATH_MAX];
int name_length = GetClipboardFormatName ( format_id, format, PATH_MAX );
format_name = format;
```

- `format` は未初期化。`GetClipboardFormatName` が 0 を返す（未登録フォーマット ID 等）と、**未初期化スタックを NUL が現れるまで `wstring` 化**する。ゴミ文字列どころか読み越しの可能性もある。`name_length` は取得しているのに未使用。
- クリップボードフォーマット名に `PATH_MAX` を使うのは定数の意味からして誤用。

**修正方針:** 戻り値 0 なら空文字列（または `#<id>` 形式）を返す。`format_name.assign(format, name_length)` で長さ明示。バッファは必要なら `std::wstring` を resize して渡す。

### 7. `SelectFolder` — `SHGetPathFromIDList` の MAX_PATH 制限 + PIDL リーク
**場所:** `Source/win/BEWinFunctions.cpp:660-668`

```cpp
wchar_t path[PATH_MAX] = L"";
if ( item_list != 0 ) {
    SHGetPathFromIDList ( item_list, path );
}
return wstring ( path );
```

- `SHGetPathFromIDList` は呼び出し側バッファが MAX_PATH 前提の古い API。260 文字超のフォルダを選ぶと失敗（戻り値も未チェックなので黙って空/途中の値を返す）。動的サイズで受けられる `SHGetPathFromIDListEx` に置き換えるべき箇所。
- 併記: `SHBrowseForFolder` が返す PIDL（`item_list`）を `CoTaskMemFree`/`ILFree` していない。呼ぶたびにリーク。

### 8. `Do_GetString` — 上流から `resultsize` を受けているのに 4096 固定に付け替え
**場所:** `Source/BEPluginUtilities.cpp:1242-1246`

```cpp
const int length = 4096;
fmx::unichar16 temp_buffer [ length ];
Do_GetString ( whichStringID, 0, length, temp_buffer );
function_information->AssignUnicode ( temp_buffer );
```

- 関数プロトタイプ文字列を受けるバッファを**根拠のない 4096 固定**にしている。超えた分は黙って切り詰め（Win は `LoadStringW` が切る）、Mac/Linux は #2 / #3 の通りむしろ**あふれる**。固定長スタックの手抜きが下位のオーバーフローの直接の受け皿になっている。

**修正方針:** `std::vector<fmx::unichar16>` を使い、必要なら段階的に拡張して再取得。少なくとも下位 `Sub_LoadString` 側のクランプ（#1〜#3）とセットで直す。

### 9. `XSLTErrorFunction` / `XSDErrorFunction` — 10 KB 固定でエラー本文喪失、try/catch は死に体
**場所:** `Source/BEXSLT.cpp:70-93`、`Source/BEXMLSchema.cpp:38-62`（同型コードの重複）

```cpp
const int size = 10240;
try {
    xmlChar buffer[size]; // individual errors are typically < 1k
    int error = xmlStrVPrintf ( buffer, size, message, parameters );
    if ( error != -1 ) { g_last_xslt_error_text += ...; }
    else { g_last_xslt_error = kLowMemoryError; }
} catch ( exception& ) { ... }
```

- 10240 バイトを超えるエラーメッセージ（長大な XPath やスキーマパスを含むと普通に起きる）で `xmlStrVPrintf` が失敗し、**本文が丸ごと失われ「kLowMemoryError」と誤報告**される。実際はメモリ不足ではない。
- `try` ブロック内はスタック配列と C 関数だけで**何も throw しない**。「メモリ不足に備えた catch」はコメントごと機能していない（動的確保していた頃の名残とみられる）。

**修正方針:** `vsnprintf`（サイズ問い合わせ→ `std::string` resize → 本書き込み）の 2 パス方式にする。`va_list` は 2 回使うので `va_copy` が必要。2 ファイルの重複実装も 1 箇所に統合する。

### 10. `get_system_drive` — API 失敗時に未初期化の固定バッファを使用
**場所:** `Source/win/BEWinFunctions.cpp:947-955`

```cpp
char system_directory[PATH_MAX];
const UINT length = GetSystemDirectoryA ( (LPSTR)system_directory, PATH_MAX );
auto system_path = boost::filesystem::path ( system_directory );
```

- `length` を取得しているのに**未チェック**。0（失敗）や `> PATH_MAX`（バッファ不足時は必要サイズが返り、バッファは不定）のケースで未初期化配列をそのまま `path` に食わせる。
- ANSI 版 API を使っているのも減点（システムディレクトリに非 ASCII が入る環境でロス）。W 版 + 戻り値で必要サイズを見て確保、が本来の形。

### 11. Win `get_machine_name` — 現状は上限内だが戻り値無視
**場所:** `Source/win/BEWinFunctions.cpp:935-944`

- `MAX_COMPUTERNAME_LENGTH + 1` の固定バッファは `ComputerNamePhysicalNetBIOS` に限れば足りるが、`GetComputerNameEx` の戻り値を捨てている。将来 DNS 名系の `COMPUTER_NAME_FORMAT` に変えた瞬間に破綻する書き方。正攻法は「NULL で必要サイズを問い合わせ → 動的確保 → 本呼び出し」。ゼロ初期化してあるので失敗時も空文字で済んでいるのは偶然の産物。

---

## 低（動的確保はあるが作法が手抜き — 例外時リーク・死んだコード・二重確保）

### 12. `UTF8ClipboardData` / `WideClipboardData`
**場所:** `Source/win/BEWinFunctions.cpp:380-435`

- `GetClipboardData` / `GlobalLock` の NULL 未チェックのまま `GlobalSize`・`memcpy_s` に進む。
- `new char[...]()` の後の `if ( !clipboard_data )` は**絶対に真にならない死んだコード**（例外を投げる new に NULL 検査）。書いた本人が確保失敗の扱いを理解していない痕跡で、この関数は全体を `std::string` / `std::vector` で書き直すのが妥当。
- 生 `new`/`delete[]` のため、間の処理が投げるとリーク。

### 13. `utf8toutf16`（Win 版）
**場所:** `Source/win/BEWinFunctions.cpp:968-987`

- `new WCHAR[bufferlen + 1]` → `wstring out(widestr)` → `delete[]`。`wstring` 構築が投げるとリーク。すぐ下の `utf16ToUTF8` は `vector<char>` で正しく書けているので、同じ作法に揃えるだけ（`std::wstring w(bufferlen, 0)` に直接書き込む）。

### 14. `ParameterAsWideString`
**場所:** `Source/BEPluginUtilities.cpp:348-369`

- `new FMX_UInt16[text_size + 1]` に加え、Mac/Linux 分岐でさらに `new wchar_t[text_size + 1]`。`GetUnicode` や代入が投げると両方リーク（catch はあるが解放していない）。`std::vector` 2 本にすれば消える問題。

### 15. `TextAsUTF8String`
**場所:** `Source/BEPluginUtilities.cpp:887-905`

- `char * text = new char [ 4 * GetSize() + 1 ]()` — `GetBytes` が投げると catch 側で解放されずリーク。UTF-8 最悪 4 倍の過大確保も `std::string` + resize で必要分に抑えられる。

### 16. `BinaryDataAsVectorChar`
**場所:** `Source/BEPluginUtilities.cpp:664-675`

- `new char[size]` に読み込んでから `vector<char>` にコピーする**二重確保**。`vector<char> output(size)` を作って `data.GetData(..., output.data())` に直接書かせれば確保 1 回で済み、例外リークも消える。

---

## 誤検出しやすいが問題なしと判定した固定長バッファ

| 場所 | 理由 |
|------|------|
| `Source/BEShellExec.cpp:254,375` `char buf[4096]` | パイプ読みのチャンクバッファ。結果は `std::string` に逐次追記しており、固定長は転送単位でしかない。正当。 |
| `Source/Crypto/BEMessageDigest.cpp:115` `EVP_MAX_MD_SIZE` | OpenSSL が規定する全ダイジェストの上限値。API の想定通り。 |
| `Source/BEZlib.cpp:268` `WRITEBUFFERSIZE`（inflate 側） | チャンク処理で `vector` に追記するループ。正当（deflate 側 #5 とは別物）。 |
| `Source/Crypto/BEOpenSSLAES.cpp:73-74` `key_buffer[32]` / `iv[32]` | AES-256 の鍵長 32 バイト・IV16 バイト以下という API 仕様に基づく固定長。`iv` を鍵長で取るのは雑だが安全側。 |
| `Source/BEPluginUtilities.cpp:653` `quad_char[QUAD_CHAR_SIZE+1]` | 4 バイト固定の QuadChar 読み。仕様通り。 |
| `Source/BEPluginFunctions.cpp:675,685` `char codepoint[2]` | UTF-16 コードユニット単位の 2 バイト固定。仕様通り。 |

---

## 修正優先度の提案

1. **#5（new[]/free 不一致）** — CRT 混在の 32bit ビルドで顕在化しやすい未定義動作。修正も 3 行で済む。
2. **#1〜#3（Sub_LoadString 3 兄弟）** — 同じ契約（`intoHereMax` 尊重）を 3 プラットフォームとも破っている。#8 の 4096 固定とセットで直すと筋がよい。
3. **#4 / #6 / #10** — 未初期化・未終端読みの類。再現条件は狭いが直し方は機械的。
4. **#9** — 実害は「エラーメッセージが消える」だが、XSLT デバッグ時に地味に効く。
5. **#12〜#16** — RAII 化のリファクタリング。挙動変更を伴わないので余力があるときに。
