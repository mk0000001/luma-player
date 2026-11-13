#pragma once

#include <QString>

namespace luma {
// Creates a unique directory whose files are accessible only to the user (and SYSTEM on Windows).
QString createPrivateDirectory(const QString &parent, const QString &prefix);
}
