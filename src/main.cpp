#include <QApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QFile>
#include <QSaveFile>
#include <QTimer>
#include "ui/main_window.h"
#include "luma_version.h"
#ifdef Q_OS_WIN
#include "ui/update_manager.h"
#include <windows.h>
#endif
int main(int argc,char **argv){
#ifdef Q_OS_WIN
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#endif
    QApplication app(argc,argv);QApplication::setApplicationName("LumaPlayer");QApplication::setOrganizationName("LumaPlayer");QApplication::setApplicationVersion(LUMA_VERSION);QApplication::setStyle("Fusion");
    QCommandLineParser parser;parser.addHelpOption();parser.addVersionOption();parser.addOption({"open","Open media file","file"});parser.addOption({"disc","Open Blu-ray drive/folder","path"});parser.addOption({"report","Write live diagnostic report and final report","path"});parser.addOption({"exit-after","Exit after N milliseconds (verification)","ms"});parser.addOption({"ai","Enable local original-language captions","language"});parser.addOption({"subtitle","Load UTF-8 SRT or external subtitles","file"});parser.addOption({"prepared-project","Play with a completed caption project","file"});parser.process(app);
    luma::MainWindow window;window.show();const auto source=parser.isSet("disc")?parser.value("disc"):parser.value("open");if(parser.isSet("prepared-project"))QTimer::singleShot(100,&window,[&window,&parser]{window.loadPreparedCaptionProject(parser.value("prepared-project"));});else if(!source.isEmpty())QTimer::singleShot(100,&window,[&window,source]{window.openSource(source);});
    if(!parser.isSet("report") && !parser.isSet("exit-after")){
        window.startLanDiscoveryAtStartup();
#ifdef Q_OS_WIN
        if(auto *updates=window.findChild<luma::UpdateManager*>())updates->checkAtStartup();
#endif
    }
    if(parser.isSet("ai") && !parser.isSet("prepared-project"))QTimer::singleShot(500,&window,[&window,&parser]{window.enableAutomaticSubtitles(parser.value("ai"));});if(parser.isSet("exit-after")){bool ok=false;const auto delay=parser.value("exit-after").toInt(&ok);if(ok && delay>0)QTimer::singleShot(delay,&app,&QCoreApplication::quit);}
    if(parser.isSet("subtitle"))QTimer::singleShot(700,&window,[&]{window.loadCaptionFile(parser.value("subtitle"));});
    const auto writeReport=[&]{if(parser.isSet("report")){QSaveFile file(parser.value("report"));if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(window.diagnostics()).toJson());file.commit();}}};
    QTimer reportTimer;if(parser.isSet("report")){reportTimer.setInterval(1000);QObject::connect(&reportTimer,&QTimer::timeout,&window,writeReport);reportTimer.start();}
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&window,writeReport);
    return app.exec();
}
