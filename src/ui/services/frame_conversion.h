#pragma once
// Decoded VideoFrame → QImage for UI surfaces (preview, thumbnails, hover
// previews). BT.601-ish math, acceptable for preview (docs/06 color
// management arrives with the render graph). One implementation shared by
// every UI consumer so they can never drift apart.

#include <velocity/media/video_decoder.h>

#include <QImage>

namespace velocity::ui {

// Converts a (CPU or hardware) frame to a displayable QImage. Returns a null
// image for unsupported pixel formats or failed GPU→CPU transfers.
QImage toQImage(const velocity::media::VideoFrame& frame);

} // namespace velocity::ui
