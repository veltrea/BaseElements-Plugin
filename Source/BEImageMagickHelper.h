// BEImageMagickHelper.h
//
// BE_ContainerConvertImage の画像変換をワンショット子プロセス（同梱 magick.exe）に
// 委譲する純粋ロジック層。FMWrapper・Poco・boost に依存しないので、FileMaker を
// 起動せずに単体 exe としてビルド・実行・テストできる（tests/test_magick_helper.cpp）。
//
// なぜプロセス分離か: mingw ビルドの ImageMagick（MagickCore/Wand = belibs.dll 内）
// と MSVC 再コンパイルの Magick++ を混在リンクすると、ヘッダ内構造体レイアウトの
// 食い違い（MAGICKCORE_HAVE_PTHREAD 等）で AV が起き、FMP11 は SEH で握りつぶして
// 式評価ごと黙って中断する。プロセス境界は究極の ABI 境界であり、この問題のクラス
// 全体が消える。ヘルパーが死んでも FileMaker は無傷で、明示エラーにできる。
// （経緯: HANDOVER-32bit.md SESSION 7 / メモリ be-plugin-livefire-results）
//
// ヘルパーの探索は PATH を使わず、belibs.dll と同じディレクトリ（= FMP exe 同階層）
// → プラグイン自身のディレクトリ、の順で固定する。見つからなければ明示エラー。

#ifndef BE_IMAGEMAGICK_HELPER_H
#define BE_IMAGEMAGICK_HELPER_H

#include <string>
#include <vector>

namespace be_magick {

// 変換結果。ok=false のとき error_utf8 に失敗理由が入る。
struct ConvertImageResult {
	bool ok = false;
	std::string error_utf8;    // ヘルパー不在／タイムアウト／magick の stderr など
	std::vector<char> output;  // 変換後の画像バイト列（ok=true のとき有効）
};

// 変換の上限時間。超えたら Job オブジェクトで子孫ごと強制終了して失敗を返す。
constexpr long kConvertTimeoutMs = 60000;

// image_bytes を output_extension_utf8（例 "jpg", "png", "gif"）の形式へ変換する。
// 入力形式は magick.exe が内容（マジックバイト）から自動判別する。
// 拡張子は [a-z0-9]{1,8} のみ許可（コマンドラインと一時ファイル名に使うため）。
ConvertImageResult ConvertImage ( const std::vector<char>& image_bytes,
                                  const std::string& output_extension_utf8 );

} // namespace be_magick

#endif // BE_IMAGEMAGICK_HELPER_H
