#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <cstdio>

// Test-only device inventory fixture. Never loads a model or GPU runtime.
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(app.arguments()!=QStringList{app.arguments().first(),"--list-devices"})return 2;
    QFile mode(QCoreApplication::applicationDirPath()+"/probe-mode.txt");
    if(!mode.open(QIODevice::ReadOnly))return 3;
    const auto behavior=mode.readAll();
    if(behavior=="stall"){
        QTimer::singleShot(30000,&app,[]{QCoreApplication::exit(4);});
        return app.exec();
    }
    if(behavior!="unicode")return 5;
    QFile output;
    if(!output.open(stdout,QIODevice::WriteOnly))return 6;
    const auto inventory=QString::fromUtf8("Available devices:\n Vulkan0: Intel Arc 日本語 (16000 MiB, 15000 MiB free)\n").toUtf8();
    if(output.write(inventory)!=inventory.size() || !output.flush())return 7;
    return 0;
}
