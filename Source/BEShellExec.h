// BEShellExec.h
//
// システムコマンド実行の純粋ロジック層。
// FileMaker SDK (FMWrapper)・Poco・boost に一切依存しないので、FileMaker を
// 起動せずに単体 exe としてビルド・実行・テストできる（tests/test_shellexec.cpp）。
//
// Windows: CreateProcessW + 匿名パイプで生バイトを捕捉し、指定コードページ
//          (既定 = コンソール OEM = 日本語なら CP932) → UTF-16 → UTF-8 に変換する。
//          タイムアウト時は Job オブジェクトで子孫プロセスごと強制終了する。
// POSIX  : posix_spawn(p) で起動し、出力は UTF-8 とみなす。
//
// 旧実装 (Poco::Process + boost::program_options::split_winmain) は
//   (1) コマンドを再トークン化して引用符・リダイレクトを壊す
//   (2) 出力バイトを無変換で返すため CP932 出力が不正 UTF-8 になり 0x3F 化けする
//   (3) タイムアウトで子プロセスを kill せずスレッドをリークする
// という 3 つの不具合があった。本モジュールはこれらを設計段階で解消する。

#ifndef BE_SHELL_EXEC_H
#define BE_SHELL_EXEC_H

#include <string>

namespace be_shell {

// コマンド実行結果。
struct ShellResult {
	bool started = false;         // プロセスを起動できたか（実行ファイル不在等は false）
	bool timed_out = false;       // タイムアウトで強制終了したか
	unsigned long exit_code = 0;  // 子プロセスの終了コード（timed_out / !started 時は不定）
	std::string output_utf8;      // 標準出力＋標準エラーをデコード＋改行正規化した UTF-8
};

// タイムアウト値の特別扱い。BE の従来仕様に合わせる。
//   kWaitForever : 完了まで無制限に待つ
//   kReturnNow   : 起動だけして即座に返る（出力を待たない）
constexpr long kWaitForever = -1;
constexpr long kReturnNow   = 0;

// コマンドを実行し、標準出力＋標準エラーを UTF-8 で返す。
//
//   command_utf8  : 実行するコマンド（UTF-8）。空なら started=false の空結果。
//   timeout_ms    : kWaitForever(<0) = 無制限 / kReturnNow(0) = 即返り /
//                   >0 = ミリ秒。超過時は子孫ごと kill し timed_out=true。
//   use_shell     : true なら Windows は `cmd.exe /S /C "<command>"`、
//                   POSIX は `/bin/sh -c "<command>"` 経由で実行（ワンライナー可）。
//                   false なら command を直接プロセスとして起動する。
//   encoding_spec : 出力バイトの復号方法（大文字小文字無視）。空 or "oem" = コンソール
//                   OEM コードページ（日本語なら CP932）。他に "utf8"/"utf-8"、
//                   "cp932"/"932"/"sjis"/"shiftjis"、"ansi"/"acp"、"utf16"/"utf-16"、
//                   "auto"（UTF-8 厳密→失敗時 OEM フォールバック）、
//                   または任意の 10 進コードページ番号（例 65001, 949, 936）。
//                   ※ Windows 専用。POSIX では常に UTF-8 とみなし無視する。
ShellResult RunSystemCommand ( const std::string& command_utf8,
                               long timeout_ms,
                               bool use_shell,
                               const std::string& encoding_spec );

// 改行を CR(\r) に正規化し（CRLF→CR, 単独 LF→CR）、末尾の改行を除去する。
// FileMaker の内部改行は CR。単体テストしやすいよう公開する。
std::string NormalizeNewlines ( const std::string& text );

} // namespace be_shell

#endif // BE_SHELL_EXEC_H
