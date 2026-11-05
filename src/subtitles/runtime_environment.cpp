#include "runtime_environment.h"
#include <QDir>
namespace luma {
QProcessEnvironment localRuntimeEnvironment(const QString &runtime,QProcessEnvironment environment){
#ifdef Q_OS_WIN
    const auto shared=QDir::cleanPath(QDir(runtime).absoluteFilePath("cuda"));
    if(QDir(shared).exists()){
        QStringList paths;
        for(const auto &path:environment.value("PATH").split(';',Qt::SkipEmptyParts)){
            if(QDir::cleanPath(QDir::fromNativeSeparators(path)).compare(shared,Qt::CaseInsensitive)!=0)paths.append(path);
        }
        paths.prepend(QDir::toNativeSeparators(shared));environment.insert("PATH",paths.join(';'));
    }
#else
    Q_UNUSED(runtime);
#endif
    return environment;
}
}
