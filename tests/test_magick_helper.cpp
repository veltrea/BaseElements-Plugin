// test_magick_helper.cpp — BEImageMagickHelper 単体テストハーネス（FileMaker 不要）
//
// FMWrapper・Poco・boost に依存しないので、cl.exe だけでビルドできる:
//   cl /EHsc /utf-8 /I ..\Source test_magick_helper.cpp ^
//      ..\Source\BEImageMagickHelper.cpp ..\Source\BEShellExec.cpp
// 実行するときは magick.exe（+ mingw ランタイム DLL）をこの exe と同じディレクトリ
// に置くこと（FindHelperPath のフォールバック = 自モジュールのディレクトリ）。
//
// 使い方: test_magick_helper.exe <画像ファイル>
//   1) <画像ファイル> → jpg 変換（先頭バイトが FF D8 になるはず）
//   2) <画像ファイル> → png 変換（先頭バイトが 89 50 になるはず）
//   3) 画像でないバイト列 → 明示エラー（magick の stderr が返るはず）
//   4) 不正な拡張子 → 明示エラー（プロセス起動前に弾かれるはず）

#include "BEImageMagickHelper.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using be_magick::ConvertImage;
using be_magick::ConvertImageResult;

static int g_failures = 0;

static void check ( const char* label, bool passed, const std::string& detail )
{
	std::printf ( "%s %s — %s\n", passed ? "[PASS]" : "[FAIL]", label, detail.c_str() );
	if ( ! passed ) ++g_failures;
}

static std::vector<char> read_file ( const char* path )
{
	std::vector<char> bytes;
	FILE* f = std::fopen ( path, "rb" );
	if ( ! f ) return bytes;
	std::fseek ( f, 0, SEEK_END );
	const long size = std::ftell ( f );
	std::fseek ( f, 0, SEEK_SET );
	if ( size > 0 ) {
		bytes.resize ( (std::size_t)size );
		if ( std::fread ( bytes.data(), 1, bytes.size(), f ) != bytes.size() ) {
			bytes.clear();
		}
	}
	std::fclose ( f );
	return bytes;
}

static std::string first_bytes_hex ( const std::vector<char>& bytes )
{
	char buf[16] = "";
	if ( bytes.size() >= 2 ) {
		std::snprintf ( buf, sizeof ( buf ), "%02X %02X",
		                (unsigned char)bytes[0], (unsigned char)bytes[1] );
	}
	return buf;
}

int main ( int argc, char** argv )
{
	if ( argc < 2 ) {
		std::printf ( "usage: test_magick_helper.exe <image-file>\n" );
		return 2;
	}

	const std::vector<char> image = read_file ( argv[1] );
	if ( image.empty() ) {
		std::printf ( "cannot read %s\n", argv[1] );
		return 2;
	}

	// 1) → jpg（JPEG マジック FF D8）
	{
		const ConvertImageResult r = ConvertImage ( image, "jpg" );
		check ( "convert_to_jpg", r.ok && r.output.size() >= 2
		        && (unsigned char)r.output[0] == 0xFF && (unsigned char)r.output[1] == 0xD8,
		        r.ok ? "bytes=" + std::to_string ( r.output.size() )
		               + " magic=" + first_bytes_hex ( r.output )
		             : "error: " + r.error_utf8 );
	}

	// 2) → png（PNG マジック 89 50）
	{
		const ConvertImageResult r = ConvertImage ( image, "png" );
		check ( "convert_to_png", r.ok && r.output.size() >= 2
		        && (unsigned char)r.output[0] == 0x89 && (unsigned char)r.output[1] == 0x50,
		        r.ok ? "bytes=" + std::to_string ( r.output.size() )
		               + " magic=" + first_bytes_hex ( r.output )
		             : "error: " + r.error_utf8 );
	}

	// 3) 画像でないバイト列 → ok=false + エラーテキスト（magick の stderr）
	{
		const char* garbage = "this is not an image at all, just plain text bytes";
		const std::vector<char> not_image ( garbage, garbage + std::strlen ( garbage ) );
		const ConvertImageResult r = ConvertImage ( not_image, "jpg" );
		check ( "garbage_input_fails", ! r.ok && ! r.error_utf8.empty(),
		        r.ok ? "unexpectedly succeeded" : "error: " + r.error_utf8 );
	}

	// 4) 不正な拡張子 → ok=false（プロセスを起動せずに弾く）
	{
		const ConvertImageResult r = ConvertImage ( image, "j pg" );
		check ( "bad_extension_rejected", ! r.ok && ! r.error_utf8.empty(),
		        r.ok ? "unexpectedly succeeded" : "error: " + r.error_utf8 );
	}

	std::printf ( "%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures );
	return g_failures == 0 ? 0 : 1;
}
