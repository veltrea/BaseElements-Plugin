# BaseElements Plugin 32bit 復活 — 次セッションへのハンドオーバー・プロンプト

（このファイルをそのまま次セッションの最初のプロンプトに貼ってよい。日本語で応答すること。ユーザーに休息・中断を促さないこと。git author は必ず `-c user.name="veltrea" -c user.email="<GIT_EMAIL>"` を明示。）

（プレースホルダ凡例: `<USER>` = WORK1 の対話ログオンユーザー / `<ADMIN>` = WORK1 の管理・ビルド用ユーザー / `<PORT>` = fmd11 デーモンの待受ポート / `<SSH_KEY>` = WORK1 用 SSH 鍵パス / `<GIT_EMAIL>` = git コミット用メールアドレス / `<N>` = 動的ポート数の設定値。実値はローカルの `~/.claude/machines.local.env`・`~/.claude/CLAUDE.md`・メモリ `be-plugin-daemon-test` を参照。）

---

# 🟢 SESSION 6 最終状態（2026-07-03 0:30頃） — ①BE_XPath 根治完了（XMLスタック MSVC静的化）②文字列/メモリ監査の高優先バグ修正 Batch1+2 完了（日本語パス実弾検証済み・commit 済み）③WORK1 再起動→テスト環境完全復旧済み

**⚠️ 最新の確定状態。詳細メモリ: `be-plugin-xml-msvc-static`（XPath根治）/ `be-plugin-audit-fixes`（監査修正の全記録）/ `be-plugin-daemon-test`（テスト基盤+再起動復旧手順）。**

## 現在の環境状態（次セッション開始時の前提）
- **WORK1: 稼働中・再起動済みのクリーン状態**（2026-07-03 0時過ぎに再起動。FMP がハングして手動でも起動できなくなったため）。
- **FMP11: 起動済み**、`fmtest.fp7` が開いていて全レコードの q = "idle"（プラグイン+HTTP+デーモン全経路正常の証明）。画面に Amical（別アプリ）のアップデートダイアログが出ているが無関係・未操作。
- **fmd11 デーモン: 稼働中**。`/ru SYSTEM` の schtasks に作り直した（<ADMIN> 非ログオンでも `schtasks /run /tn fmd11` で起動可能）。
- **⚠️ 再起動後の復旧ノウハウ（今回ハマった）**: (a) winnat の動的ポート予約ガチャが <PORT> を握ると node が EACCES で死ぬ → `net stop winnat & net start winnat`。**恒久対策済み**: `netsh int ipv4 set dynamicport tcp start=49152 num=<N>`（エフェメラル範囲をデーモンポート未満に制限）。(b) loophole は再起動後 reachable=False → `loophole_configure(host_ip, username=<USER>, ssh_key=<SSH_KEY>, ssh_opts="-o ProxyJump=none -o IdentitiesOnly=yes")` で再デプロイ。詳細はメモリ `be-plugin-daemon-test`。
- **Extensions の .fmx = Batch2 込みの最新ビルド**（監査修正まで全部入り）。belibs.dll は `FileMaker Pro 11\` 直下（無変更）。
- **転送の教訓**: mssh の stdin push は 5KB 超でハング多発 → **loophole_write_file + base64（13KB以下チャンク）+ SHA256 照合**が確実。PowerShell の `.Replace` 全置換パッチは他関数の同名変数に誤爆する（BEZlib で実際に発生）→ 原則丸ごと転送+ハッシュ照合。

## 監査バグ修正（Batch1+2、コミット済み）
ユーザー提供の `ENCODING-AUDIT.md`（プロジェクトルート）/`BUFFER-AUDIT.md`（filemaker-plugin/直下）をトリアージし、高優先2バッチを修正・実機検証・commit:
- **Batch1 = `02df26ef`**: Assign kEncoding_UTF8 明示(H-9)・SMTP添付パスencodeWord撤去(H-8)・CFSTR_FILENAMEMAPW(M-1)+thread_local除去(M-2)・XML_PARSE_IGNORE_ENC(M-13)・curl型テーブル(M-16)。実証: Shift_JIS宣言XMLのValidate正常化、JS日本語eval正常。
- **Batch2 = `62675de2`**: **boost::filesystem::path::imbue(UTF-8)** で narrow path を UTF-8 統一(H-4/H-5)・tolower UB(H-7)・クリップボードCP932⇔UTF-8+memcpy_s書き越し(H-6)・Sub_LoadStringクランプ(BUF#1)・BEZlib new[]/free(BUF#5)。**実証: BE_FileWriteText/BE_FilePatternCount/BE_FileReadText が日本語パス・日本語内容で完全動作**（従来はWinで全滅するケース）。
- **残り=Batch3候補（設計判断要）**: ConvertTextEncoding の UTF-16フォールバック(H-1/H-10/M-3)・SetResult の埋め込みNUL切断(H-2)・easy_setopt 32bit varargs(M-7)・mac/Linux系(H-3, BUF#2/#3)。詳細はメモリ `be-plugin-audit-fixes`。
- **⚠️ 発見した罠**: unstored計算(q)の評価中に BE_EvaluateJavaScript→BE_Evaluate_FileMaker_Calculation（Evaluate再入）を呼ぶと FMP11 がクラッシュ（プラグインではなく FMP11 の再入制限）。デーモンテストでこの組合せは禁止。
- push は未実施（5c9b150e 以降 7793d1b9/02df26ef/62675de2 の3コミットが未push）。

## このセッションで達成
1. **BE_XPath 根治（SESSION 5 残作業①完了）**: XML スタックを MSVC /MT x86 で静的ビルドしプラグイン本体にリンク（アロケータを libcmt に統一）。
   - **win_iconv 0.0.10**（MSVC 単一ファイル、素の `iconv_*`。BE 本体の iconv 直呼びは GNU ヘッダの `libiconv_*` → belibs 経由のままなので衝突なし）
   - **libxml2 2.9.10**: `win32/configure.js compiler=msvc cruntime=/MT static=yes iconv=yes iso8859x=yes zlib=no` → `nmake libxmla`。機能フラグをバンドル `Headers/libxml/xmlversion.h` と完全一致させた（iso8859x はデフォルト no なので明示必須）
   - **libxslt/libexslt 1.1.34**: configure.js 後に `xsltconfig.h` の `@WITH_PROFILER@` 未置換バグ → `1` にパッチしてから nmake
   - **belibs.lib（import lib）から xml/xslt/exslt/iconv_open 系シンボルを機械除去**（dumpbin /linkermember で静的lib定義シンボルを収集→def から除去→`lib /def` 再生成）。belibs.dll 自体は無変更（Magick 内部用 xml2 は残留）
   - **罠2つ（必読）**: (a) フィルタ def に `LIBRARY belibs.dll` 行が無いと def ファイル名の DLL をインポートする import lib になる（belibs_noxml.dll 事故→修正済）。(b) `Libraries\win32\libcrypto.lib`/`libssl.lib` は belibs.lib のコピーなので再生成のたびに必ずコピーし直す
   - vcxproj（WORK1、`.pre-xmlmsvc.bak` あり）: Release|Win32/PRO Release|Win32 に `LIBXML_STATIC;LIBXSLT_STATIC;LIBEXSLT_STATIC` 追加 + xml2msvc/xsltmsvc/exsltmsvc/winiconv.lib 追加
2. **FMP11 実機で実証（デーモン方式、.fmx 4.76MB 配置済み）**:
   - `BE_XPath` → "hello" / 日本語 "こんにちは世界"（0x3F 化け解消）/ `BE_XPathAll` → 正しい値リスト / 繰り返し評価でもクラッシュなし
   - `BE_XSLT_ApplyInMemory` → 正常変換 / `BE_XMLValidate` → valid=空・invalid=正しいエラーメッセージ
3. ビルド資材は WORK1 `C:\dev\mkxmlmsvc.bat` / `nmxslt.bat` / `mkimplib2.bat` + `filterdef.ps1`。旧 import lib は `belibs.lib.full` に退避。

## 残作業（優先順）
1. **監査 Batch 3（設計判断が要る組、ユーザーと仕様を決めてから実装）**:
   - H-1/H-10/M-3: `ConvertTextEncoding` の UTF-16 フォールバック（BOM 検出化 or 廃止）+ 変換全滅時の明示エラー化（現状: CP932 が UTF-16 誤認で化け、BE_FileWriteText が「0バイト成功」）。**互換性が変わる**（今まで化けて成功していたものがエラーになる）ので方針決めが先。
   - H-2: `SetResult(vector<char>&)` のサイズ引数復活（埋め込み NUL 切断。BE_HTTP_* バイナリレスポンス全経路）。FM の Text が U+0000 を保持できるかの確認込み。
   - M-7: `easy_setopt` の型別ディスパッチ（32bit で CURLOPT_*_LARGE 系の va_list ずれ）。
   - H-3/BUF#2/#3: mac/Linux 系（このフォークの Win32 ビルドでは非実行。上流 PR 価値あり）。
2. **主要機能の実弾検証（デーモンで一括）**: BE_ConvertContainer(ImageMagick)・BE_Cipher*/RSA/AES(OpenSSL)・BE_PDF*(podofo)・BE_FileMakerSQL・BE_JSON_jq。XPath と同じアロケータ境界（belibs 内 malloc→本体 free）が他ライブラリに無いかの洗い出し。※コンテナ系は q（テキスト）では検証しにくいので工夫が要る。
3. **push**: `7793d1b9`(vcxproj) / `02df26ef`(Batch1) / `62675de2`(Batch2) の3コミットが未 push（ユーザー指示があれば）。
4. **リポジトリ整理（一部完了）**: vcxproj は同期・commit 済み。残り = `Libraries/win32` の管理方針（.gitignore かコミットか、ユーザー判断待ち）と WORK1 ビルドスクリプト（mkbelibs.sh/mkxmlmsvc.bat/filterdef.ps1/deploy系.ps1 等）のリポジトリ取り込み判断。
5. **FMP12+ 対応確認・配布**。
6. 注意: belibs.dll を再ビルドしたら ordinal がずれる → belibs.def 再出力→filterdef→belibs.lib 再生成をセットで実行（メモリ `be-plugin-xml-msvc-static` 参照）。

## テストサイクル（次セッションで即使う手順）
1. 式を投入: JSON `{"input":"<FM式>"}` を base64 で WORK1 に置き `curl -s -X POST --data-binary @file http://127.0.0.1:<PORT>/jobs`（mssh 経由。日本語式は python の json.dumps でエスケープすると安全）
2. FMP11 で新規レコード作成: `loophole_menu invoke hwnd=<FileMaker Proウィンドウ> command_id=50157`（1ジョブ=1レコード）
3. 回収: `mssh work1 'curl -s http://127.0.0.1:<PORT>/result/job-N'`
4. **禁止**: BE_EvaluateJavaScript 内での BE_Evaluate_FileMaker_Calculation（FMP11 が再入クラッシュ）。BE_HTTP_GET("/next") を式に含めるテスト（キューの次ジョブを食べる）。

---

# 🟡 SESSION 5 最終状態（2026-07-02 23時頃） — H2解決（belibs.dll隔離）を実証・commit/push済。GUIレスのデーモンテスト基盤を確立。BE_XPathの真因=アロケータ境界と特定。残課題 = BE_XPath根治(libxml2 MSVC化) ※XPath根治は SESSION 6 で完了

**詳細メモリ: `be-plugin-belibs-isolation`（H2解決の全手順）と `be-plugin-daemon-test`（テスト基盤）。**

## このセッションで達成
1. **H2解決（本丸）**: mingw依存スタック(Magick/curl/xml2/xslt/OpenSSL/png/jpeg/freetype/iconv/ssh2)を **1個の `belibs.dll`** に隔離し、プラグイン本体をピュアMSVC化。`/FORCE:MULTIPLE` と `/SAFESEH:NO` を撤去してクリーンリンク成功。`.fmx` 18.5MB→**3.7MB**。**FMP11実機で BE_FileWriteText が「H2FIX-こんにちは」を UTF-8 21バイトで正しく書けた**（従来0バイト）。手順は `C:\dev\mkbelibs.sh`→`mkimplib.bat`→`swap_belibs.ps1`。
2. **GUIレスのデーモンテスト基盤**: `WORK1 C:\dev\fmd11.mjs`(node:http, schtasks常駐, :<PORT>) + 新規 `fmtest.fp7` の unstored計算フィールド `q`(BE_HTTP_GET/POST往復)。`scratchpad/submit.sh "<式>"`投入→FMPで再評価→`curl /result`回収。BE_Version/MessageDigest/HTTP/ExecuteSystemCommand すべて正常動作を実証。
3. **h3クラッシュの切り分け完了・真因=アロケータ境界**: BE_MessageDigest/HTTP/Version=正常。**犯人=BE_XPath**。構造体は2.9.10と2.15.1で全一致→**バージョン不一致仮説は棄却**（libxml2を2.9.10・libxslt/libexsltを1.1.34で再ビルドしてもクラッシュ変わらず）。**真因=belibs.dll分離でlibxml2(belibsのmsvcrtヒープ)とプラグイン本体(libcmtヒープ)のアロケータが別になり、`BEXSLT.cpp`が libxml2 malloc の `xmlChar*` を C++ `delete`(std::unique_ptr<xmlChar>)で解放していた箇所がヒープ破壊**。318/424行を `unique_ptr<xmlChar,void(*)(void*)>(...,xmlFree)` に修正したが**まだクラッシュ＝XPath経路に他の malloc/free 境界越えが残る**。詳細メモリ `be-plugin-belibs-isolation`。

## commit / push 状態
- **push 済み**（`origin/main` = https://github.com/veltrea/BaseElements-Plugin、著者 veltrea・AI表記なし）:
  - `5c9b150e` BE_XPath の xmlChar を xmlFree で解放 + ApplyXPathExpression のエラー処理を libxml2 2.14+ 対応（今回）
  - `59f44b39` BE_ExecuteSystemCommand 再設計 + 32bit Windows リソース修正（SESSION4分、今回まとめて push）
- belibs化した `Libraries/win32` と WORK1 の vcxproj belibs化は **未コミット**（ビルド入力/環境依存。.gitignore か管理方針を要判断）。

## 残作業（優先順）
1. **BE_XPath 根治（本命）**: libxml2/libxslt/libexslt を **MSVC /MT でビルドして belibs.dll から出し、プラグイン本体に静的リンク**（ヒープを libcmt に一致。C ライブラリなので C++ ABI 問題なし）。curl/Magick 等は belibs.dll のまま。iconv/zlib 依存は既存 mingw .a を流用できるか要確認。※`Source/BEXSLT.cpp` 318/424 の xmlFree 修正(commit 5c9b150e)だけでは不十分と実証済み＝XPath経路に他の malloc/free 境界越えが残る。副案(対症): BEXSLT/BEXMLTextReader の全 xmlChar/xmlBuffer を xmlFree/xmlBufferFree に統一（漏れやすい）。
2. **主要機能の実弾検証（デーモンで一括）**: BE_ConvertContainer(ImageMagick=旧A案の画像変換)・BE_Cipher*/RSA/AES(OpenSSL)・BE_PDF*(podofo)・BE_FileMakerSQL・BE_JSON_jq・BE_XSLT_Apply(libxslt)。XPathと同じアロケータ境界が他ライブラリに無いかの洗い出しも兼ねる。
3. **リポジトリ整理**: Mac canonical の `Project/BaseElements.vcxproj` を belibs化版に同期（WORK1のみ swap済、`.pre-belibs.bak`あり）。`Libraries/win32` の .gitignore/コミット判断。
4. **FMP12+ 対応確認・配布**: 単一.fmx が FMP12〜19(32bit)で動くか（テスト機制約）、最終 Releases 配布。

## テスト基盤（次セッションで即再利用）— 詳細メモリ `be-plugin-daemon-test`
- **デーモンは WORK1 で schtasks 常駐**（タスク名 `fmd11`, :<PORT>, 本体 `C:\dev\fmd11.mjs`）。落ちていたら `schtasks /run /tn fmd11`。
- **式の評価**: `scratchpad/submit.sh "<式>"` で投入 → FMP11(`C:\Users\<USER>\Documents\fmtest.fp7`)で **新規レコード作成**(loophole_menu invoke command_id=50157)し unstored計算フィールド `q` を再評価 → `mssh work1 'curl -s http://127.0.0.1:<PORT>/result/job-N'` で回収。※再評価トリガーが不安定なので**新規レコード直後が確実**。ScriptMakerでポーリングスクリプトを作れば安定化（未着手）。
- クラッシュする式は `/result` が `done:false` のまま FMP が DIED → その式が犯人と安全に判別できる。
- belibs一式は WORK1 `C:\dev\BaseElements-Plugin\Libraries\win32`（現状 libxml2 2.9.10 / libxslt 1.1.34 で再ビルド済み）。`.fmx` は FMP11 Extensions、`belibs.dll` は `FileMaker Pro 11\`（exe同階層）に配置済み。配置は <ADMIN>(mssh) 必須。

## cruft
- `.bak` cruft は削除済み。`jptest_x86.fp7` は h3 で開けなくなった（表形式で BE_XPath 評価→クラッシュ）。検証は `fmtest.fp7` で足りる。

---

# 🟢 SESSION 4 最終状態（2026-07-02 20時頃） — BE_ExecuteSystemCommand をゼロ再設計し FMP11 実機で実証・commit 済み。次の本丸は H2（CRT混在で BE_FileWriteText が0バイト書き）

**⚠️ 以下が最新の確定状態。下部の SESSION 3/2 は履歴（ビルド構成・依存ライブラリの記述は有効、"犯人=InitializeMagick"/"登録中断"等の途中推論は棄却済み）。詳細メモリ: `be-plugin-shellexec-rewrite`。**

## このセッションで達成したこと
1. **BE_ExecuteSystemCommand をゼロ再設計** → 新 `Source/BEShellExec.{h,cpp}`（FMWrapper/Poco/boost **非依存**の純Win32）。
   - `cmd.exe /S /C`（POSIX `/bin/sh -c`）でシェルラップ（素の echo 等が動く／再トークン化しない）
   - 出力を **コンソール OEM(CP932)→UTF-16→UTF-8** 変換（**0x3F 化けの根治**）
   - **encoding オプション新設**: 省略時=OEM/CP932、`utf8`/`cp932`/`sjis`/`oem`/`ansi`/`auto`/`utf16` または任意コードページ番号
   - **タイムアウト時 Job オブジェクトで子孫ごと kill**、終了コードを `BE_GetLastError` で取得
   - 旧 `BESystemCommand.{cpp,h}`（Poco::Process + split_winmain）は削除
   - 単体テスト `tests/test_shellexec.cpp`（cl.exe のみ、FileMaker不要）で全ケース検証済
2. **公式3.3.8 の壊れ方を実測確定**（オラクル）: 動くのは「`cmd /c` 明示 かつ 純ASCII出力」だけ。素 echo=0x3F、日本語=0x3F、chcp=空。CP932→UTF-8変換が無いのが真因。
3. **ドイツ語リソース問題を発見・修正**: vcxproj が `BaseElements_en.rc` と `BaseElements_de.rc` の両方をリンクし、日本語Windowsで LoadString がドイツ語を選び、関数がドイツ語名で登録されていた（BE_ExecuteSystemCommand=`BE_SystemBefehlAusfuehren`、BE_FileWriteText=`BE_DateiSchreibeText`）。→ **vcxproj から `_de.rc` を除外**して英語専用化。
4. **FMP11 実機でエンドツーエンド実証**: 計算フィールド `BE_ExecuteSystemCommand ( "echo こんにちは" ; 5000 )` が **英語名で「こんにちは」を正しく表示**。
5. **git commit 済み**（`59f44b39` on main、著者 veltrea、AI表記なし）。コミット前に BEPlugin.cpp の診断コード(be_init.log書き等)を全除去し TerminateMagick を復元。**push は未実施**。

## 現状（次セッションの前提）
- **Mac canonical**: commit 済み・クリーン。未追跡で残: `Libraries/win32`・`Libraries/fm11-sdk`（ビルド入力、コミット対象外）、`Source/BEPluginFunctions.cpp.bak`/`.h.bak`（cruft、削除してよい）。
- **WORK1** (`C:\dev\BaseElements-Plugin`): ソースは Mac とほぼ同期済だが **BEPlugin.cpp にまだ診断コードが残っている**（Mac側のみ除去）。次に WORK1 でビルドするなら Mac の BEPlugin.cpp を転送して同期すること。vcxproj は WORK1 固有の Magick/mingw リンク改変あり（上書き禁止・`_de.rc` 除外は反映済）。
- **FMP11 Extensions**: 現在 **英語版 自前5.0.0** が入っている。公式3.3.8 バックアップ = `C:\dev\official\be338_fmp11_backup.fmx`（戻すなら FMP11終了→コピー）。
- テストDB `C:\Users\<USER>\Documents\jptest_x86.fp7`: 検証用フィールド batt/batt2/jp(独語名で今は無効)/jp2/cmd_in/cmd_out が残存。レイアウト=jptest_x86、表形式。

## 次の本丸（優先順）
1. **H2（CRT混在 /FORCE:MULTIPLE）の恒久解決** ← 最重要。`BE_FileWriteText`(=`BE_DateiSchreibeText`) が **0バイトファイルを書く**（std::ofstream の CRT が mingw_msvcrt と libcmt の混在で機能不全＝ハンドオーバー F4 の実物）。CRT ファイルI/O系が全滅している疑い。**フェーズ3: mingw依存スタック(Magick/curl/xml2/OpenSSL)を1個のmingw DLLに隔離**し、プラグイン本体をピュアMSVC化→`/FORCE:MULTIPLE` を外すのが成功判定。※ BEShellExec 自体は純Win32でCRTファイルI/O非依存なので無傷。
2. WORK1 の BEPlugin.cpp 診断コード同期（Mac の除去済み版を転送）。
3. push（ユーザー指示があれば）。`.bak` cruft 削除。
4. 診断で作った検証フィールド類の掃除（任意）。

## このセッションで確立した実務ノウハウ（再利用）
- **loophole座標 = 論理座標 × 1.5**（物理2560×1440 / 150%スケーリング、論理1707×960）。GetCursorPos で実証。マウス直クリックは不安定なので **FM ダイアログは Win32 直接操作が確実**。
- **FMダイアログ駆動**: `C:\dev\fm_enum.ps1 -Hwnd <h>`（子コントロール列挙）、`C:\dev\fm_drive.ps1`（-EditHwnd で WM_SETTEXT、-ComboHwnd/-ComboIndex/-ParentHwnd/-ComboId でコンボ選択、-ClickHwnd で PostMessage クリック）、`C:\dev\set_formula.ps1 -Hwnd <edit> -File <f>`（ファイルから式を WM_SETTEXT、日本語・複数行OK）。ダイアログのコントロールは `GetDlgItem(dlg, id)` が速い（計算式ダイアログ: 式=id103, 結果combo=id108, OK=id1）。メッセージボックスは class `#32770`・OKボタン=id2。
- **計算フィールドの作り方**: Manage DB(menu id 51154)→フィールド名WM_SETTEXT+タイプcomboを「計算」(index6, WM_COMMAND CBN_SELCHANGE通知必須)→作成→計算式ダイアログ→式セット→結果タイプ「テキスト」(index0)→OK→OK。**新規計算フィールドは表形式ビューに自動で列追加され即評価される**。
- **結果の読み方**: BE_FileWriteText は H2 で0バイト＝**ファイル書き出しで検証できない**。**計算フィールドのセル表示を loophole_screenshot で読む**のが確実（日本語も描画される＝0x3Fなら"?"表示で判別可）。cmd 自身の `> file` リダイレクトは cmd がネイティブ書きするので有効（プラグインのCRT非経由）。
- **MSVC は日本語コメント入りソースに `/utf-8` か UTF-8 BOM 必須**。BE の vcxproj は `/utf-8` 無し → BOM無しUTF-8を CP932 誤読しC2059大量エラー。**BEShellExec.{h,cpp} は UTF-8 BOM 付きで保存**（standalone は cl の /utf-8 で通るが本体ビルドはBOM必須）。
- **PS 5.1 は BOM無しUTF-8 の .ps1 を CP932 誤読** → WORK1 パッチスクリプトは **ASCII のみ**で書く（日本語コメント厳禁）。
- **ビルド**: `cmd /c C:\dev\BaseElements-Plugin\build32.bat`（loophole_run で start /b すると同期実行され `BUILD_SCRIPT_DONE exit=N` が返る、ログ `build32.log`、`=== MSBUILD EXIT=N ===`）。ソース更新後は増分ビルド事故回避に `BEPlugin.obj`/`BEPluginFunctions.obj` を明示削除。**.fmx 配置は <ADMIN>(mssh) 必須**（Extensions への <USER> 書込不可）。FMP11 起動は loophole(<USER>)。
- **関数名の版差・言語差**: BE_WriteTextToFile(3.3.8)→BE_FileWriteText(5.0.0)。ドイツ語ビルドだと BE_SystemBefehlAusfuehren 等。関数名は `Resources/BaseElements_en.rc`(UTF-16, id→"Name ( proto )|keywords|desc")で定義、Windows は LoadStringW で id 引き。
- **BE のパス**: `ParameterAsPath` が `make_preferred()` で区切り正規化するので Windows形式(`C:\...`)そのままOK。

---

# 🟡 SESSION 3 最終結論（2026-07-02 18:10） — 履歴（"犯人=InitializeMagick" は棄却済み。ビルド構成の記述は有効）

**⚠️ このファイル下部の「SESSION 2」および旧「SESSION 3 速報（15:05）」の "真犯人=InitializeMagick" という記述は誤り。下記が最新の確定結論。**

## 経緯（3段階の診断で結論が反転した）
1. **15:05 時点の暫定結論（誤り）**: InitializeMagick/TerminateMagick を無効化したビルドを FMP11 に置いたら一覧に出て BE_Version=5.0.0 が動いた → 「InitializeMagick が犯人」と早計に判断。
2. **計測版（try/catch + SEH + タイムスタンプログ, BEPlugin.cpp に `BEDiagInitMagick` 追加）で再検証**: InitializeMagick を**有効に戻して**実行 → **例外もSEHクラッシュもハングも起きず正常完了**。しかも一覧に出て BE_Version も動いた。「無効化で直る」と矛盾。
3. **高精度計測（QueryPerformanceCounter, GetTickCount の 15.6ms 分解能では"0ms"が誤読を招くため）**: InitializeMagick は **6.65ms で正常完了**。ハングでも遅延でもタイムアウトでもない。→ **InitializeMagick は無罪で確定**。
4. **公式バイナリを正解機にした裏取り（ユーザー提案のブラックボックス方式）**: 公式 3.3.8（32bit・素の InitializeMagick 入り）を同じ FMP11 に配置 → **プラグイン一覧に BaseElements が表示（ユーザーが手動でプラグインタブを開いて目視確認）**、bever 列 = `3.3.8`、BE_WriteTextToFile = `0`（成功）。→ **FMP11 環境は32bitプラグインを正しく扱える。前セッションで自前ビルドが出なかったのは環境のせいでも InitializeMagick の設計のせいでもない。**

## 確定した真因の候補（2つに絞られた。どちらも「自前ビルド固有」）
- **H1: セッション2の Extensions 上の .fmx が転送で壊れていた。** 本セッション中、mssh 経由の base64 転送が**何度も切り詰め/破損**した（20222バイト事故、コマンドライン長超過、SSHハング等）。当時ハッシュ照合していなかった（照合記録は修正後のみ）。FMP11 は LoadLibrary 失敗のプラグインを黙って一覧から消す。→ コードは最初から無罪の可能性。
- **H2: CRT混在（mingw_msvcrt + libcmt を `/FORCE:MULTIPLE`）による「リンクごとのガチャ」。** 決定的証拠 F4: 計測ログ v1 で **fopen/fprintf が黙って機能不全**（ファイルは作られたが0バイト。Win32 API CreateFile/WriteFile に替えたら書けた）。`/FORCE:MULTIPLE` は重複シンボルを「先に見つかった方」で解決するので、obj/lib の並びが変わるたびに fopen と fprintf が別CRTに解決され得る。この機能不全は**現行の成果物に実在が確定**。ビルドのたびにサイコロを振っている。

## ユーザーが決めた次の方針（SESSION 4 でやること）= A + B 両方
「13年分の継ぎ接ぎを直す」のではなく「**この関数がやりたい機能をゼロから設計し直す**」+「公式バイナリを仕様のオラクル（正解機）にするブラックボックス方式」で進める。A→B の順（A が B の検証ハーネスにもなる）。

### フェーズ1: A の仕様確定（公式 3.3.8/5.1 をオラクルに）— ここから開始
- **未解決の観測**: 公式3.3.8で `BE_WriteTextToFile("...";C1&BE_ExecuteSystemCommand("echo hello")&C2&...)` を実行したら、各コマンド出力が全て **`0x3F`（`?`）1バイト**になった（結果ファイル 83バイト、hex確認済み。区切り文字列 `[[C1-echo]]` 等はそのまま、コマンド出力位置だけが `?`）。原因未切り分け。**次の一手**: ファイル書き込み（BE_WriteTextToFile のエンコーディング）を変数から排除するため、`echo hello` 単体を **output フィールドに直接** Evaluate して画面で読む。`?` がエンコーディング化け（CP932→CP_UTF8変換で置換文字0x3F）か、出力空か、バージョン差かを特定。
- 公式で echo / 日本語 / cd / 複合コマンド(`&`) / 存在しないコマンド / タイムアウト の挙動を観測し **あるべき仕様を SPEC 文書化**。マニュアル: `docs/Functions/BE_ExecuteSystemCommand.md`（リポジトリ内にある。「| pipe は動かない」「引用符エスケープが複雑」等の caveat 記載あり）。

### フェーズ2: A のクリーンルーム実装（純Win32・CRT非依存・Poco/boost 依存を捨てる）
- **着手前に `windows-cmd-japanese-encoding` スキルを必ず読む**（CP932/0x5Cダメ文字/CreateProcessW の罠。ユーザーの moo_shell/ZooPlug 知見）。
- 設計: `CreateProcessW`（shell モードは `cmd /c <原文>`、再トークン化しない）+ `CREATE_NO_WINDOW` + 匿名パイプで stdout+stderr 生バイト回収 + **OEM(CP932)→UTF-16→UTF-8 変換** + **Job オブジェクトでタイムアウト時に子ごと kill**（現行はタイムアウトで kill せずスレッドリーク）。CRT を一切使わない。
- テスタビリティ優先（ユーザーの強い好み）: まず `main()` 付き**単体 exe** として WORK1 でビルド → 日本語ファイル名 dir / `ping -n 100`+timeout / 引用符・リダイレクト を単体で通す → 通ったら BE_ExecuteSystemCommand に差し込む。FMX 登録スケルトンは流用（関数単位のクリーンルーム置換）。
- 現行実装の問題（確証・高）: `BESystemCommand.cpp:68-75` が Poco::Pipe 生バイトを変換なしで返す（CP932化け）。`BEPluginFunctions.cpp:4330-4334` がタイムアウトで子を kill しない。shell なのに `split_winmain` で再トークン化。

### フェーズ3: B の設計（mingw 依存スタックの DLL 隔離）
- MagickCore/Wand + curl + libxml2/libxslt + OpenSSL（mingw製C系）を **mingw製の1個のDLL**にまとめ、プラグイン本体はピュアMSVC。CRT を DLL 境界で物理分離。**`/FORCE:MULTIPLE` を外せるか＝キメラ解消の成功判定**。全MSVC化（重い）より軽い正攻法。
- フェーズ2の純Win32シェル実行ハーネスで、隔離後も mingwスタック（画像変換 BE_ConvertContainer・HTTP BE_HTTP_*・暗号 BE_Cipher*）が動くか実弾検証。

## ⚠️ SESSION 3 で入れた診断コードの状態（次セッションで戻す必要あり）
- **Mac canonical (`BaseElements-Plugin/Source/BEPlugin.cpp`)**: `BEDiagLog`/`BEDiagInnerInitMagick`/`BEDiagInitMagick`（QueryPerformanceCounter 版）を追加済み。LoadPlugin 入口・InitializeMagick 前後・return 前にログ呼び出し。**InitializeMagick は有効（診断でラップして呼んでいる）**。`#if 0` 無効化は既に解除済み。
- **WORK1 (`C:\dev\BaseElements-Plugin\Source\BEPlugin.cpp`)**: 同じQPC計測版が入っている（22823バイト、sha256 52f7f2...）。バックアップ: `BEPlugin.cpp.pre-qpc.bak`, `BEPlugin.cpp.pre-diag.bak`。
- **これらの診断コードは最終的に全て撤去する。** ログ出力先 `C:\dev\be_init.log`。診断は役目を終えた（結論確定済み）ので、フェーズ2着手前に Mac 側で除去 → WORK1 同期が綺麗。

## SESSION 3 で確立した実務ノウハウ（次セッションで再利用）
- **権限分離**: ビルド=<ADMIN>(`mssh work1`)、FMP11実行=<USER>(loophole)。`C:\dev` に <USER> 継承フルコントロールを付与済み（`icacls "C:\dev" /grant "<USER>:(OI)(CI)F" /T`、1435ファイル処理済み）。**ただし `C:\Program Files (x86)\...\Extensions` への .fmx 配置は <USER> 権限では不可 → 必ず <ADMIN>(mssh) 経由でコピーする。**
- **MSBuild 増分ビルドが壊れる**: ソース更新後も古い obj でリンクをスキップする事故が頻発。**リビルド前に必ず `BEPlugin.obj` を明示削除**（`Get-ChildItem C:\dev\BaseElements-Plugin\build -Recurse -Filter BEPlugin.obj | Remove-Item`）。obj/fmx の LastWriteTime で再コンパイルされたか必ず確認。
- **ファイル転送**: mssh 経由 base64 直パイプは長さ制限・SSHハングで不安定。**確実なのは loophole_write_file でチャンク（8KB）を WORK1 に置き、PowerShell で結合→FromBase64String→WriteAllBytes**。または小さな差分なら PowerShell の文字列 Replace でパッチ（`qpc_patch.ps1` が実例）。転送後は必ず sha256 照合。
- **FMP11 GUI 操作**: DPI/「ウィンドウ内容を拡大」でマウス/キーが不安定。Win32 直接操作が確実。ヘルパー WORK1 `C:\dev\fm_enum.ps1`(-Hwnd で子ウィンドウ列挙), `C:\dev\fm_drive.ps1`(-EditHwnd/-Text/-ComboHwnd/-ComboIndex/-ComboId/-ParentHwnd/-ClickHwnd), `C:\dev\fm_settext_b64.ps1`(-EditHwnd/-B64 で日本語含む長文を WM_SETTEXT), `C:\dev\recv_b64.ps1`/`recv_append.ps1`(base64受信)。menu: 環境設定=49153, DB管理=51154, 新規レコード=50157, フィールド全置換=50190。**環境設定のプラグインタブへの Ctrl+Tab は不安定→ユーザーが手動でタブを開いてくれる運用にした（AIからは環境設定を開閉しない）。**
- **公式バイナリ**: WORK1 `C:\dev\official\` に配置済み。3.3.8-win32（`be338\BaseElements.fmx`, machine=0x014C=x86, これがFMP11用オラクル）、5.1.0.6-win64（`be51\BaseElements.fmx64`）。DL元: `https://goya.com.au/files/beplugin/3.3.8/BaseElements.fmx.zip` 他（`docs/Downloads.md` 参照）。GitHub Releases の .fmx は ELF(Linux) なので注意。
- **テスト DB**: `C:\Users\<USER>\Documents\jptest_x86.fp7`（result/bever フィールド有、レイアウト jptest_x86）、`zootest.fp7`。plugin-test.fmp12 は FMP19 用。

---

# ⚠️ 以下は SESSION 1〜2 の記録（"真犯人=InitializeMagick" の記述は上記 SESSION 3 で棄却済み。ビルド構成・依存ライブラリ・ノウハウ部分は有効）

# 🟢 SESSION 2 現況サマリ（2026-07-02） — まず読む

## 達成: 32bit `.fmx` のビルドは **完全成功**
`C:\dev\BaseElements-Plugin\build\Win32\Release\BaseElements.fmx`（PE32 i386 DLL, 18.6MB）が `build32.bat` exit=0 で生成。エラー推移 1724→212→20→0(コンパイル)→リンク→依存lib約20個→.fmx。DLLとして正常ロード可・FMExternCallProcエクスポート・リソース正常・`LoadStringW(1)=GyBE1nnYYnn`。詳細メモリ: `be-plugin-32bit-linked`。

**依存ライブラリの最終構成（`C:\dev\BaseElements-Plugin\Libraries\win32` にステージ）:**
- **C系(mingw32 GCC、`.a`→`.lib`リネームでMSVCリンク):** iconv, libxml2, libxslt/libexslt, zlib, libssh2, libpng16, turbojpeg, freetype, curl(`--without-libpsl`), openjpeg
- **OpenSSL: システムmingw 3.6.3に統一**（pacman `/mingw32/lib`をOUT+win32へコピー、BEバンドル`Headers/openssl`も3.6.3に差替。BEの`EVP_CIPHER_iv_length`等は3.xでマクロ→`_get_`版でリンク解決。ユーザー指示で3.6.3統一）
- **C++系(MSVC v145 /MT):** **podofo 0.9.7**(BEバンドルヘッダが0.9.7、1.0.3は非互換API), libde265, heif, Poco 1.10.1, boost 1.75(vc141)。**全て`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`+`CMP0091=NEW`で/MT必須**(でないとLNK2038 CRT不一致)
- **ImageMagick 7.1.1-29 ハイブリッド:** MagickCore/MagickWandをmingw(C)でビルド、`Magick++/lib/*.cpp`だけMSVC /MTで再コンパイル(C++ ABI回避)。MSVC compile時`/FI C:\dev\msvc_shim.h`(ssize_t/__attribute__/__restrict__中和)+`C:\dev\shim_inc\strings.h`シム+magick-baseconfig.hの`MAGICKCORE_HAVE_PTHREAD`無効化。delegateはpng/jpeg/freetypeのみ(`--without-openjp2`等でexotic無効化)
- **mingwランタイム:** libgcc.lib, libmingwex.lib, mingw_msvcrt.lib, libmingw32.lib, libwinpthread.lib(mingw C libのpthread/printf/64bit除算/msvcrt import解決)
- **シム:** wspiapi_shim.lib(openssl socket BIOの`Wspiapi*`/`gai_strerrorA`をMSVCで供給。ソース`C:\dev\wspiapi_shim.c`+`gai_shim.c`)

**vcxproj `Project/BaseElements.vcxproj` の Release|Win32(行537)/PRO Release|Win32(行593) の変更(WORK1で実施済み):** 37個の`CORE_RL_*`削除→`Magick++.lib;MagickWand.lib;MagickCore.lib;jpeg.lib;zlib.lib`+mingwランタイム+wspiapi_shim+boost6+Poco7に置換、`openjp2.lib`削除(ユーザー指示、BE非依存)、preprocessor(行515/571)に`BOOST_ALL_NO_LIB;POCO_NO_AUTOMATIC_LIBS`追加、Link に`ImageHasSafeExceptionHandlers=false`(=/SAFESEH:NO、mingw obj対策)と`/FORCE:MULTIPLE`(mingw_msvcrt vs libcmtのCRT重複=LNK4006警告化)。**注意: これらはWORK1のvcxprojにsed直編集で入れた。Macローカルのvcxprojには未反映(要同期)。バックアップ`BaseElements.vcxproj.pre-magick.bak`あり。**

## 🎯【解決・真因確定】FMP11で一覧に出ない真因 = `Magick::InitializeMagick(NULL)`（2026-07-02）
診断ビルドで **BEPlugin.cpp:98の`Magick::InitializeMagick(NULL)`と:388の`Magick::TerminateMagick()`を無効化**(`/* DIAG-NO-MAGICK */ ;`に置換。バックアップ: WORK1 `C:\dev\BaseElements-Plugin\Source\BEPlugin.cpp.pre-diag.bak`)→ build32.bat再ビルド(exit=0)→ FMP11に配置し検証:
- **FMP11の環境設定>プラグイン一覧に BaseElements が出た**（AutoUpdate/BaseElements/JpRegex/MooPlug/ZooPlug と並んでチェックON）。
- **`BE_Version()` 計算フィールドが `5.0.0` を返した**（jptest_x86.fp7に計算フィールド`bever = BE_Version`を追加して確認）。
→ **真犯人は InitializeMagick で確定**。ImageMagickハイブリッド(mingw MagickCore/Wand + MSVC Magick++ + `/FORCE:MULTIPLE`のCRT混在)の境界で InitializeMagick が Init(LoadPlugin)中にクラッシュし、FMP11がプラグインを一覧に出さなかった。FMP11自体はクラッシュしないため「静かに一覧から外す」挙動だった。

**8個のFM_* SDK関数不足は原因では「なかった」（訂正確定）:** InitializeMagick無効化だけで動作したことが裏付け。版分岐済み(`BEFileMakerPlugin.cpp:85`が`>=k150`, `BEPlugin.cpp:346`が`>=k160`ガード)で、delay-loadは呼び出し時解決のため8関数は未対処のまま動いた。

**恒久修正の次アクション（優先順）:**
1. **ImageMagick初期化のクラッシュを恒久修正。** (a)ImageMagickを全MSVCビルドし直しCRT境界解消(本命だが重い)、(b)Magick++のCRT境界精査、(c)**InitializeMagickを遅延初期化**(画像関数の初回呼び出し時に実行)に変更しInit時クラッシュ回避 — (c)が最も低コストで実動に近い。
2. 恒久修正後、診断パッチ(`/* DIAG-NO-MAGICK */`)を`.pre-diag.bak`から元に戻す。
3. FMP11で画像変換(BE_ConvertContainer)・暗号・SQL・PDF等の主要機能テスト。8関数の無分岐使用箇所を`extnVersion`ガードで囲むのは低優先(呼ばなければ問題なし)。

**loophole GUI操作の教訓:** FMP11ダイアログはDPI/「ウィンドウ内容を拡大」設定でマウス・キー入力が届かないことが多発。Win32 API直接操作が確実 — `WM_SETTEXT`でフィールド入力, `WM_LBUTTONDOWN/UP`のPostMessageでボタン押下, `CB_SETCURSEL`+親`WM_COMMAND`でコンボ選択。ヘルパー: WORK1 `C:\dev\fm_enum.ps1`(-Hwnd で子ウィンドウ列挙/id/座標), `C:\dev\fm_drive.ps1`(-EditHwnd/-Text/-ComboHwnd/-ComboIndex/-ComboId/-ParentHwnd/-ClickHwnd)。menu barは`loophole_menu invoke command_id=`が確実(環境設定=49153, DB管理=51154)。

## ユーザーの「FMP11用と12+用の2プラグインが必要か」への回答（合意済み）
BEソースは版分岐で**単一バイナリがFMP11〜現行を跨ぐ設計**（`k150`/`k160`ガードが証拠）。よって**単一.fmxで両対応が意図**、2プラグインは不要の見込み。方針合意: **B（4〜8の再実装＝容易）を先に入れ、A（ImageMagick）はじっくり。AはInitializeMagickを一時コメントアウトすればロード可能な状態を作れる**（ユーザーの核心指摘）。

## FMP11で欠落する8関数の分類と対応（`dumpbin`比較で確定）
プラグインがFMWrapper.dllからdelay-load importする77関数のうち8個がFMP11の2010年版FMWrapper.dllに無い。**全てInit経路では呼ばれない**ことを総点検で確認済み（下記A参照）＝ロード失敗の直接原因ではない。

**(1〜3) 対応不可・不要（純粋なSDK世代差、既に版ガード済み）:**
- `FM_ExprEnv_RegisterExternalFunctionEx` … 関数登録の拡張版。`BEFileMakerPlugin.cpp:87`で`extnVersion>=k150`ガード、FMP11は非Ex`RegisterExternalFunction`にフォールバック済み。
- `FM_ExprEnv_RegisterScriptStep` / `FM_ExprEnv_UnRegisterScriptStep` … プラグインスクリプトステップ(FMP16+概念)。`BEPlugin.cpp:346`で`>=k160`ガード、FMP11は`RegisterHiddenFunction(BE_NotImplemented)`。

**(4〜8) 「書き方を工夫すれば同等機能を実装可能」＝FMP11に代替APIが実在（確認済み）:**
- **#7 `FM_ExprEnv_EvaluateGetFunction`** (`BEPluginUtilities.cpp:966,1130,1142` = 一時パス/FileName/AllowAbortState取得) → FMP11に`FM_ExprEnv_Evaluate`が有る。`Evaluate("Get(TemporaryPath)")`等で置換。**最も容易**。
- **#6 `FM_ExprEnv_ExecuteFileSQLTextResult`** (`BESQLCommand.cpp:66` = BE_FileMakerSQL) → FMP11に`FM_ExprEnv_ExecuteFileSQL`と`FM_ExprEnv_ExecuteSQL`が有る。旧SQL APIにフォールバック（署名差の吸収要）。
- **#8 `FM_BinaryData_Constructor3`**(`BinaryData(const Text& name, uint32 amount, void* buffer)` = ファイル名付きバイナリ生成、`BEPluginUtilities.cpp:208`のファイル読込→コンテナ) → FMP11に`Constructor1`/`Constructor2`が有る。空構築＋Add系（`FM_BinaryData_Add`/`AddFNAMData`/`AddSIZEData`はFMP11に有る）で再構築。
- **#4,#5 `FM_ExprEnv_SessionID`/`FM_ExprEnv_FileID`** (`BEDebugInformation.cpp:96,97` = 診断関数BE_DebugInformationのみ) → FMP11に直接代替APIは無い。診断専用なので空/"N/A"を返すか`Evaluate("Get(FileName)")`で代用（スタブで十分）。
- 実装は全て`if (gFMX_ExternCallPtr->extnVersion >= k1xxExtnVersion) { 新API } else { FMP11フォールバック }`。delay-loadは呼出時解決なのでFMP11で新関数が呼ばれなければクラッシュしない（1〜3と同じ手法）。

## Init(LoadPlugin)経路の総点検結果（重要）
`LoadPlugin`(BEPlugin.cpp:84〜378)がInit時に行うのは: ①`InitialiseForPlatform`(クリップボード形式登録・_set_fmode、安全) ②`InitialiseLibXSLT`(xmlInit/xsltInit系、安全) ③`new BEFileMakerPlugin`(QuadChar、FMP11有) ④**`Magick::InitializeMagick(NULL)`(:98)** ⑤`RegisterFunction`×多数(版ガード済み) ⑥`return kCurrentExtnVersion`。**8関数のいずれもInit経路で呼ばれない**（grep確認済み）。→ FMP11ロード失敗の真犯人は **④ InitializeMagick が最有力**（ImageMagickハイブリッドのCRT境界）。

## 次アクション（FMP11対応、優先順）— 合意済みプラン
1. **【最優先・最安の検証】ImageMagick一時無効化でロード確認:** WORK1の`Source/BEPlugin.cpp:98`の`Magick::InitializeMagick(NULL);`を`#if 0`/コメントアウト（必要なら:388の`TerminateMagick`も）→`build32.bat`再ビルド→FMP11のExtensionsに配置→FMP11起動→環境設定>プラグイン一覧に**BaseElementsが出るか**確認。**出れば犯人=InitializeMagick確定**。ソース編集はMac canonical(`/Volumes/2TB_USB/dev/filemaker-plugin/BaseElements-Plugin/BaseElements-Plugin/Source/`)を編集→WORK1(`C:\dev\BaseElements-Plugin\Source\`)へ転送→リビルド、が清潔（WORK1直接sedでも可）。
2. **B（4〜8のFMP11フォールバック実装）:** 上記表の通り版分岐で実装。EvaluateGetFunction→Evaluate、SQL→ExecuteFileSQL、BinaryData→Constructor1+Add、SessionID/FileID→スタブ。容易な#7#4#5から。
3. **A（ImageMagick本対応、要トライ&エラー）:** InitializeMagickが犯人と確定後、(a)ImageMagickを全MSVCビルドし直す(重い/最も正攻法)、(b)Magick++のCRT境界(mingw MagickCore msvcrt vs MSVC libcmt)を精査、(c)遅延初期化化 等を検討。当面は無効化のまま画像変換以外を先にリリース可能。
4. **実動テスト:** ロード確認後、`plugin-test` DBで暗号(BE_Cipher*/RSA, OpenSSL 3.6.3)・SQL・PDF(podofo 0.9.7)・XML/XSLT等をEvaluate。画像変換(BE_ConvertContainer)はA対応後。
5. **テスト環境の制約:** WORK1はFMP11(32bit)とFMP19(64bit・32bitプラグイン不可)のみ。単一.fmxがFMP12+で動くか確認には32bit FMP12〜14/16のインストールが要る。

## 未実施の同期作業（忘れず）
- **vcxprojの変更はWORK1のみ**(`Project/BaseElements.vcxproj`、sed直編集)。Mac canonicalへ未反映。git commit前に同期要（バックアップ`.pre-magick.bak`あり）。
- Source .cppの今後の編集もMac↔WORK1同期に注意。git author必ず`-c user.name="veltrea" -c user.email="<GIT_EMAIL>"`。

## 実動テスト手順(loophole=<USER>対話セッション、WORK1)
- `.fmx`配置: `C:\Program Files (x86)\FileMaker\FileMaker Pro 11\Extensions\BaseElements.fmx`(mssh cpで配置可)
- FMP11起動: `loophole_run(["cmd","/c","start","","C:\\Program Files (x86)\\FileMaker\\FileMaker Pro 11\\FileMaker Pro.exe"])`
- `plugin-test` DBが自動で開く。**スクリプト: Paste to input[id=32768]/Evaluate[id=32769]/Copy from output[id=32770]**。input欄に計算式を入れEvaluateするとoutputにEvaluate結果。
- 機能テスト: `loophole_clipboard_set("計算式")`→inputフィールドをクリック→`ctrl+a ctrl+v`→フィールド外クリックで確定→`loophole_menu invoke hwnd=<plugin-test> command_id=32769`(Evaluate)→output読む。BE関数は`BE_Version`, `BE_GetLastError`等。
- プラグイン一覧確認: 環境設定(`loophole_menu invoke command_id=49153`)→ダイアログでCtrl+Tab×3で「プラグイン」タブ(DPIでマウスクリックがずれるためキーボード推奨)。
- 自己署名(未実施): `New-SelfSignedCertificate`はmssh(<ADMIN>ネットワークログオン)ではNTE_PERMで不可→loophole(<USER>対話セッション)で実行要。ただしFMP11ロードに署名は不要。

## 診断ツール(WORK1 `C:\dev` に配置済み)
- `fmxharness.exe`/`.cpp`: FMExternCallProc直接呼び出しハーネス。`build_harness.bat`でビルド。
- `dumpbin /imports fmx` / `dumpbin /exports FMWrapper.dll` の比較で8関数不足を特定した(`fmx_imports.txt`/`wrap_exports.txt`)。
- 32bit LoadLibrary/LoadStringWテスト: `C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe`(64bit PSでは32bit DLL不可)。

---

## 目的
`veltrea/BaseElements-Plugin`（GoyaPtyLtd のフォーク）を **Windows 32bit (`.fmx`)** で復活させる。本体コードのコンパイルは既に0エラーで通っており、**残りは依存サードパーティ・ライブラリ約22個を x86 でビルドしてリンクを通すこと**。

## なぜ本家が32bitを捨てたか（調査済み・確定）
git 履歴 `e1fe08f9`/`089b0c77`/`76052775`（2016-10, v3.3→3.3.1）で「Removing support for 32-bit builds」。理由の明記は無し＝Goya の自主判断。動機は依存ライブラリ約50個を x86/x64 二重維持するコスト（Claris の64bit強制より前）。コード上の「32bit化石」を2つ発見・修正済み（下記）。

## 既に適用済みの修正（ローカル Mac と WORK1 の両方に反映済み）
1. `Source/BEPluginGlobalDefines.h:39` … `#elif defined _WIN64` → **`_WIN32`**（`_WIN64`は64bit専用。32bitが全部 `#else "Unknown compiler"` に落ちていた）。同66行 `error`→`#error`。
2. `Source/BEPluginFunctions.h` と `.cpp` … プラグイン関数約123個を **`FMX_PROC(fmx::errcode)`** で修飾（SDKの`ExtPluginType`は Windows で `__stdcall`、BE は素の`__cdecl`。x64は同一だが x86 で型不一致 C2664）。正規表現 `^fmx::errcode (BE_\w+\s*\(\s*(?:const\s+)?short)` で変換。WORK1 には `C:\dev\BaseElements-Plugin\fixconv2.ps1` で適用済み。
3. `Source/BEPluginFunctions.cpp` … `#include <chrono>` 追加（x64では推移的に入っていた）。
4. `Project/BaseElements.vcxproj` と `.sln` … **Win32 構成4種**（Debug/PRO Debug/Release/PRO Release）追加。`Libraries\win32` を参照、出力 `.fmx`、v143ピンだがビルド時 `/p:PlatformToolset=v145` で上書き。

エラー推移: 1724 → (_WIN32) 212 → (FMX_PROC) 20 → (const short + chrono) **0(コンパイル)** → リンク段階。

## ビルド環境（WORK1 = Windows 11、`mssh work1` でSSH）
- プラグインclone: `C:\dev\BaseElements-Plugin`（`GIT_LFS_SKIP_SMUDGE=1` でclone）
- ビルド: `cmd /c C:\dev\BaseElements-Plugin\build32.bat`（VSLANG=1033、vcvarsall x64_x86、`msbuild ... /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v145`、ログ `build32.log`）。**cl.exe の診断は VSLANG 無視で CP932 日本語のまま**＝ASCII部分（パス・エラーコード・識別子）だけ読む。
- VS: Visual Studio 18(VS2026) Community。ツールセットは **v141/v145 のみ（v143なし）**。
- MSVC は 32bit ターゲット可（`vcvarsall x64_x86`）。

## 依存ライブラリ・ツールチェーン（重要な確定事項）
- 上流 `GoyaPtyLtd/BaseElements-Plugin-Libraries`（Mac ref: `/Volumes/2TB_USB/dev/filemaker-plugin/BE-Libraries-ref`、WORK1: `C:\dev\BE-Libraries`）の **Windows 対応は未完成**（`_build_common.sh` と全 per-lib スクリプトに Windows 分岐が無い。CI `windows.yml` は CLANG64 前提の書きかけ）。→ 上流スクリプトは流用せず、**バージョンと configure フラグだけ拝借**して自前ビルドするのが速い。
- **MSYS2 は CLANG32/i686-clang を2024年に廃止**。32bit は **MINGW32 = `mingw-w64-i686-gcc`（GCC 16.1、pacmanで入手可、WORK1にインストール済み）** を使う。
- **【実証済み・最重要】MINGW32 GCC の `.a` を `.lib` にリネームすると MSVC v145 の32bitプラグインに素直にリンクできる**（x86はGCCもMSVCもcdecl C symbol が先頭`_`で一致。libgcc未解決も形式拒否も無し）。iconv で確認済み。
- **GCC 16 は C23デフォルトで古いCソースを弾く → `CFLAGS` に必ず `-std=gnu17` を付ける**（libiconv の `extern size_t mbrtowc ();` 等が C2143 相当で死ぬのを回避）。

### 標準レシピ（autotools 系）
WORK1 で `C:\msys64\usr\bin\bash.exe -l <script>`、スクリプト冒頭:
```
export MSYSTEM=MINGW32; export PATH=/mingw32/bin:/usr/bin:$PATH
WORK=/c/dev/lib32-work; OUT="$WORK/out"; WIN32=/c/dev/BaseElements-Plugin/Libraries/win32
```
各libは `./configure --host=i686-w64-mingw32 --disable-shared --enable-static CC=i686-w64-mingw32-gcc CFLAGS="-O2 -std=gnu17 -I$OUT/include" LDFLAGS="-L$OUT/lib" --prefix="$OUT"` → `make -j4` → `make install` → 出力 `.a` を下表の `.lib` 名で `$WIN32` にコピー。先に作った lib は `$OUT` に溜まり後続が参照。

## 【最重要】ライブラリは C系(mingw) と C++系(MSVC) の2系統に分ける（2026-07-01 判明）
- **C言語ライブラリ**: mingw32 GCC の `.a` を `.lib` リネームで MSVC に直接リンク可（cdecl・先頭`_`一致、実証済み）。
- **C++ライブラリ (podofo/Poco/boost/ImageMagick(Magick++)/libheif/libde265)**: mingw-gcc と MSVC は **C++ ABI 非互換**（mangling・STL・例外機構）。→ **MSVC v145 (x86) でビルド必須**。詳細は memory `be-plugin-cpp-abi-msvc`。
- MSVC ビルド環境: cmake=`C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`、ninja=同 `\Ninja\ninja.exe`。`vcvarsall x64_x86`(=14.50/v145) or `x86 -vcvars_ver=14.16`(=vc141)。C++ライブラリの依存C系(freetype/openssl/xml2/zlib)は既存mingwビルドの`.a`を手動パス指定で流用可。
- **MSVCビルド用batは必ずCRLFで転送**（LFだと`^`行継続が壊れ大量の「1」出力になる）。`perl -pe 's/\r?\n/\r\n/g' bat|base64|mssh...WriteAllBytes`。

## 進捗（2026-07-01 大幅前進。リンクは「最初に欠けた lib」で停止＝進捗インジケータ）
**ビルド済み(18) — `C:\dev\BaseElements-Plugin\Libraries\win32` にステージ:**
- C系(mingw): iconv.lib, libxml2.lib, libxslt_a.lib/libexslt_a.lib, libz.a, libcrypto_static.lib/libssl_static.lib, libssh2.lib, libcurl.lib, libpng16.lib, turbojpeg-static.lib, freetype.lib, openjp2.lib
- C++系(MSVC v145): podofo.lib, libde265.lib, heif.lib
- C++系(MSVC vc141): boost×6 (`libboost_{chrono,date_time,filesystem,program_options,regex,thread}-vc141-mt-s-x32-1_75.lib`), Poco×8 (`Poco{Foundation,Crypto,JSON,JWT,Net,Util,XML,Zip}mt.lib`)

**openjp2 は plugin 本体が直接参照しないため vcxproj の AdditionalDependencies から削除済み**（ImageMagick delegate 経由のみ）。ユーザー指示。WORK1 vcxproj の Release|Win32/PRO Release|Win32 から除去済み（Debug構成には残存＝ビルド未使用なので無害）。

**各ライブラリのビルド詳細（configureフラグ・ハマり所）:**
- libxslt 1.1.43: autotools。`--without-python --without-crypto --with-libxml-prefix=$OUT`
- zlib 1.3.1: `make -f win32/Makefile.gcc PREFIX=i686-w64-mingw32- AR=ar` (AR明示必須、`i686-w64-mingw32-ar`無い)
- openssl 3.2.1: `perl Configure mingw no-shared no-docs no-tests no-apps`
- libssh2 1.11.1: autotools `--with-crypto=openssl --without-libz`。example/がリンク失敗するが `src/.libs/libssh2.a` は生成済→`make install`で回収
- curl 8.7.1: autotools。GCC16のため`CFLAGS`に`-Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration`必須(ioctlsocket検出)。`--without-nghttp2 --without-zlib --without-libidn2 --without-brotli --without-zstd --disable-ldap`
- libpng 1.6.43 / libjpeg-turbo 3.0.3(`-DWITH_SIMD=OFF` nasm無し) / freetype 2.13.2 / openjpeg 2.5.4: mingw cmake。**全cmakeビルド共通で `-DCMAKE_SYSTEM_PROCESSOR=x86 -DCMAKE_POLICY_VERSION_MINIMUM=3.5` 必須**(cmake 4.x + 古CMakeLists対策)。png実名は`libpng16.a`
- libde265 1.0.16 / libheif 1.20.2: **MSVC** cmake(`-DBUILD_SHARED_LIBS=OFF`)。heifは`-DWITH_*=OFF`で codec 全無効化+`-DLIBDE265_INCLUDE_DIR/LIBRARY`明示、de265ヘッダを`$OUT/include/libde265/`へ手動コピー
- podofo 1.0.3: **MSVC** cmake。`-DPODOFO_BUILD_STATIC=ON -DPODOFO_BUILD_TEST=OFF`、依存(zlib/freetype/openssl/libxml2/jpeg/png)を`.a`フルパス明示。example exeは失敗するが`target/podofo.lib`(45MB combined)生成
- boost 1.75.0: b2エンジンは**mingw gccでbootstrap**(`./bootstrap.sh --with-toolset=gcc`)、libは`toolset=msvc-14.1`(user-config.jamでcl明示)。bat内で`PATH末尾に C:\msys64\mingw32\bin`追加(b2.exeのmingw DLL解決)。`link=static runtime-link=static`(=/MT、BEのlibcmt.libに一致)。auto-linkはv145とtoolsetタグ不一致→**要 BOOST_ALL_NO_LIB + 明示deps**(未実施)
- Poco 1.10.1: **MSVC** cmake。`-DPOCO_MT=ON`(/MT + `Poco*mt.lib`命名、x64一致)。OpenSSL3非互換で`Crypto/src/RSACipherImpl.cpp:54`の`RSA_SSLV23_PADDING`を`2`に置換パッチ。FindOpenSSLが`.lib`要求→`$OUT/lib/`に`libcrypto.lib`/`libssl.lib`(=.aコピー)作成+`-DOPENSSL_ROOT_DIR`。Poco auto-linkは名前一致するので無効化不要

**残り: ImageMagick のみ（hybrid minimal 進行中）**
- BE の Magick++ 使用は `Magick::Blob`→`Image`→`Blob` の画像変換1機能のみ(BEPluginFunctions.cpp:4622付近, BEPlugin.cpp init/term)。
- 37 delegate 全MSVCビルドは cairo/pango/glib/harfbuzz/librsvg(Rust) 等で非現実的 → **mingw で MagickCore/Wand+coders+delegate(png/jpeg/freetype/openjp2/zlib/xml2) をビルド(C, MSVCリンク可)、Magick++ だけ MSVC 再コンパイル** する hybrid。
- ImageMagick 7.1.1-29 を `--with-quantum-depth=16 --enable-hdri` (BEヘッダに一致) で mingw ビルド中。install で `$OUT/include/ImageMagick-7` にヘッダ、`$OUT/lib/libMagick{Core,Wand}-7.Q16HDRI.a`, `libMagick++-7.Q16HDRI.a` 生成。
- **次**: (1) `Magick++/lib/*.cpp` を MSVC で `$OUT/include/ImageMagick-7` ヘッダに対しコンパイル→Magick++静的lib。(2) vcxproj Win32 の 37 CORE_RL_* を実ビルドした lib 名(MagickCore/Wand/Magick++ + png/jpeg/freetype/openjp2/zlib)に**書き換え** + `BOOST_ALL_NO_LIB` 追加 + boost 6明示deps追加。(3) plugin の Win32 IncludePath を `Headers\ImageMagick-7-win64` から自前ビルドヘッダ(`$OUT/include/ImageMagick-7`)へ変更(ヘッダ/lib のバージョン整合)。
- duktape は不要(`Source/duktape/duktape.c`として直接コンパイル済)。xerces はソース未使用で無視可。

## ファイル転送の罠（WORK1、mssh に scp 無し）
- base64 を mssh stdin 経由 → PowerShell `[IO.File]::WriteAllBytes`。ヘルパー `scratchpad/xfer.sh`。
- **mssh 出力を `| head` に通すと SIGPIPE で転送が中断する**。転送コマンドは head/tail に通さない。
- **base64 が ~150KB 超で ssh PTY がデッドロック**。大きい/バイナリは gzip 圧縮してから（`gzip -c f|base64|mssh ... ungz.ps1 -Out <path>`）。ヘルパー `scratchpad/ungz.ps1`。

## 直近の次アクション（これを実行）
ネットワーク/暗号スタックを1ドライバで: **zlib→openssl→libssh2→curl** を上表の設定でビルドし `$WIN32` にステージ → `build32.bat` 再実行 → リンクが libxslt/その次へ進むか確認。並行で libxslt(libexslt) も。**注意: heredoc で複数行 configure を書くと `\` 行継続が剥がれる事故が起きた**ので、configure は1行に畳むかヒアドキュメントを避けてスクリプトを直接 Write すること。以後 lib を1つ通すたびに `build32.bat` で relink し、次に欠ける lib を潰す。最後に全 lib 揃ったら `.fmx` 生成 → ad-hoc 署名不要（Windows）→ FileMaker 実機ロード確認。

## メモリ
`~/.claude/projects/-Volumes-2TB-USB-dev-filemaker-plugin-BaseElements-Plugin/memory/` に `be-plugin-32bit-fork` / `be-plugin-work1-build` / `be-plugin-32bit-remaining-libs` 保存済み。
