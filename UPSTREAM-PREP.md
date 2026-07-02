# 上流還元の準備状況（2026-07-03, SESSION 10）

上流 = GoyaPtyLtd/BaseElements-Plugin（`upstream/main` = `2f204a1b`）。
このフォークの merge-base は upstream/main の HEAD そのもの（分岐後の上流コミットは 0）なので、
PR ブランチは upstream/main 直上に作成済み。**PR / issue の提出はまだ行っていない（ユーザー確認待ち）。**

## 重要な訂正: 「PRO 限定登録 = バグ」説は誤りだった

上流 `CHANGE_LOG`（v5.0.0, 2024-03-06 相当のエントリ）に **"Pro Version Only"** として
BE_BackgroundTaskAdd / BE_BackgroundTaskList / BE_ContainerConvertImage / BE_SMTPServer の
第5引数が明記されている。つまり `#if BEP_PRO_VERSION` の登録ガードは**意図的な製品区分**であり、
登録漏れバグではない。**「PRO 限定登録解除」の上流 PR は撤回**（有償機能の無償化要求になり受理されない）。
フォーク側の解除はフォークの自由なのでそのまま維持。

- 裏付け: 上流で BEP_PRO_VERSION が実装コードをガードするのは BE_VersionPro の true/false と
  デバッグ情報のみ。関数本体は非 PRO でもコンパイル・リンクされ、登録だけがガードされる。
- BE_VersionPro という判別関数が存在する（= 製品区分が API 仕様の一部）。

## LIBICONV_PLUG 知見: 上流には非該当（issue 不要）

上流 x64 Windows は libiconv を `Project/Extras/libiconv/iconv.vcxproj`（StaticLibrary, /MT）で
自前ビルドし `Libraries/win64/iconv.lib` として**静的リンク**している。CRT 境界が存在しないため
errno 境界問題（フォークの belibs.dll = mingw DLL 構成で発生）は上流では起こらない。
フォーク固有の知見としてメモリ `be-plugin-belibs-isolation` に記録済みのまま維持。

## 作成済みブランチ（origin に push 済み、PR は未提出）

### 1. `upstream-pr/backgroundtask-thread-safety`（2 コミット）

上流の PRO コードに現存する実バグ群。フォーク Batch 4（9031d64b）の再設計を
FMP11 ゲート抜きで上流形に移植したもの。

- コミット 1 "Fix BE_BackgroundTaskAdd worker thread safety":
  - `&environment` 参照キャプチャ（呼び出し元 return 後にダングリング → UB）
  - detached ワーカースレッドからの ExecuteFileSQL（FMX API はスレッド安全でない）
  - detached lambda から例外が漏れると std::terminate = 0xc0000409 でホスト死
  - `###RESULT###` の SQL 文字列リテラル無エスケープ埋め込み（`'` を `''` に）
  - `g_completed_background_tasks` へのワーカー/メイン同時 push_back（データ競合）
  - 解決: ワーカーは素データのみ → mutex 付きキュー → **idle ハンドラがメインスレッドで
    刈り取り**（g_ddl_command と同じパターン）。queue/execute は BESQLCommand.{h,cpp} に追加。
- コミット 2 "Hold a libcurl global-init reference for the plug-in lifetime":
  - `~BECurl` はインスタンスごとに `curl_global_cleanup()` を呼ぶ。ワーカースレッド上の
    デストラクトで refcount が 0 になると libcurl 全体の teardown が別スレッドで走る。
  - LoadPlugin/UnloadPlugin で参照を 1 本保持（AcquireCurlGlobalReference /
    ReleaseCurlGlobalReference、Net/BECurl.{h,cpp}）。
- 検証状態: フォークでは同型の修正を FMP11 実機で回帰済み。**上流形（idle 刈り取り）は
  未ビルド・未実機**（x64 ビルド環境が要る）。PR 本文にその旨を明記すること。

### 2. `upstream-pr/smtpserver-duplicate-registration`（1 コミット）

`df719a6a`（2022-03-03 "Pro Version Targets"）が kBE_SMTPServer 登録を
`#ifdef BEP_PRO_VERSION`（PRO=1,5 / 非PRO=1,4）で包んだ際、**直後の無条件 1,5 登録の
削除を忘れた**。同一 id の 2 回目登録は失敗するので現状は死にコード（先勝ちで意図どおり動く）だが、
PRO ゲートを分かりにくくし、登録順が変われば silent にゲートが無効化される。→ 残骸 1 行を削除。

### 3. `upstream-pr/loadstring-nonbmp-fixes`（1 コミット）

監査 H-3 + BUF#2 + BUF#3（mac/iOS/Linux。フォークは Win32 ビルドなので実害なし＝上流向けに新規実装）。

- `ParameterAsWideString`（BEPluginUtilities.cpp）: UTF-16→UTF-32 の単純キャストで
  非 BMP 文字（絵文字、U+20BB7 等の JIS X 0213 非 BMP 漢字）が孤立サロゲート 2 個になる
  → サロゲートペア合成。ParameterAsPath の下請け = mac/Linux の全ファイルパスに影響。
  ついでに例外時リーク解消（vector 化）と長さベース assign（U+0000 保持）。
- mac `Sub_LoadString`（BEAppleFunctionsCommon.mm）: `intoHereMax` を無視してコピー
  → クランプ追加。
- Linux `Sub_LoadString`（BELinuxFunctions.cpp）: 同上 + 終端 NUL の位置が UTF-8 バイト長
  （UTF-16 単位数ではない）→ 実書き込み単位数に修正。
- 検証状態: サロゲート合成ループは standalone テストで 9 ケース全合格
  （BMP/U+20BB7/U+1F600/孤立 lead/孤立 trail/末尾 lead/境界 U+10000・U+10FFFF/埋め込み NUL/空）。
  mac .mm と BEPluginUtilities.cpp は clang -fsyntax-only 通過（mac defines）。
  Linux 版は macOS 上で構文チェック不可（API 型整合は手動確認済み）。**3 プラットフォームとも
  未実ビルド**。BUF#4（Linux gethostname の NUL 終端）は同ファイルだが今回のスコープ外
  （フォローアップ候補）。

## issue 草稿: FMP11 idle-ExecuteFileSQL クラッシュ

`BUGREPORT-fmp11-idle-executefilesql.md`（repo ルート、英語）が提出可能な状態。
上流の関連コード行（BEPlugin.cpp kFMXT_Idle / BESQLCommand::execute 無引数オーバーロード）への
参照を追記済み。PRO 登録の誤った記述は削除済み。

- 未確定点: **現行 FM で再現するか**。WORK1 の FMP19 は 64bit で 32bit .fmx をロードできないため、
  検証するなら x64 ビルドが必要（ブランチ 1 の実機検証と兼用できる）。
- 上流は idle DDL リプレイを何年も出荷しており FM13+ では恐らく無害。issue は
  「古いホストで致命的 + 新しい FM でも要確認」という現在の書きぶりのままで提出可能。

## 方針決定（2026-07-03）: 上流への PR は出さない

**Claude は上流（GoyaPtyLtd）への pull request を作成・提出しない**（`~/.claude/CLAUDE.md` の
全プロジェクト共通ルールとして恒久化済み）。upstream-pr/* ブランチ 3 本と BUGREPORT は
**リポジトリ内のドラフト・記録として保管**する。外部に出すかどうか（issue を含む）は
100% ユーザーが決めることで、Claude からは提出も催促もしない。

## ユーザー判断待ち（次セッションで確認）

1. **x64 ビルドをやるか**: backgroundtask ブランチの実機検証 + FMP19 での idle-SQL 再現確認の
   両方に効く。WORK1 に FMP19 あり。vcxproj は x64 構成を持っている（上流の標準構成）ので、
   Libraries/win64 の取得（上流は git LFS）から。提出とは無関係にフォークの品質確認として価値あり。
2. Libraries/win32・fm11-sdk の管理方針（従来からの持ち越し）。
