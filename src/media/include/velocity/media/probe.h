#pragma once
// Fast, header-only-inputs media probing (docs/04 §1). Opens the container,
// reads stream metadata, never decodes more than stream-info discovery needs.

#include <velocity/foundation/expected.h>
#include <velocity/foundation/time.h>
#include <velocity/media/error.h>

#include <filesystem>
#include <optional>
#include <string>

namespace velocity::media {

struct VideoStreamInfo {
    int index = -1;
    int width = 0;
    int height = 0;
    Rational frameRate{0, 1}; // container-declared; may be corrected by indexing later
    Rational timebase{0, 1};
    std::int64_t durationPts = 0; // in timebase units, 0 when unknown
    std::string codecName;
};

struct AudioStreamInfo {
    int index = -1;
    int sampleRate = 0;
    int channels = 0;
    Rational timebase{0, 1};
    std::int64_t durationPts = 0;
    std::string codecName;
};

struct MediaInfo {
    std::optional<VideoStreamInfo> bestVideo;
    std::optional<AudioStreamInfo> bestAudio;
    double durationSeconds = 0.0;
    std::string containerName;
};

Expected<MediaInfo, MediaError> probe(const std::filesystem::path& file);

} // namespace velocity::media
