#pragma once
#include <QProcessEnvironment>
#include <QString>

namespace luma {
QProcessEnvironment localRuntimeEnvironment(const QString &runtime,
    QProcessEnvironment environment=QProcessEnvironment::systemEnvironment());
}
