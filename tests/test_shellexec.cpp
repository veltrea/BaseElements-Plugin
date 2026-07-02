// test_shellexec.cpp — BEShellExec 単体テストハーネス（FileMaker 不要）
//
// FMWrapper・Poco・boost に依存しないので、cl.exe だけでビルドできる:
//   cl /EHsc /utf-8 /I ..\Source test_shellexec.cpp ..\Source\BEShellExec.cpp
// 生成した test_shellexec.exe を実行すると、公式 3.3.8 で壊れていたケース
// （素の echo・日本語・chcp）が直っているかを一括で確認できる。

#include "BEShellExec.h"

#include <cstdio>
#include <string>

using be_shell::RunSystemCommand;
using be_shell::ShellResult;

static void dump ( const char* label, const ShellResult& r )
{
	std::printf ( "=== %s\n", label );
	std::printf ( "    started=%d timed_out=%d exit=%lu len=%zu\n",
	              (int)r.started, (int)r.timed_out, r.exit_code, r.output_utf8.size() );
	std::printf ( "    bytes=[" );
	for ( unsigned char c : r.output_utf8 ) std::printf ( "%02x ", c );
	std::printf ( "]\n" );
	std::printf ( "    utf8=[%s]\n", r.output_utf8.c_str() );
}

static void run ( const char* label, const std::string& cmd, long timeout,
                  bool shell, const std::string& enc )
{
	dump ( label, RunSystemCommand ( cmd, timeout, shell, enc ) );
}

int main ()
{
	// 1) cmd /c echo hello（ASCII, shell 経由）→ "hello"
	run ( "cmd_echo_ascii", "cmd /c echo hello", 5000, true, "" );

	// 2) 素の echo hello（shell 経由なら通るはず。公式3.3.8は shell 無しで失敗していた）
	run ( "bare_echo_shell", "echo hello", 5000, true, "" );

	// 3) 日本語 echo（既定 OEM=CP932 デコード）→ "こんにちは" が化けずに返るはず
	run ( "jp_echo_oem", "echo \xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf", 5000, true, "" );

	// 4) chcp（日本語出力「現在のコード ページ: 932」）→ 化けずに返るはず
	run ( "chcp_oem", "chcp", 5000, true, "" );

	// 5) ver（ASCII）
	run ( "ver", "ver", 5000, true, "" );

	// 6) UTF-8 明示指定で ASCII（そのまま）
	run ( "cmd_echo_utf8", "cmd /c echo hello", 5000, true, "utf8" );

	// 7) auto エンコーディング（UTF-8 厳密→失敗時 CP932）で日本語
	run ( "jp_echo_auto", "echo \xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf", 5000, true, "auto" );

	// 8) タイムアウト（ping で ~3 秒待つコマンドを 1 秒で打ち切る）→ timed_out=1
	run ( "timeout_kill", "ping -n 4 127.0.0.1", 1000, true, "" );

	// 9) 存在しないコマンド（shell 経由）→ started=1 だがエラー出力、exit!=0
	run ( "nonexistent", "no_such_command_xyz", 5000, true, "" );

	// 10) shell 無しで cmd を直接起動（引数は cmd の流儀）
	run ( "direct_cmd", "cmd /c echo direct", 5000, false, "" );

	std::printf ( "HARNESS_DONE\n" );
	return 0;
}
