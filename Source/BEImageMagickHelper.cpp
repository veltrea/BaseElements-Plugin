// BEImageMagickHelper.cpp — ConvertImage の実装
//
// FMWrapper・Poco・boost 非依存。プロセス起動（CreateProcessW・タイムアウト・
// Job kill・出力デコード）は BEShellExec の RunSystemCommand に委譲する。
// 設計意図は BEImageMagickHelper.h を参照。

#include "BEImageMagickHelper.h"

#if defined(_WIN32)

#include "BEShellExec.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cwchar>

namespace be_magick {

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

// module のフルパスからディレクトリ部分（末尾 \ 付き）を返す。
std::wstring ModuleDirectory ( HMODULE module )
{
	std::wstring path ( MAX_PATH, L'\0' );
	for ( ;; ) {
		const DWORD n = ::GetModuleFileNameW ( module, &path[0], (DWORD)path.size() );
		if ( n == 0 ) return std::wstring();
		if ( n < (DWORD)path.size() ) { path.resize ( n ); break; }
		path.resize ( path.size() * 2 ); // バッファ不足：倍にして再試行
	}
	const std::size_t slash = path.find_last_of ( L'\\' );
	return ( slash == std::wstring::npos ) ? std::wstring() : path.substr ( 0, slash + 1 );
}

// magick.exe を固定の場所から探す（PATH 探索はしない）。
//   1) belibs.dll と同じディレクトリ（= FMP exe 同階層。DLL 群も揃っている）
//   2) このコードを含むモジュールのディレクトリ（プラグイン .fmx / テスト exe）
std::wstring FindHelperPath ()
{
	if ( HMODULE belibs = ::GetModuleHandleW ( L"belibs.dll" ) ) {
		const std::wstring candidate = ModuleDirectory ( belibs ) + L"magick.exe";
		if ( ::GetFileAttributesW ( candidate.c_str() ) != INVALID_FILE_ATTRIBUTES ) {
			return candidate;
		}
	}

	HMODULE self = nullptr;
	if ( ::GetModuleHandleExW ( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                            (LPCWSTR)(void*)&FindHelperPath, &self ) ) {
		const std::wstring candidate = ModuleDirectory ( self ) + L"magick.exe";
		if ( ::GetFileAttributesW ( candidate.c_str() ) != INVALID_FILE_ATTRIBUTES ) {
			return candidate;
		}
	}

	return std::wstring();
}

// %TEMP% 内の一意なベースパスを返す（拡張子なし）。日本語 %TEMP% でも wide API で安全。
std::wstring TempFileBase ()
{
	std::wstring dir ( MAX_PATH + 1, L'\0' );
	const DWORD n = ::GetTempPathW ( (DWORD)dir.size(), &dir[0] );
	if ( n == 0 || n >= (DWORD)dir.size() ) return std::wstring();
	dir.resize ( n );

	static std::atomic<unsigned> counter { 0 };
	wchar_t name[64];
	std::swprintf ( name, 64, L"be_img_%lu_%lu_%u",
	                (unsigned long)::GetCurrentProcessId(),
	                (unsigned long)::GetTickCount(),
	                counter.fetch_add ( 1 ) + 1 );
	return dir + name;
}

bool WriteAllBytes ( const std::wstring& path, const std::vector<char>& bytes )
{
	HANDLE h = ::CreateFileW ( path.c_str(), GENERIC_WRITE, 0, nullptr,
	                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	if ( h == INVALID_HANDLE_VALUE ) return false;

	bool ok = true;
	std::size_t total = 0;
	while ( ok && total < bytes.size() ) {
		const std::size_t remain = bytes.size() - total;
		const DWORD chunk = remain < ( 1u << 20 ) ? (DWORD)remain : ( 1u << 20 );
		DWORD written = 0;
		ok = ::WriteFile ( h, bytes.data() + total, chunk, &written, nullptr ) && written == chunk;
		total += written;
	}
	::CloseHandle ( h );
	return ok;
}

bool ReadAllBytes ( const std::wstring& path, std::vector<char>& bytes )
{
	HANDLE h = ::CreateFileW ( path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
	                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
	if ( h == INVALID_HANDLE_VALUE ) return false;

	LARGE_INTEGER size;
	bool ok = ::GetFileSizeEx ( h, &size ) != 0
	          && size.QuadPart >= 0 && size.QuadPart < ( (LONGLONG)1 << 31 );
	if ( ok ) {
		bytes.resize ( (std::size_t)size.QuadPart );
		std::size_t total = 0;
		while ( ok && total < bytes.size() ) {
			const std::size_t remain = bytes.size() - total;
			const DWORD chunk = remain < ( 1u << 20 ) ? (DWORD)remain : ( 1u << 20 );
			DWORD nread = 0;
			ok = ::ReadFile ( h, bytes.data() + total, chunk, &nread, nullptr ) && nread > 0;
			total += nread;
		}
	}
	::CloseHandle ( h );
	return ok;
}

} // namespace


ConvertImageResult ConvertImage ( const std::vector<char>& image_bytes,
                                  const std::string& output_extension_utf8 )
{
	ConvertImageResult result;

	// 拡張子はコマンドラインと一時ファイル名にそのまま埋め込むので厳しく制限する
	const std::string& ext = output_extension_utf8;
	bool ext_ok = ! ext.empty() && ext.size() <= 8;
	for ( const char c : ext ) {
		if ( ! ( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) ) ) {
			ext_ok = false;
			break;
		}
	}
	if ( ! ext_ok ) {
		result.error_utf8 = "invalid image format: " + ext;
		return result;
	}

	const std::wstring helper = FindHelperPath();
	if ( helper.empty() ) {
		result.error_utf8 = "magick.exe not found (expected beside belibs.dll or the plug-in)";
		return result;
	}

	const std::wstring base = TempFileBase();
	if ( base.empty() ) {
		result.error_utf8 = "failed to resolve the temporary directory";
		return result;
	}

	// 入力は未知の拡張子 .dat で書き、形式は magick にマジックバイトで判別させる。
	// 出力形式は出力ファイルの拡張子で指定する。
	const std::wstring in_path  = base + L"_in.dat";
	const std::wstring out_path = base + L"_out." + Utf8ToWide ( ext );

	if ( ! WriteAllBytes ( in_path, image_bytes ) ) {
		::DeleteFileW ( in_path.c_str() );
		result.error_utf8 = "failed to write the temporary input file";
		return result;
	}

	// -quiet: delegates.xml 不在などの警告を抑止し、stderr を実エラーだけにする
	const std::string command = "\"" + WideToUtf8 ( helper ) + "\" -quiet \""
	                            + WideToUtf8 ( in_path ) + "\" \""
	                            + WideToUtf8 ( out_path ) + "\"";

	// シェルを介さず magick.exe を直接起動。タイムアウト時は Job で子孫ごと kill。
	// stderr は "auto"（UTF-8 厳密→失敗時 OEM）で UTF-8 へデコードされる。
	const be_shell::ShellResult run =
		be_shell::RunSystemCommand ( command, kConvertTimeoutMs, false, "auto" );

	if ( ! run.started ) {
		result.error_utf8 = "failed to launch magick.exe";
	} else if ( run.timed_out ) {
		result.error_utf8 = "image conversion timed out";
	} else if ( run.exit_code != 0 ) {
		result.error_utf8 = run.output_utf8.empty()
			? "magick.exe failed (exit code " + std::to_string ( run.exit_code ) + ")"
			: run.output_utf8;
	} else if ( ! ReadAllBytes ( out_path, result.output ) || result.output.empty() ) {
		result.error_utf8 = "magick.exe produced no output";
	} else {
		result.ok = true;
	}

	::DeleteFileW ( in_path.c_str() );
	::DeleteFileW ( out_path.c_str() );

	return result;
}

} // namespace be_magick

#else // ---- Windows 以外 ----

// 本モジュールを使うのは Windows 32bit ビルドだけ（BE_MAGICK_VIA_HELPER）。
// 他プラットフォームでは従来どおり Magick++ を直接リンクするため実装を持たない。

namespace be_magick {

ConvertImageResult ConvertImage ( const std::vector<char>& /* image_bytes */,
                                  const std::string& /* output_extension_utf8 */ )
{
	ConvertImageResult result;
	result.error_utf8 = "not implemented on this platform";
	return result;
}

} // namespace be_magick

#endif
