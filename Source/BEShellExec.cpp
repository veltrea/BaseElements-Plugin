// BEShellExec.cpp — RunSystemCommand / NormalizeNewlines の実装
//
// FMWrapper・Poco・boost 非依存。Windows は純 Win32 API のみで完結する。
// 詳細な設計意図は BEShellExec.h を参照。

#include "BEShellExec.h"

#include <vector>

#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#else
	#include <spawn.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <sys/wait.h>
	#include <cerrno>
	extern char** environ;
#endif

namespace be_shell {

std::string NormalizeNewlines ( const std::string& text )
{
	std::string out;
	out.reserve ( text.size() );

	// CRLF→CR, 単独 LF→CR に正規化（FileMaker の内部改行は CR）
	for ( std::size_t i = 0; i < text.size(); ++i ) {
		const char c = text[i];
		if ( c == '\r' ) {
			out.push_back ( '\r' );
			if ( i + 1 < text.size() && text[i + 1] == '\n' ) {
				++i; // CRLF をまとめて 1 つの CR にする
			}
		} else if ( c == '\n' ) {
			out.push_back ( '\r' );
		} else {
			out.push_back ( c );
		}
	}

	// 末尾の改行を取り除く（echo 等が付ける最後の改行を消す）
	while ( !out.empty() && ( out.back() == '\r' || out.back() == '\n' ) ) {
		out.pop_back();
	}
	return out;
}


#if defined(_WIN32)

namespace {

std::wstring Utf8ToWide ( const std::string& s )
{
	if ( s.empty() ) return std::wstring();
	const int n = ::MultiByteToWideChar ( CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0 );
	std::wstring w ( (std::size_t)n, L'\0' );
	::MultiByteToWideChar ( CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n );
	return w;
}

std::string WideToUtf8 ( const std::wstring& w )
{
	if ( w.empty() ) return std::string();
	const int n = ::WideCharToMultiByte ( CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr );
	std::string s ( (std::size_t)n, '\0' );
	::WideCharToMultiByte ( CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr );
	return s;
}

// 指定コードページのバイト列 → UTF-16
std::wstring BytesToWide ( const std::string& s, UINT codepage )
{
	if ( s.empty() ) return std::wstring();
	const int n = ::MultiByteToWideChar ( codepage, 0, s.data(), (int)s.size(), nullptr, 0 );
	if ( n <= 0 ) return std::wstring();
	std::wstring w ( (std::size_t)n, L'\0' );
	::MultiByteToWideChar ( codepage, 0, s.data(), (int)s.size(), &w[0], n );
	return w;
}

// ASCII 小文字化（ロケール非依存）
std::string ToLowerAscii ( const std::string& s )
{
	std::string r = s;
	for ( char& c : r ) {
		if ( c >= 'A' && c <= 'Z' ) c = (char)( c - 'A' + 'a' );
	}
	return r;
}

// 特殊値：バイト列を UTF-16LE そのものとして扱う指示
constexpr UINT kCodepageUtf16LE = 0xFFFFFFFEu;
// 特殊値：auto（UTF-8 厳密 → 失敗時 OEM フォールバック）
constexpr UINT kCodepageAuto    = 0xFFFFFFFFu;

// encoding_spec を Windows コードページ（または特殊値）に解決する。
UINT ResolveCodepage ( const std::string& encoding_spec )
{
	const std::string e = ToLowerAscii ( encoding_spec );

	if ( e.empty() || e == "oem" )                                   return ::GetOEMCP();
	if ( e == "ansi" || e == "acp" )                                 return ::GetACP();
	if ( e == "auto" )                                               return kCodepageAuto;
	if ( e == "utf8" || e == "utf-8" )                               return CP_UTF8;
	if ( e == "utf16" || e == "utf-16" || e == "utf16le" || e == "utf-16le" ) return kCodepageUtf16LE;
	if ( e == "cp932" || e == "932" || e == "sjis" || e == "shiftjis" || e == "shift_jis" || e == "shift-jis" ) return 932;

	// 純粋な 10 進コードページ番号（例 "65001", "949", "936"）
	bool all_digits = !e.empty();
	for ( char c : e ) { if ( c < '0' || c > '9' ) { all_digits = false; break; } }
	if ( all_digits ) {
		UINT cp = 0;
		for ( char c : e ) cp = cp * 10 + (UINT)( c - '0' );
		return cp;
	}

	// 不明な指定は既定（OEM）に落とす
	return ::GetOEMCP();
}

// 生バイト列を encoding に従って UTF-8 へデコードする。
std::string DecodeBytes ( const std::string& bytes, UINT codepage )
{
	if ( bytes.empty() ) return std::string();

	if ( codepage == kCodepageUtf16LE ) {
		const std::size_t wchars = bytes.size() / 2;
		std::wstring w ( wchars, L'\0' );
		if ( wchars ) ::memcpy ( &w[0], bytes.data(), wchars * 2 );
		return WideToUtf8 ( w );
	}

	if ( codepage == kCodepageAuto ) {
		// UTF-8 として厳密にデコードできるか試す
		const int n = ::MultiByteToWideChar ( CP_UTF8, MB_ERR_INVALID_CHARS,
		                                       bytes.data(), (int)bytes.size(), nullptr, 0 );
		if ( n > 0 ) {
			return bytes; // 既に妥当な UTF-8。そのまま返す
		}
		// 妥当な UTF-8 でなければ OEM(CP932) として再解釈
		return WideToUtf8 ( BytesToWide ( bytes, ::GetOEMCP() ) );
	}

	if ( codepage == CP_UTF8 ) {
		return bytes; // UTF-8 指定：そのまま
	}

	return WideToUtf8 ( BytesToWide ( bytes, codepage ) );
}

// UTF-8 コマンドから CreateProcessW に渡すコマンドラインを組み立てる。
std::wstring BuildCommandLine ( const std::string& command_utf8, bool use_shell )
{
	if ( use_shell ) {
		// cmd.exe /S /C "<command>"
		//   /S /C : 先頭と末尾の " を 1 組だけ外し、間をそのまま 1 コマンドとして
		//           実行する。ユーザーのワンライナー（引用符・リダイレクト・&）を
		//           再トークン化せずそのまま渡せる。
		return L"cmd.exe /S /C \"" + Utf8ToWide ( command_utf8 ) + L"\"";
	}
	// 直接起動：コマンド文字列をそのまま lpCommandLine に渡す（アプリ名は解決される）
	return Utf8ToWide ( command_utf8 );
}

} // namespace


ShellResult RunSystemCommand ( const std::string& command_utf8,
                               long timeout_ms,
                               bool use_shell,
                               const std::string& encoding_spec )
{
	ShellResult result;
	if ( command_utf8.empty() ) return result;

	const UINT codepage = ResolveCodepage ( encoding_spec );

	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof ( sa );
	sa.lpSecurityDescriptor = nullptr;
	sa.bInheritHandle = TRUE;

	// 子プロセスの標準出力／標準エラーを受け取る匿名パイプ
	HANDLE read_pipe = nullptr;
	HANDLE write_pipe = nullptr;
	if ( !::CreatePipe ( &read_pipe, &write_pipe, &sa, 0 ) ) {
		return result;
	}
	::SetHandleInformation ( read_pipe, HANDLE_FLAG_INHERIT, 0 ); // 読み取り側は継承させない

	// 標準入力は NUL に向け、入力待ちハングを防ぐ
	HANDLE nul_in = ::CreateFileW ( L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
	                                &sa, OPEN_EXISTING, 0, nullptr );

	// タイムアウト時に子孫プロセスごと確実に殺すため Job オブジェクトを使う
	HANDLE job = ::CreateJobObjectW ( nullptr, nullptr );
	if ( job ) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
		::ZeroMemory ( &jeli, sizeof ( jeli ) );
		jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		::SetInformationJobObject ( job, JobObjectExtendedLimitInformation, &jeli, sizeof ( jeli ) );
	}

	STARTUPINFOW si;
	::ZeroMemory ( &si, sizeof ( si ) );
	si.cb = sizeof ( si );
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = nul_in;
	si.hStdOutput = write_pipe;
	si.hStdError = write_pipe; // stderr もまとめて取得

	PROCESS_INFORMATION pi;
	::ZeroMemory ( &pi, sizeof ( pi ) );

	std::wstring command_line = BuildCommandLine ( command_utf8, use_shell );
	std::vector<wchar_t> mutable_cmd ( command_line.begin(), command_line.end() );
	mutable_cmd.push_back ( L'\0' ); // CreateProcessW は書き換え可能バッファを要求する

	// CREATE_SUSPENDED で起動 → Job に割り当て → 再開。こうすると子が動き出す前に
	// Job へ入るので、子が即座に生成する孫プロセスも Job に含められる。
	const BOOL ok = ::CreateProcessW (
		nullptr,
		mutable_cmd.data(),
		nullptr, nullptr,
		TRUE,                                        // パイプを継承
		CREATE_NO_WINDOW | CREATE_SUSPENDED,         // 窓を出さず、まず一時停止
		nullptr, nullptr,
		&si, &pi );

	// 親側では書き込み側とNUL入力を閉じる（子だけが write_pipe を保持 → 子終了で EOF）
	::CloseHandle ( write_pipe );
	if ( nul_in ) ::CloseHandle ( nul_in );

	if ( !ok ) {
		::CloseHandle ( read_pipe );
		if ( job ) ::CloseHandle ( job );
		return result; // started=false
	}
	result.started = true;

	if ( job ) ::AssignProcessToJobObject ( job, pi.hProcess );
	::ResumeThread ( pi.hThread );

	// --- 出力読み取り＋タイムアウト監視（スレッド不要のポーリング方式） ---
	const bool wait_forever = ( timeout_ms < 0 );
	const bool return_now   = ( timeout_ms == 0 );
	const DWORD deadline    = wait_forever ? 0 : ( ::GetTickCount() + (DWORD)timeout_ms );

	std::string raw;
	char buf[4096];

	if ( !return_now ) {
		for ( ;; ) {
			// パイプに来ているデータを可能な限り読む
			DWORD avail = 0;
			while ( ::PeekNamedPipe ( read_pipe, nullptr, 0, nullptr, &avail, nullptr ) && avail > 0 ) {
				DWORD nread = 0;
				const DWORD to_read = avail < sizeof ( buf ) ? avail : (DWORD)sizeof ( buf );
				if ( ::ReadFile ( read_pipe, buf, to_read, &nread, nullptr ) && nread > 0 ) {
					raw.append ( buf, nread );
				} else {
					break;
				}
			}

			// プロセス終了？（終了していればパイプを読み切って抜ける）
			const DWORD w = ::WaitForSingleObject ( pi.hProcess, 0 );
			if ( w == WAIT_OBJECT_0 ) {
				DWORD nread = 0;
				while ( ::ReadFile ( read_pipe, buf, sizeof ( buf ), &nread, nullptr ) && nread > 0 ) {
					raw.append ( buf, nread );
				}
				break;
			}

			// タイムアウト？
			if ( !wait_forever ) {
				// GetTickCount のラップアラウンドを跨いでも符号付き差分で正しく判定
				if ( (long)( ::GetTickCount() - deadline ) >= 0 ) {
					result.timed_out = true;
					break;
				}
			}

			::Sleep ( 5 ); // ビジーウェイトを避ける
		}
	}

	if ( result.timed_out ) {
		// Job を閉じると KILL_ON_JOB_CLOSE で子孫ごと終了する
		if ( job ) {
			::TerminateJobObject ( job, 1 );
		} else {
			::TerminateProcess ( pi.hProcess, 1 );
		}
	} else if ( !return_now ) {
		DWORD code = 0;
		if ( ::GetExitCodeProcess ( pi.hProcess, &code ) ) {
			result.exit_code = code;
		}
	}

	::CloseHandle ( read_pipe );
	::CloseHandle ( pi.hThread );
	::CloseHandle ( pi.hProcess );
	if ( job ) ::CloseHandle ( job ); // KILL_ON_JOB_CLOSE：未終了の子が残っていればここで掃除

	result.output_utf8 = NormalizeNewlines ( DecodeBytes ( raw, codepage ) );
	return result;
}


#else // ---- POSIX (macOS / Linux) ----

namespace {

std::vector<std::string> ShellArgv ( const std::string& command )
{
	return std::vector<std::string> { "/bin/sh", "-c", command };
}

} // namespace

ShellResult RunSystemCommand ( const std::string& command_utf8,
                               long timeout_ms,
                               bool use_shell,
                               const std::string& /* encoding_spec: POSIX は UTF-8 とみなす */ )
{
	ShellResult result;
	if ( command_utf8.empty() ) return result;

	// use_shell=false は将来 argv 分割に対応する余地を残すが、現状はシェル経由に統一。
	// （POSIX ではシェル経由が最も互換性が高く、引用問題も /bin/sh に委ねられる）
	const std::vector<std::string> argv = ShellArgv ( command_utf8 );

	int pipefd[2];
	if ( ::pipe ( pipefd ) != 0 ) return result;
	const int rd = pipefd[0];
	const int wr = pipefd[1];

	posix_spawn_file_actions_t actions;
	::posix_spawn_file_actions_init ( &actions );
	::posix_spawn_file_actions_addopen ( &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0 );
	::posix_spawn_file_actions_adddup2 ( &actions, wr, STDOUT_FILENO );
	::posix_spawn_file_actions_adddup2 ( &actions, wr, STDERR_FILENO );
	::posix_spawn_file_actions_addclose ( &actions, rd );
	::posix_spawn_file_actions_addclose ( &actions, wr );

	std::vector<char*> c_argv;
	c_argv.reserve ( argv.size() + 1 );
	for ( const std::string& a : argv ) c_argv.push_back ( const_cast<char*> ( a.c_str() ) );
	c_argv.push_back ( nullptr );

	pid_t pid = 0;
	const int spawn_rc = ::posix_spawnp ( &pid, c_argv[0], &actions, nullptr, c_argv.data(), environ );
	::posix_spawn_file_actions_destroy ( &actions );
	::close ( wr );

	if ( spawn_rc != 0 ) {
		::close ( rd );
		return result; // started=false
	}
	result.started = true;

	// POSIX 側のタイムアウトは将来対応（現状は完了まで待つ）。use_shell/timeout_ms は
	// Windows と同じ引数順を保つため受け取るが、ここでは完了待ちのみ行う。
	(void) use_shell;
	(void) timeout_ms;

	std::string raw;
	char buf[4096];
	ssize_t n = 0;
	while ( ( n = ::read ( rd, buf, sizeof ( buf ) ) ) > 0 ) {
		raw.append ( buf, (std::size_t)n );
	}
	::close ( rd );

	int status = 0;
	while ( ::waitpid ( pid, &status, 0 ) < 0 && errno == EINTR ) {
		// シグナル割り込みは無視して待ち直す
	}
	if ( WIFEXITED ( status ) ) {
		result.exit_code = (unsigned long) WEXITSTATUS ( status );
	}

	result.output_utf8 = NormalizeNewlines ( raw ); // macOS/Linux の出力は UTF-8 とみなす
	return result;
}

#endif

} // namespace be_shell
