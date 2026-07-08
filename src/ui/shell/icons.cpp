#include "icons.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace velocity::ui::icons {

namespace {

// 24×24 viewBox, stroke-only bodies. %C% is replaced with the stroke color.
const QHash<QString, QString>& bodies() {
    static const QHash<QString, QString> map = {
        {"play", R"(<polygon points="6 4 20 12 6 20 6 4" fill="%C%" stroke="none"/>)"},
        {"pause", R"(<rect x="6" y="4" width="4" height="16" fill="%C%" stroke="none"/><rect x="14" y="4" width="4" height="16" fill="%C%" stroke="none"/>)"},
        {"stop", R"(<rect x="6" y="6" width="12" height="12" rx="1" fill="%C%" stroke="none"/>)"},
        {"skip-start", R"(<polygon points="19 20 9 12 19 4 19 20" fill="%C%" stroke="none"/><line x1="5" y1="19" x2="5" y2="5"/>)"},
        {"frame-prev", R"(<polyline points="15 18 9 12 15 6"/>)"},
        {"frame-next", R"(<polyline points="9 18 15 12 9 6"/>)"},
        {"loop", R"(<polyline points="17 1 21 5 17 9"/><path d="M3 11V9a4 4 0 0 1 4-4h14"/><polyline points="7 23 3 19 7 15"/><path d="M21 13v2a4 4 0 0 1-4 4H3"/>)"},
        {"undo", R"(<polyline points="9 14 4 9 9 4"/><path d="M20 20v-7a4 4 0 0 0-4-4H4"/>)"},
        {"redo", R"(<polyline points="15 14 20 9 15 4"/><path d="M4 20v-7a4 4 0 0 1 4-4h12"/>)"},
        {"split", R"(<circle cx="6" cy="6" r="3"/><circle cx="6" cy="18" r="3"/><line x1="20" y1="4" x2="8.12" y2="15.88"/><line x1="14.47" y1="14.48" x2="20" y2="20"/><line x1="8.12" y1="8.12" x2="12" y2="12"/>)"},
        {"trash", R"(<polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/>)"},
        {"zoom-in", R"(<circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/><line x1="11" y1="8" x2="11" y2="14"/><line x1="8" y1="11" x2="14" y2="11"/>)"},
        {"zoom-out", R"(<circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/><line x1="8" y1="11" x2="14" y2="11"/>)"},
        {"zoom-fit", R"(<path d="M8 3H5a2 2 0 0 0-2 2v3"/><path d="M21 8V5a2 2 0 0 0-2-2h-3"/><path d="M16 21h3a2 2 0 0 0 2-2v-3"/><path d="M3 16v3a2 2 0 0 0 2 2h3"/>)"},
        {"import", R"(<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/>)"},
        {"export", R"(<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/>)"},
        {"eye", R"(<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>)"},
        {"eye-off", R"(<path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94"/><path d="M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19"/><line x1="1" y1="1" x2="23" y2="23"/>)"},
        {"lock", R"(<rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/>)"},
        {"unlock", R"(<rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0 1 9.9-1"/>)"},
        {"volume", R"(<polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5" fill="%C%" stroke="none"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/>)"},
        {"mute", R"(<polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5" fill="%C%" stroke="none"/><line x1="23" y1="9" x2="17" y2="15"/><line x1="17" y1="9" x2="23" y2="15"/>)"},
        {"plus", R"(<line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/>)"},
        {"detach", R"(<path d="M9 17H7A5 5 0 0 1 7 7h2"/><path d="M15 7h2a5 5 0 0 1 0 10h-2"/><line x1="4" y1="20" x2="20" y2="4"/>)"},
        {"film", R"(<rect x="2" y="2" width="20" height="20" rx="2"/><line x1="7" y1="2" x2="7" y2="22"/><line x1="17" y1="2" x2="17" y2="22"/><line x1="2" y1="12" x2="22" y2="12"/><line x1="2" y1="7" x2="7" y2="7"/><line x1="2" y1="17" x2="7" y2="17"/><line x1="17" y1="7" x2="22" y2="7"/><line x1="17" y1="17" x2="22" y2="17"/>)"},
        {"music", R"(<path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/>)"},
        {"image", R"(<rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/>)"},
    };
    return map;
}

QPixmap renderAt(const QString& svg, int px) {
    QSvgRenderer renderer;
    renderer.load(svg.toUtf8());
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    renderer.render(&p, QRectF(0, 0, px, px));
    p.end();
    return pm;
}

} // namespace

QIcon icon(const QString& name, const QColor& color) {
    static QHash<QString, QIcon> cache;
    const QString key = name + "/" + color.name();
    if (auto it = cache.constFind(key); it != cache.constEnd())
        return it.value();

    const QString body = bodies().value(name);
    if (body.isEmpty())
        return {};

    QString svg = QStringLiteral(
                      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
                      "fill=\"none\" stroke=\"%1\" stroke-width=\"2\" stroke-linecap=\"round\" "
                      "stroke-linejoin=\"round\">%2</svg>")
                      .arg(color.name(), body);
    svg.replace("%C%", color.name());

    QIcon result;
    for (int px : {16, 20, 24, 32, 48})
        result.addPixmap(renderAt(svg, px));
    cache.insert(key, result);
    return result;
}

QIcon icon(const QString& name) { return icon(name, QColor(0xc9, 0xc9, 0xce)); }

} // namespace velocity::ui::icons
