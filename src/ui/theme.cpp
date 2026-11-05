#include "theme.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPalette>
#include <QSet>
#include <QSettings>

namespace luma {
bool installApplicationFonts(const QString &runtime){
    const QString root=runtime.isEmpty()?QCoreApplication::applicationDirPath()+"/runtime":runtime;
    static QSet<QString> registered;
    bool complete=true;
    for(const auto &file:{"NotoSans-Regular.ttf","NotoSans-Bold.ttf","NotoSansCJKkr-Regular.otf","NotoSansCJKjp-Regular.otf"}){
        const auto path=QDir(root).filePath("fonts/"+QString(file));
        if(registered.contains(path))continue;
        if(!QFileInfo::exists(path) || QFontDatabase::addApplicationFont(path)<0){complete=false;continue;}
        registered.insert(path);
    }
    return complete;
}
bool playerUsesDarkTheme(){return QSettings().value("ui/theme","dark").toString()!="light";}
void setPlayerDarkTheme(bool dark){QSettings().setValue("ui/theme",dark?"dark":"light");installPlayerTheme();}
void installPlayerTheme(){
    qApp->setProperty("lumaFontsLoaded",installApplicationFonts());
    const bool dark=playerUsesDarkTheme();qApp->setProperty("lumaDarkTheme",dark);
    QFont font;font.setFamilies({"Noto Sans","Noto Sans CJK KR","Noto Sans CJK JP"});font.setPointSize(10);qApp->setFont(font);
    const QString background=dark?"#0b0d11":"#ffffff",panel=dark?"#11141a":"#f7f8fa",elevated=dark?"#1b2029":"#edf0f4";
    const QString text=dark?"#f5f6f8":"#121418",muted=dark?"#9da6b5":"#637083",border=dark?"#29313e":"#dde2e9",hover=dark?"#292f3a":"#e3e9f1",accent=dark?"#62aaff":"#006de5";
    QPalette palette;
    palette.setColor(QPalette::Window,QColor(background));palette.setColor(QPalette::WindowText,QColor(text));palette.setColor(QPalette::Base,QColor(panel));
    palette.setColor(QPalette::AlternateBase,QColor(elevated));palette.setColor(QPalette::Text,QColor(text));palette.setColor(QPalette::Button,QColor(elevated));
    palette.setColor(QPalette::ButtonText,QColor(text));palette.setColor(QPalette::Mid,QColor(border));palette.setColor(QPalette::Midlight,QColor(hover));
    palette.setColor(QPalette::Highlight,QColor("#2189ff"));palette.setColor(QPalette::HighlightedText,Qt::white);palette.setColor(QPalette::PlaceholderText,QColor(muted));
    palette.setColor(QPalette::Disabled,QPalette::Text,QColor(muted));palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor(muted));qApp->setPalette(palette);
    QString css=R"(
      QWidget{background:%1;color:%4;font-family:'Noto Sans','Noto Sans CJK KR','Noto Sans CJK JP';font-size:12px;}
      QMainWindow{background:%1;} QLabel{background:transparent;}
      QPushButton{background:%3;border:1px solid transparent;border-radius:17px;padding:8px 14px;font-weight:600;min-height:18px;}
      QPushButton:hover{background:%7;border-color:%6;} QPushButton:pressed{background:%6;}
      QPushButton:focus{border-color:#2189ff;} QPushButton:disabled{color:%5;}
      QPushButton#primary{background:#2189ff;color:#fff;border:1px solid #2189ff;} QPushButton#primary:hover{background:#459cff;}
      QPushButton#subtle{background:transparent;border:1px solid %6;}
      QPushButton#themeToggle{background:transparent;border:1px solid %6;min-width:74px;}
      QPushButton#iconButton{background:transparent;border:1px solid transparent;border-radius:17px;padding:0;min-height:0;}
      QPushButton#iconButton:hover{background:%3;} QPushButton#iconButton:focus{border-color:#2189ff;}
      QPushButton#playButton{background:#2189ff;border-radius:19px;padding:0;min-height:0;} QPushButton#playButton:hover{background:#459cff;}
      QLabel#brand{font-size:24px;font-weight:700;letter-spacing:3px;} QLabel#muted{color:%5;}
      QLabel#section{font-size:10px;font-weight:700;color:%5;letter-spacing:1px;} QLabel#hero{font-size:38px;font-weight:700;letter-spacing:-1px;}
      QLabel#nowTitle{font-size:14px;font-weight:700;} QLabel#status{color:%5;padding:5px 18px;}
      QLabel#menuStatus{color:%8;background:%3;border-radius:9px;padding:9px;font-size:11px;}
      QLabel#captureStatus,QLabel#displayStatus{color:%5;font-size:11px;}
      QWidget#header{background:%1;border-bottom:1px solid %6;} QWidget#sidebar{background:%2;border-right:1px solid %6;}
      QWidget#settingsPanel{background:%2;border-left:1px solid %6;} QWidget#videoSurface{background:#000;}
      QWidget#welcome{background:%2;border-radius:18px;} QWidget#welcome QLabel{background:transparent;} QWidget#captionOverlay{background:transparent;}
      QWidget#transport{background:%1;border-top:1px solid %6;}
      QComboBox{background:%3;border:1px solid %6;border-radius:12px;padding:9px;min-height:18px;}
      QComboBox QAbstractItemView{background:%2;color:%4;selection-background-color:#2189ff;selection-color:white;padding:6px;}
      QListWidget{background:transparent;border:0;outline:0;} QListWidget::item{padding:9px;border-radius:8px;}
      QListWidget::item:selected{background:%3;color:%8;}
      QPlainTextEdit{background:%1;color:%5;border:1px solid %6;border-radius:10px;padding:6px;font-size:10px;}
      QTabWidget::pane{border:0;background:%2;} QTabBar::tab{background:%2;padding:13px 14px;color:%5;font-weight:600;border-bottom:2px solid transparent;}
      QTabBar::tab:selected{color:%4;border-bottom:2px solid #2189ff;} QTabBar::tab:hover{color:%8;}
      QSlider{background:transparent;} QSlider::groove:horizontal{height:3px;background:%6;border-radius:1px;}
      QSlider::sub-page:horizontal{background:#2189ff;border-radius:1px;}
      QSlider::handle:horizontal{background:%4;width:10px;margin:-4px 0;border-radius:5px;} QSlider::handle:horizontal:hover{background:#2189ff;}
      QCheckBox{background:transparent;spacing:9px;padding:5px 0;}
      QCheckBox::indicator{width:16px;height:16px;border:1px solid %5;border-radius:5px;background:%2;}
      QCheckBox::indicator:checked{background:#2189ff;border:3px solid #2189ff;} QCheckBox:disabled{color:%5;}
      QScrollArea{border:0;background:%2;} QScrollBar:vertical{background:transparent;width:7px;margin:2px;}
      QScrollBar::handle:vertical{background:%6;border-radius:3px;min-height:30px;} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
      QMenu{background:%2;color:%4;border:1px solid %6;border-radius:12px;padding:6px;}
      QMenu::item{padding:9px 24px;border-radius:7px;} QMenu::item:selected{background:%3;color:%8;}
      QMenu::separator{height:1px;background:%6;margin:5px 8px;}
      QToolTip{background:%3;color:%4;border:1px solid %6;border-radius:7px;padding:7px;}
    )";
    qApp->setStyleSheet(css.arg(background,panel,elevated,text,muted,border,hover,accent));
}
}
