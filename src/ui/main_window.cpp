#include "main_window.h"
#include "luma_version.h"
#include <QApplication>
#include <QBoxLayout>
#include <QGridLayout>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QFileDialog>
#include <QFile>
#include <QShortcut>
#include <QTimer>
#include <QStorageInfo>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QSignalBlocker>
#include <QDateTime>
#include <QScrollArea>
#include <QSettings>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif
namespace luma {
namespace {
QLabel *label(const QString &text,const char *name=nullptr){auto *w=new QLabel(text);if(name)w->setObjectName(name);return w;}
QPushButton *button(const QString &text,const char *name=nullptr){auto *w=new QPushButton(text);w->setCursor(Qt::PointingHandCursor);if(name)w->setObjectName(name);return w;}
void syncCombo(QComboBox *box,const QVector<MediaItem> &items){
    QStringList newLabels;for(const auto &item:items)newLabels.append(item.label);
    QStringList oldLabels;for(int i=0;i<box->count();++i)oldLabels.append(box->itemText(i));
    if(oldLabels==newLabels)return;
    QSignalBlocker block(box);const auto selected=box->currentData();box->clear();for(const auto &item:items)box->addItem(item.label,item.id);
    const int index=box->findData(selected);if(index>=0)box->setCurrentIndex(index);
}
}
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),job_({},this),subtitleCache_(QDir::tempPath()+"/luma-captions-XXXXXX"){
    setWindowTitle("LumaPlayer");resize(1440,900);setMinimumSize(1060,680);setAcceptDrops(true);
    buildUi();
    model_=QSettings().value("ai/model",QCoreApplication::applicationDirPath()+"/runtime/models/ggml-base.bin").toString();
    discEngine_=std::make_unique<PlaybackEngine>(PlaybackEngine::Kind::Disc,QString{},this);
    fileEngine_=std::make_unique<PlaybackEngine>(PlaybackEngine::Kind::File,QString{},this);
    for(auto *engine:{discEngine_.get(),fileEngine_.get()}){
        connect(engine,&PlaybackEngine::updated,this,[this,engine]{if(engine==active_)refresh();});
        connect(engine,&PlaybackEngine::logMessage,this,&MainWindow::appendLog);
        connect(engine,&PlaybackEngine::errorOccurred,this,[this,engine](const QString &message){appendLog(message);if(engine==active_){status_->setText(message);status_->setProperty("error",true);}});
        if(!engine->available())appendLog(engine->lastError());
    }
    connect(&job_,&SubtitleJob::progress,aiStatus_,&QLabel::setText);
    connect(&job_,&SubtitleJob::failed,this,[this](const QString &message){coverage_.cancel();aiStatus_->setText(message);QSignalBlocker b(aiEnabled_);aiEnabled_->setChecked(false);appendLog(message);});
    connect(&job_,&SubtitleJob::ready,this,[this](quint64 generation,QVector<SubtitleCue> cues){
        if(generation!=job_.generation() || !active_)return;
        coverage_.complete();generatedCues_+=cues;const QString path=subtitleCache_.filePath("captions.srt");QFile f(path);
        if(f.open(QIODevice::WriteOnly|QIODevice::Truncate)){f.write(writeSrt(generatedCues_).toUtf8());f.close();active_->loadSubtitle(path,true);}
        aiStatus_->setText(QString("자막 %1개 생성 · 기기에서 처리됨").arg(generatedCues_.size()));
    });
    auto *aiTimer=new QTimer(this);aiTimer->setInterval(1000);connect(aiTimer,&QTimer::timeout,this,&MainWindow::scheduleSubtitles);aiTimer->start();
    QTimer::singleShot(0,this,&MainWindow::discoverDisc);
#ifdef Q_OS_WIN
    const BOOL dark=TRUE;DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()),20,&dark,sizeof(dark));
#endif
}
MainWindow::~MainWindow(){job_.cancel();active_=nullptr;fileEngine_.reset();discEngine_.reset();}
void MainWindow::buildUi(){
    qApp->setStyleSheet(R"(
        QWidget{background:#111318;color:#e9ebf2;font-family:'Segoe UI','Noto Sans',sans-serif;font-size:13px;}
        QMainWindow{background:#111318;} QLabel{background:transparent;} QPushButton{background:#20232d;border:1px solid #303441;border-radius:7px;padding:9px 14px;}
        QPushButton:hover{background:#303548;border-color:#6873a1;} QPushButton:pressed{background:#414964;} QPushButton:disabled{color:#606675;}
        QPushButton#primary{background:#b5a7fa;border:0;color:#1b1635;font-weight:600;} QPushButton#primary:hover{background:#d2c7ff;}
        QLabel#brand{font-size:22px;font-weight:700;letter-spacing:4px;} QLabel#muted{color:#8c92a5;} QLabel#section{font-size:11px;color:#858ca0;letter-spacing:2px;}
        QLabel#hero{font-size:42px;font-weight:300;color:#d2c8f5;letter-spacing:5px;} QLabel#nowTitle{font-size:15px;font-weight:600;}
        QWidget#header{background:#171920;border-bottom:1px solid #292c37;} QWidget#sidebar{background:#171920;border-right:1px solid #292c37;}
        QWidget#videoSurface{background:#050608;} QWidget#welcome{background:#050608;} QWidget#welcome QLabel{background:transparent;}
        QLabel#status{color:#a6afc9;padding:6px;} QComboBox{background:#20232d;border:1px solid #343847;border-radius:6px;padding:7px;min-height:20px;}
        QComboBox QAbstractItemView{background:#242938;selection-background-color:#4b4567;} QListWidget{background:#181b23;border:0;outline:0;}
        QListWidget::item{padding:10px;border-radius:5px;} QListWidget::item:selected{background:#38334f;color:#e8dfff;}
        QPlainTextEdit{background:#0c0e13;color:#b2bed4;border:0;font-family:Consolas,monospace;font-size:11px;}
        QTabWidget::pane{border:0;} QTabBar::tab{background:#191c25;padding:11px 13px;color:#8e96ac;} QTabBar::tab:selected{color:#d4c4ff;border-bottom:2px solid #b5a7fa;}
        QSlider::groove:horizontal{height:4px;background:#313642;border-radius:2px;} QSlider::sub-page:horizontal{background:#b6a7f2;border-radius:2px;}
        QSlider::handle:horizontal{background:#ebe5ff;width:12px;margin:-4px 0;border-radius:6px;} QCheckBox{spacing:8px;padding:6px 0;}
        QScrollArea{border:0;} QToolTip{background:#292d3d;color:white;border:1px solid #555b76;padding:4px;}
    )");
    auto *central=new QWidget;setCentralWidget(central);auto *outer=new QVBoxLayout(central);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    header_=new QWidget;header_->setObjectName("header");auto *top=new QHBoxLayout(header_);top->setContentsMargins(24,15,24,15);top->addWidget(label("LUMA", "brand"));top->addWidget(label("PLAYER  /  PREVIEW 0.1", "muted"));top->addStretch();
    auto *open=button("파일 열기");open->setToolTip("Ctrl+O");top->addWidget(open);discButton_=button("Blu-ray 재생","primary");top->addWidget(discButton_);outer->addWidget(header_);
    auto *body=new QHBoxLayout;body->setSpacing(0);body->setContentsMargins(0,0,0,0);outer->addLayout(body,1);
    sidebar_=new QWidget;sidebar_->setObjectName("sidebar");sidebar_->setFixedWidth(198);auto *nav=new QVBoxLayout(sidebar_);nav->setContentsMargins(18,23,18,20);nav->setSpacing(14);nav->addWidget(label("라이브러리","section"));
    auto *files=button("로컬 미디어");nav->addWidget(files);nav->addSpacing(18);nav->addWidget(label("연결된 디스크","section"));discLabel_=label("디스크 확인 중…");discLabel_->setWordWrap(true);nav->addWidget(discLabel_);discDetails_=label("","muted");discDetails_->setWordWrap(true);nav->addWidget(discDetails_);
    auto *rescan=button("새로 확인");nav->addWidget(rescan);nav->addStretch();nav->addWidget(label("WINDOWS FIRST\nOPEN SOURCE","section"));body->addWidget(sidebar_);
    auto *center=new QWidget;auto *centerLayout=new QVBoxLayout(center);centerLayout->setContentsMargins(0,0,0,0);centerLayout->setSpacing(0);
    surface_=new QWidget;surface_->setObjectName("videoSurface");surface_->setAttribute(Qt::WA_NativeWindow);surface_->setFocusPolicy(Qt::StrongFocus);surface_->setMinimumSize(500,280);
    auto *videoLayout=new QVBoxLayout(surface_);welcome_=new QWidget;welcome_->setObjectName("welcome");auto *welcomeLayout=new QVBoxLayout(welcome_);welcomeLayout->addStretch();auto *hero=label("◯  LUMA","hero");hero->setAlignment(Qt::AlignCenter);welcomeLayout->addWidget(hero);auto *intro=label("당신의 화면에, 오롯이.","muted");intro->setAlignment(Qt::AlignCenter);welcomeLayout->addWidget(intro);welcomeLayout->addSpacing(20);auto *hint=label("미디어 파일을 놓거나 Blu-ray를 재생하세요.","muted");hint->setAlignment(Qt::AlignCenter);welcomeLayout->addWidget(hint);welcomeLayout->addStretch();videoLayout->addWidget(welcome_);centerLayout->addWidget(surface_,1);
    auto *transport=new QWidget;auto *transportLayout=new QVBoxLayout(transport);transportLayout->setContentsMargins(22,15,22,16);auto *titleRow=new QHBoxLayout;nowTitle_=label("재생할 미디어를 선택하세요","nowTitle");titleRow->addWidget(nowTitle_,1);codec_=label("","muted");titleRow->addWidget(codec_);transportLayout->addLayout(titleRow);
    auto *timeRow=new QHBoxLayout;seek_=new QSlider(Qt::Horizontal);seek_->setRange(0,10000);timeRow->addWidget(seek_,1);clock_=label("00:00 / 00:00","muted");clock_->setMinimumWidth(132);timeRow->addWidget(clock_);transportLayout->addLayout(timeRow);
    auto *buttons=new QHBoxLayout;auto *previous=button("|◀");previous->setToolTip("이전 챕터");auto *back=button("−10s");play_=button("▶","primary");play_->setMinimumWidth(60);play_->setToolTip("재생 / 일시정지 · Space");auto *forward=button("+10s");auto *next=button("▶|");next->setToolTip("다음 챕터");
    for(auto *b:{previous,back,play_,forward,next})buttons->addWidget(b);buttons->addStretch();rate_=new QComboBox;for(const auto r:{0.5,0.75,1.0,1.25,1.5,2.0})rate_->addItem(QString::number(r)+"×",r);rate_->setCurrentIndex(2);buttons->addWidget(rate_);buttons->addWidget(label("음량","muted"));volume_=new QSlider(Qt::Horizontal);volume_->setRange(0,100);volume_->setValue(70);volume_->setFixedWidth(85);buttons->addWidget(volume_);auto *full=button("전체 화면");buttons->addWidget(full);transportLayout->addLayout(buttons);centerLayout->addWidget(transport);status_=label("준비됨","status");status_->setWordWrap(true);centerLayout->addWidget(status_);body->addWidget(center,1);
    panel_=new QWidget;panel_->setFixedWidth(280);auto *panelLayout=new QVBoxLayout(panel_);panelLayout->setContentsMargins(0,0,0,0);auto *tabs=new QTabWidget;panelLayout->addWidget(tabs);body->addWidget(panel_);
    auto *discTab=new QWidget;auto *discLayout=new QVBoxLayout(discTab);discLayout->setContentsMargins(16,18,16,14);discLayout->setSpacing(10);discLayout->addWidget(label("DISC REMOTE","section"));auto *menus=new QHBoxLayout;auto *topMenu=button("메인 메뉴");auto *popup=button("팝업");menus->addWidget(topMenu);menus->addWidget(popup);discLayout->addLayout(menus);
    auto *remote=new QGridLayout;const QList<QPair<QString,QPoint>> keys={{"↑",{1,0}},{"←",{0,1}},{"확인",{1,1}},{"→",{2,1}},{"↓",{1,2}}};const int modes[]={1,3,0,4,2};
    for(int i=0;i<keys.size();++i){auto *b=button(keys[i].first);b->setAccessibleName("디스크 "+keys[i].first);remote->addWidget(b,keys[i].second.y(),keys[i].second.x());connect(b,&QPushButton::clicked,this,[this,i,modes]{if(active_)active_->navigate(modes[i]);});}discLayout->addLayout(remote);
    discLayout->addWidget(label("타이틀","section"));titles_=new QComboBox;discLayout->addWidget(titles_);discLayout->addWidget(label("오디오","section"));audio_=new QComboBox;discLayout->addWidget(audio_);discLayout->addWidget(label("자막","section"));subs_=new QComboBox;discLayout->addWidget(subs_);auto *external=button("자막 파일 추가");discLayout->addWidget(external);discLayout->addWidget(label("챕터","section"));chapters_=new QListWidget;discLayout->addWidget(chapters_,1);tabs->addTab(discTab,"디스크");
    auto *aiTab=new QWidget;auto *aiLayout=new QVBoxLayout(aiTab);aiLayout->setContentsMargins(18,20,18,18);aiLayout->setSpacing(15);aiLayout->addWidget(label("LOCAL AI CAPTIONS","section"));auto *aiIntro=label("음성을 자막으로.\n내 기기에서 자동으로.","nowTitle");aiIntro->setWordWrap(true);aiLayout->addWidget(aiIntro);aiEnabled_=new QCheckBox("AI 자동자막 켜기");aiLayout->addWidget(aiEnabled_);aiLayout->addWidget(label("음성 언어","section"));language_=new QComboBox;language_->addItem("日本語 · 일본어","ja");language_->addItem("English · 영어","en");language_->addItem("한국어","ko");language_->addItem("자동 감지","auto");aiLayout->addWidget(language_);auto *model=button("음성인식 모델 선택");aiLayout->addWidget(model);auto *aiNote=label("원어 자막을 생성합니다. 노래와 배경음이 강한 구간은 정확도가 낮을 수 있습니다.","muted");aiNote->setWordWrap(true);aiLayout->addWidget(aiNote);aiStatus_=label("꺼짐","muted");aiStatus_->setWordWrap(true);aiLayout->addWidget(aiStatus_);aiLayout->addStretch();auto *save=button("생성한 자막 저장");aiLayout->addWidget(save);tabs->addTab(aiTab,"AI 자막");
    auto *diagnosticTab=new QWidget;auto *diagnosticLayout=new QVBoxLayout(diagnosticTab);diagnosticLayout->setContentsMargins(10,10,10,10);diagnosticLayout->addWidget(label("PLAYBACK LOG","section"));log_=new QPlainTextEdit;log_->setReadOnly(true);log_->setMaximumBlockCount(500);diagnosticLayout->addWidget(log_);tabs->addTab(diagnosticTab,"진단");
    connect(open,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"미디어 열기",{},"미디어 (*.mkv *.mp4 *.webm *.mov *.avi *.m2ts *.ts *.flac *.mp3 *.wav);;모든 파일 (*)");if(!path.isEmpty())openSource(path);});connect(files,&QPushButton::clicked,open,&QPushButton::click);
    connect(rescan,&QPushButton::clicked,this,&MainWindow::discoverDisc);connect(discButton_,&QPushButton::clicked,this,[this]{if(discPath_.isEmpty())discoverDisc();if(!discPath_.isEmpty())openSource(discPath_);else status_->setText("연결된 Blu-ray를 찾지 못했습니다.");});
    connect(play_,&QPushButton::clicked,this,[this]{if(active_)active_->pauseToggle();});connect(back,&QPushButton::clicked,this,[this]{if(active_){cancelSubtitleWork();active_->seek(active_->state().positionMs-10000);}});connect(forward,&QPushButton::clicked,this,[this]{if(active_){cancelSubtitleWork();active_->seek(active_->state().positionMs+10000);}});
    connect(previous,&QPushButton::clicked,this,[this]{if(active_){cancelSubtitleWork();active_->setChapter(std::max(0,active_->state().currentChapter-1));}});connect(next,&QPushButton::clicked,this,[this]{if(active_){cancelSubtitleWork();active_->setChapter(active_->state().currentChapter+1);}});
    connect(seek_,&QSlider::sliderReleased,this,[this]{if(active_){cancelSubtitleWork();active_->seek(active_->state().durationMs*seek_->value()/10000);}});connect(volume_,&QSlider::valueChanged,this,[this](int v){if(active_)active_->setVolume(v);});connect(rate_,&QComboBox::activated,this,[this]{if(active_)active_->setRate(rate_->currentData().toDouble());});
    connect(full,&QPushButton::clicked,this,&MainWindow::toggleFullScreen);connect(topMenu,&QPushButton::clicked,this,[this]{if(active_)active_->setTitle(0);});connect(popup,&QPushButton::clicked,this,[this]{if(active_)active_->navigate(discActionCode(DiscAction::Popup));});
    connect(titles_,&QComboBox::activated,this,[this]{if(active_)active_->setTitle(titles_->currentData().toInt());});connect(audio_,&QComboBox::activated,this,[this]{if(active_){resetSubtitleSession();active_->setAudio(audio_->currentData().toInt());}});connect(subs_,&QComboBox::activated,this,[this]{if(active_)active_->setSubtitle(subs_->currentData().toInt());});connect(chapters_,&QListWidget::itemActivated,this,[this](QListWidgetItem *item){if(active_){cancelSubtitleWork();active_->setChapter(item->data(Qt::UserRole).toInt());}});
    connect(external,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"자막 열기",{},"자막 (*.srt *.ass *.ssa *.vtt *.sup)");if(active_ && !path.isEmpty())active_->loadSubtitle(path);});
    connect(model,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"Whisper 모델",{},"Whisper 모델 (*.bin)");if(!path.isEmpty()){resetSubtitleSession();model_=path;QSettings().setValue("ai/model",path);aiStatus_->setText("모델 선택됨");}});
    connect(aiEnabled_,&QCheckBox::toggled,this,[this](bool enabled){if(enabled){const auto path=subtitleCache_.filePath("captions.srt");if(active_ && !generatedCues_.isEmpty())active_->loadSubtitle(path,true);scheduleSubtitles();}else{cancelSubtitleWork();if(active_)active_->clearManagedSubtitles();aiStatus_->setText("꺼짐");}});
    connect(save,&QPushButton::clicked,this,[this]{if(generatedCues_.isEmpty()){aiStatus_->setText("먼저 자막을 생성하세요.");return;}const auto path=QFileDialog::getSaveFileName(this,"자막 저장","captions.srt","SRT (*.srt)");if(!path.isEmpty()){QFile f(path);if(f.open(QIODevice::WriteOnly))f.write(writeSrt(generatedCues_).toUtf8());}});
    auto shortcut=[this](const QKeySequence &key,auto action){auto *s=new QShortcut(key,this);s->setContext(Qt::ApplicationShortcut);connect(s,&QShortcut::activated,this,action);};
    shortcut(QKeySequence("Ctrl+O"),[open]{open->click();});shortcut(QKeySequence("Ctrl+D"),[this]{discButton_->click();});shortcut(QKeySequence("Space"),[this]{if(active_)active_->pauseToggle();});shortcut(QKeySequence("F11"),[this]{toggleFullScreen();});shortcut(QKeySequence("Escape"),[this]{if(full_)toggleFullScreen();});
    const QList<QPair<QString,DiscAction>> arrows={{"Up",DiscAction::Up},{"Down",DiscAction::Down},{"Left",DiscAction::Left},{"Right",DiscAction::Right},{"Return",DiscAction::Activate}};
    for(const auto &key:arrows){auto *s=new QShortcut(QKeySequence(key.first),surface_);s->setContext(Qt::WidgetWithChildrenShortcut);s->setEnabled(false);discShortcuts_.append(s);connect(s,&QShortcut::activated,this,[this,action=key.second]{if(active_==discEngine_.get())active_->navigate(discActionCode(action));});}
    connect(language_,&QComboBox::activated,this,[this]{resetSubtitleSession();});
    shortcut(QKeySequence("Shift+Left"),[back]{back->click();});shortcut(QKeySequence("Shift+Right"),[forward]{forward->click();});
}
void MainWindow::discoverDisc(){
    discPath_.clear();
#ifdef Q_OS_WIN
    for(wchar_t drive=L'A';drive<=L'Z';++drive){const QString root=QString(QChar(drive))+":/";if(GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()))==DRIVE_CDROM && QFileInfo::exists(root+"BDMV/index.bdmv")){discPath_=root;break;}}
#else
    for(const auto &volume:QStorageInfo::mountedVolumes())if(QFileInfo::exists(volume.rootPath()+"/BDMV/index.bdmv")){discPath_=volume.rootPath();break;}
#endif
    if(discPath_.isEmpty()){discLabel_->setText("연결된 디스크 없음");discDetails_->clear();return;}
    discInfo_=inspectDisc(discPath_);discLabel_->setText(discInfo_.label.replace('_',' '));discDetails_->setText(QString("%1  ·  %2\n%3개 플레이리스트\n%4").arg(discPath_,discInfo_.menuKind).arg(discInfo_.titles.size()).arg(discInfo_.aacs?"AACS 보호 감지":"보호되지 않은 디스크"));
}
void MainWindow::openSource(const QString &source){
    resetSubtitleSession();if(active_)active_->stop();source_=source;lastTitle_=-1;decodedSamples_=0;
    active_=isDiscSource(source)?discEngine_.get():fileEngine_.get();nowTitle_->setText(isDiscSource(source)?(discInfo_.label.isEmpty()?"Blu-ray":discInfo_.label):QFileInfo(source).fileName());
    for(auto *shortcut:discShortcuts_)shortcut->setEnabled(active_==discEngine_.get());surface_->setFocus(Qt::OtherFocusReason);welcome_->hide();status_->setText("미디어를 여는 중…");if(active_->open(source,surface_->winId())){active_->setVolume(volume_->value());active_->setRate(rate_->currentData().toDouble());}else{status_->setText(active_->lastError());welcome_->show();}
}
void MainWindow::enableAutomaticSubtitles(const QString &language){const int index=language_->findData(language);if(index>=0){language_->setCurrentIndex(index);aiEnabled_->setChecked(true);}}
void MainWindow::refresh(){
    if(!active_)return;const auto &s=active_->state();if(s.playing)++decodedSamples_;
    if(s.currentTitle!=lastTitle_ && active_==discEngine_.get()){resetSubtitleSession();lastTitle_=s.currentTitle;}
    play_->setText(s.playing?"Ⅱ":"▶");clock_->setText(formatTime(s.positionMs)+" / "+formatTime(s.durationMs));if(!seek_->isSliderDown()){QSignalBlocker b(seek_);seek_->setValue(s.durationMs>0?int(s.positionMs*10000/s.durationMs):0);}
    codec_->setText(QStringList{active_->name(),s.videoCodec,s.hardwareDecoder}.filter(QRegularExpression(".+")).join(" · "));
    if(!s.failed)status_->setText(s.playing?"재생 중":"일시정지 / 준비 중");syncCombo(titles_,s.titles);syncCombo(audio_,s.audioTracks);syncCombo(subs_,s.subtitleTracks);
    const auto select=[](QComboBox *box,int id){const int index=box->findData(id);if(index>=0 && index!=box->currentIndex()){QSignalBlocker guard(box);box->setCurrentIndex(index);}};select(titles_,s.currentTitle);select(audio_,s.currentAudio);select(subs_,s.currentSubtitle);
    if(chapters_->count()!=s.chapters.size()){chapters_->clear();for(const auto &c:s.chapters){auto *item=new QListWidgetItem(c.label.isEmpty()?QString("Chapter %1").arg(c.id+1):c.label);item->setData(Qt::UserRole,c.id);chapters_->addItem(item);}}
}
void MainWindow::appendLog(const QString &message){log_->appendPlainText(QTime::currentTime().toString("HH:mm:ss")+"  "+message);}
void MainWindow::toggleFullScreen(){full_=!full_;sidebar_->setVisible(!full_);panel_->setVisible(!full_);header_->setVisible(!full_);if(full_)showFullScreen();else showNormal();}
void MainWindow::cancelSubtitleWork(){job_.cancel();coverage_.cancel();}
void MainWindow::resetSubtitleSession(){cancelSubtitleWork();if(active_)active_->clearManagedSubtitles();generatedCues_.clear();coverage_.reset();}
void MainWindow::scheduleSubtitles(){
    if(!aiEnabled_->isChecked() || !active_ || job_.busy())return;const auto &s=active_->state();if(s.durationMs<1000 || s.failed)return;
    if(s.currentAudio<0)return;const auto chunk=coverage_.begin(s.positionMs,s.durationMs);if(!chunk)return;
    RecognitionRequest r;r.source=source_;r.disc=active_==discEngine_.get();r.title=std::max(1,s.currentTitle);r.language=language_->currentData().toString();r.startMs=*chunk;r.durationMs=std::min<qint64>(30000,s.durationMs-*chunk);r.audioTrackId=s.currentAudio;
    if(r.disc)r.playlist=matchDiscPlaylist(discInfo_,s.durationMs);
    int audioIndex=0;for(const auto &track:s.audioTracks){if(track.id<0)continue;if(track.id==s.currentAudio){r.audioIndex=audioIndex;break;}++audioIndex;}
    job_.start(r,model_);
}
QJsonObject MainWindow::diagnostics()const{
    QJsonObject result{{"version",LUMA_VERSION},{"playingSamples",decodedSamples_},{"discRuntime",discEngine_->available()},{"fileRuntime",fileEngine_->available()},{"aiCueCount",generatedCues_.size()}};
    if(active_){const auto &s=active_->state();result.insert("engine",active_->name());result.insert("positionMs",s.positionMs);result.insert("durationMs",s.durationMs);result.insert("playing",s.playing);result.insert("failed",s.failed);result.insert("title",s.currentTitle);result.insert("chapter",s.currentChapter);result.insert("chapterCount",s.chapters.size());result.insert("audioTrackCount",s.audioTracks.size());result.insert("videoCodec",s.videoCodec);result.insert("hardwareDecoder",s.hardwareDecoder);result.insert("audioCodec",s.audioCodec);result.insert("decodedVideo",s.decodedVideo);result.insert("decodedAudio",s.decodedAudio);result.insert("displayedPictures",s.displayedPictures);result.insert("playedAudioBuffers",s.playedAudioBuffers);}
    return result;
}
void MainWindow::dragEnterEvent(QDragEnterEvent *event){if(event->mimeData()->hasUrls())event->acceptProposedAction();}
void MainWindow::dropEvent(QDropEvent *event){const auto urls=event->mimeData()->urls();if(!urls.isEmpty()){openSource(urls.first().isLocalFile()?urls.first().toLocalFile():urls.first().toString());event->acceptProposedAction();}}
}
