#pragma once
#include <QString>
namespace luma {
bool installApplicationFonts(const QString &runtime={});
void installPlayerTheme();
bool playerUsesDarkTheme();
void setPlayerDarkTheme(bool dark);
}
