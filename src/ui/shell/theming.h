#pragma once

class QApplication;

namespace velocity::ui::theming {

// Applies a clean, premium dark theme stylesheet to the Qt application,
// utilizing semantic dark colors, customized borders, and professional typography.
void applyDarkTheme(QApplication& app);

} // namespace velocity::ui::theming
