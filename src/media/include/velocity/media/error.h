#pragma once
#include <string>

namespace velocity::media {

enum class MediaErrorKind {
    io,          // file open / read failures
    unsupported, // no stream / no codec / no hw path
    decode,      // decoder returned an error mid-stream
    endOfStream, // normal termination of a read loop
};

struct MediaError {
    MediaErrorKind kind = MediaErrorKind::io;
    std::string message;

    [[nodiscard]] bool isEndOfStream() const { return kind == MediaErrorKind::endOfStream; }
};

} // namespace velocity::media
