#pragma once
// Internal-to-src/media helpers: RAII wrappers over FFmpeg contexts and
// error-string translation. Never included from public headers (docs/04 §1:
// FFmpeg types do not escape src/media/).

#include <filesystem>
#include <memory>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

namespace velocity::media {

struct FormatContextDeleter {
    void operator()(AVFormatContext* c) const { avformat_close_input(&c); }
};
using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;

inline FormatContextPtr openInput(const std::filesystem::path& file) {
    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, file.string().c_str(), nullptr, nullptr) < 0)
        return nullptr;
    return FormatContextPtr{raw};
}

inline std::string avErrorText(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

} // namespace velocity::media
