#pragma once
// Crisp SVG icon set (stroke style, Feather/Lucide-like geometry) rendered
// on demand through QSvgRenderer at the exact device pixel sizes, per the
// docs/11 §6 polish contract ("icons are SVG rendered to the device grid").

#include <QIcon>
#include <QString>

namespace velocity::ui::icons {

// Named icon in the default chrome color (#c9c9ce). Cached per (name, color).
QIcon icon(const QString& name);

// Same geometry with an explicit stroke color (e.g. accent or danger).
QIcon icon(const QString& name, const QColor& color);

} // namespace velocity::ui::icons
