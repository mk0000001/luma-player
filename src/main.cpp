#include <QApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QFile>
#include <QTimer>
#include "ui/main_window.h"
#include "luma_version.h"
int main(int argc,char **argv){
    QApplication app(argc,argv);QApplication::setApplicationName("LumaPlayer");QApplication::setOrganizationName("LumaPlayer");QApplication::setApplicationVersion(LUMA_VERSION);QApplication::setStyle("Fusion");
    QCommandLineParser parser;parser.addHelpOption();parser.addVersionOption();parser.addOption({"open","Open media file","file"});parser.addOption({"disc","Open Blu-ray drive/folder","path"});parser.addOption({"report","Write diagnostic report on exit","path"});parser.addOption({"exit-after","Exit after N milliseconds (verification)","ms"});parser.addOption({"ai","Enable local original-language captions","language"});parser.process(app);
    luma::MainWindow window;window.show();const auto source=parser.isSet("disc")?parser.value("disc"):parser.value("open");if(!source.isEmpty())QTimer::singleShot(100,&window,[&window,source]{window.openSource(source);});
    if(parser.isSet("ai"))QTimer::singleShot(500,&window,[&window,&parser]{window.enableAutomaticSubtitles(parser.value("ai"));});if(parser.isSet("exit-after")){bool ok=false;const auto delay=parser.value("exit-after").toInt(&ok);if(ok && delay>0)QTimer::singleShot(delay,&app,&QCoreApplication::quit);}
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&window,[&]{if(parser.isSet("report")){QFile file(parser.value("report"));if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(window.diagnostics()).toJson());}});
    return app.exec();
}
