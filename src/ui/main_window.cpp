#include "main_window.h"
#include "luma_version.h"
#include "caption_overlay.h"
#include "fullscreen_controls.h"
#include "player_controls.h"
#include "theme.h"
#include "media/platform_features.h"
#include "subtitles/ai_devices.h"
#include "subtitles/translation_service.h"
#include <QApplication>
#include <QBoxLayout>
#include <QGridLayout>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
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
#include <QSaveFile>
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace luma {
namespace {
QLabel *label(const QString &text,const char *name=nullptr){auto *widget=new QLabel(text);if(name)widget->setObjectName(name);return widget;}
QPushButton *button(const QString &text,const char *name=nullptr){auto *widget=new QPushButton(text);widget->setCursor(Qt::PointingHandCursor);if(name)widget->setObjectName(name);return widget;}
void syncCombo(QComboBox *box,const QVector<MediaItem> &items){
    QStringList expected,current;for(const auto &item:items)expected.append(item.label);for(int i=0;i<box->count();++i)current.append(box->itemText(i));
    if(current==expected)return;
    QSignalBlocker guard(box);const auto selected=box->currentData();box->clear();for(const auto &item:items)box->addItem(item.label,item.id);
    const int index=box->findData(selected);if(index>=0)box->setCurrentIndex(index);
}
QScrollArea *scrollPanel(QWidget *contents){auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setWidget(contents);return scroll;}
void setSavedChoice(QComboBox *box,const QString &key,const QString &fallback={}){const auto saved=QSettings().value(key,fallback).toString();for(int i=0;i<box->count();++i)if(box->itemData(i).toString()==saved){box->setCurrentIndex(i);break;}}
}

MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),job_({},this),subtitleCache_(QDir::tempPath()+"/luma-captions-XXXXXX"){
    setWindowTitle("LumaPlayer");resize(1440,900);setMinimumSize(1080,680);setAcceptDrops(true);setWindowIcon(playerApplicationIcon());
    buildUi();captions_=new CaptionOverlay(surface_);fullscreenControls_=new FullscreenControls(this,transport_,surface_);
    captureGuard_=new CaptureGuard(this);captureGuard_->watch(this);captureGuard_->watch(captions_);
    model_=QSettings().value("ai/model",QCoreApplication::applicationDirPath()+"/runtime/models/ggml-base.bin").toString();
    translation_=new TranslationService({},this);
    translation_->setModel(QSettings().value("translation/model").toString());
    connect(translation_,&TranslationService::translated,this,[this](quint64 generation,const QString &source,const QString &korean){
        if(generation!=translation_->generation() || !translationEnabled_->isChecked())return;
        translationPending_.remove(source);translations_.insert(source,korean);rebuildManagedCaptions();translationStatus_->setText(QString("한국어 번역 %1개 준비됨").arg(translations_.size()));
    });
    connect(translation_,&TranslationService::skipped,this,[this](quint64 generation,const QString &source,const QString &message){
        if(generation!=translation_->generation() || !translationEnabled_->isChecked())return;
        translationPending_.remove(source);
        if(!translationSkipped_.contains(source)){
            translationSkipped_.insert(source);translationSkippedOrder_.append(source);
            while(translationSkippedOrder_.size()>256)translationSkipped_.remove(translationSkippedOrder_.takeFirst());
        }
        translationStatus_->setText("이 자막의 번역을 건너뛰고 다음 구간을 계속 준비합니다.\n"+message);
        appendLog("번역 한 구간 제외: "+message);QTimer::singleShot(0,this,&MainWindow::scheduleTranslations);
    });
    connect(translation_,&TranslationService::progress,translationStatus_,&QLabel::setText);
    connect(translation_,&TranslationService::backendSelected,this,[this](const QString &backend){translationBackend_->setText("번역 · "+backend);});
    connect(translation_,&TranslationService::failed,this,[this](const QString &message){translationPending_.clear();translationFailed_=true;translationStatus_->setText(message);appendLog("번역: "+message);});
    discEngine_=std::make_unique<PlaybackEngine>(PlaybackEngine::Kind::Disc,QString{},this);
    fileEngine_=std::make_unique<PlaybackEngine>(PlaybackEngine::Kind::File,QString{},this);bindEngine(discEngine_.get());bindEngine(fileEngine_.get());
    connect(&job_,&SubtitleJob::progress,this,[this](const QString &message){aiStatus_->setText(message);});
    connect(&job_,&SubtitleJob::backendSelected,this,[this](const QString &backend){
        accelerator_->setText(backend=="NPU"?"음성 인코더 · NPU (실행 확인)":backend=="GPU"?"음성 인식 · GPU (실행 확인)":"음성 인식 · "+backend+" (실행 확인)");
    });
    connect(&job_,&SubtitleJob::failed,this,[this](const QString &message){
        coverage_.cancel();aiFailed_=true;appendLog(message);
        if(aiEnabled_->isChecked() && aiRetryCount_<3){
            const int delay=2000*(1<<aiRetryCount_++);nextAiAttemptMs_=QDateTime::currentMSecsSinceEpoch()+delay;
            aiStatus_->setText(message+QString("\n%1초 후 다시 시도합니다. (%2/3)").arg(delay/1000).arg(aiRetryCount_));
            bufferStatus_->setText("자막 준비 재시도 중 · ‘자막 없이 계속’으로 바로 재생할 수 있습니다.");
        }else{
            nextAiAttemptMs_=0;bufferBypass_=true;releaseCaptionBuffer(true);
            aiStatus_->setText(message+"\n자동 재시도를 마쳤습니다. 모델·오디오를 확인하고 다시 시도하세요.");
            bufferStatus_->setText("자막 준비에 실패하여 재생을 계속합니다. AI 설정은 켜져 있습니다.");
        }
        status_->setText(bufferStatus_->text());
    });
    connect(&job_,&SubtitleJob::ready,this,[this](quint64 generation,QVector<SubtitleCue> cues){
        if(generation!=job_.generation() || !active_ || !aiEnabled_->isChecked())return;
        coverage_.complete();aiFailed_=false;aiRetryCount_=0;nextAiAttemptMs_=0;generatedCues_+=cues;rebuildManagedCaptions();updateCaptionBuffer();
        aiStatus_->setText(QString("자막 %1개 생성 · 기기에서 처리됨").arg(generatedCues_.size()));
        QTimer::singleShot(0,this,&MainWindow::scheduleSubtitles);
    });
    updateAiPreferences();updateMotionSettings();
    deviceProbe_=new AiDeviceProbe({},this);
    connect(deviceProbe_,&AiDeviceProbe::devicesReady,this,[this](const QJsonArray &devices){
        const auto selected=QSettings().value("ai/gpu").toString();QSignalBlocker guard(aiGpu_);aiGpu_->clear();aiGpu_->addItem("자동 GPU 선택",QString{});
        for(const auto &entry:devices){const auto device=entry.toObject();if(device.value("type").toString().compare("GPU",Qt::CaseInsensitive)!=0)continue;aiGpu_->addItem(device.value("name").toString()+" · "+device.value("backend").toString(),device.value("id").toString());}
        int index=aiGpu_->findData(selected);if(index<0 && !selected.isEmpty()){aiGpu_->addItem("저장된 GPU · "+selected,selected);index=aiGpu_->count()-1;}if(index>=0)aiGpu_->setCurrentIndex(index);
    });
    connect(deviceProbe_,&AiDeviceProbe::failed,this,[this](const QString &message){appendLog("AI 기기 검색: "+message);aiGpu_->setToolTip(message);});
    auto *timer=new QTimer(this);timer->setInterval(500);connect(timer,&QTimer::timeout,this,[this]{scheduleSubtitles();scheduleTranslations();});timer->start();
    QTimer::singleShot(0,this,[this]{discoverDisc();deviceProbe_->start();updateDisplayStatus();applyTheme();});
}
MainWindow::~MainWindow(){captions_->hide();job_.shutdown();translation_->cancel();active_=nullptr;fileEngine_.reset();discEngine_.reset();}
void MainWindow::bindEngine(PlaybackEngine *engine){
    connect(engine,&PlaybackEngine::updated,this,[this,engine]{if(engine==active_)refresh();});connect(engine,&PlaybackEngine::logMessage,this,&MainWindow::appendLog);
    connect(engine,&PlaybackEngine::errorOccurred,this,[this,engine](const QString &message){appendLog(message);if(engine==active_){status_->setText(message);status_->setProperty("error",true);}});
    if(!engine->available())appendLog(engine->lastError());
}

void MainWindow::buildUi(){
    installPlayerTheme();
    auto *central=new QWidget;setCentralWidget(central);auto *outer=new QVBoxLayout(central);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    header_=new QWidget;header_->setObjectName("header");auto *top=new QHBoxLayout(header_);top->setContentsMargins(24,13,24,13);top->setSpacing(16);
    top->addWidget(new BrandMark);top->addWidget(label("나만의 시네마","muted"));top->addStretch();
    themeButton_=button("라이트 모드","themeToggle");themeButton_->setAccessibleName("화면 테마 전환");top->addWidget(themeButton_);
    auto *open=button("파일 열기","subtle");open->setToolTip("미디어 열기 · Ctrl+O");top->addWidget(open);discButton_=button("Blu-ray 재생","primary");top->addWidget(discButton_);outer->addWidget(header_);
    auto *body=new QHBoxLayout;body->setSpacing(0);body->setContentsMargins(0,0,0,0);outer->addLayout(body,1);
    sidebar_=new QWidget;sidebar_->setObjectName("sidebar");sidebar_->setFixedWidth(174);auto *nav=new QVBoxLayout(sidebar_);nav->setContentsMargins(17,25,17,20);nav->setSpacing(14);
    nav->addWidget(label("YOUR COLLECTION","section"));auto *files=button("로컬 미디어");nav->addWidget(files);nav->addSpacing(22);nav->addWidget(label("연결된 디스크","section"));
    discLabel_=label("디스크 확인 중…");discLabel_->setWordWrap(true);nav->addWidget(discLabel_);discDetails_=label({},"muted");discDetails_->setWordWrap(true);nav->addWidget(discDetails_);
    auto *rescan=button("새로 확인","subtle");nav->addWidget(rescan);nav->addStretch();nav->addWidget(label("MADE FOR\nYOUR SCREEN","section"));body->addWidget(sidebar_);
    auto *center=new QWidget;auto *centerLayout=new QVBoxLayout(center);centerLayout->setContentsMargins(0,0,0,0);centerLayout->setSpacing(0);
    surface_=new QWidget;surface_->setObjectName("videoSurface");surface_->setAttribute(Qt::WA_NativeWindow);surface_->setFocusPolicy(Qt::StrongFocus);surface_->setMinimumSize(500,280);surface_->setMouseTracking(true);
    auto *videoLayout=new QVBoxLayout(surface_);videoLayout->setContentsMargins(20,20,20,20);welcome_=new QWidget;welcome_->setObjectName("welcome");auto *welcomeLayout=new QVBoxLayout(welcome_);welcomeLayout->setContentsMargins(28,28,28,28);
    welcomeLayout->addStretch();auto *hero=label("모든 순간을,\n더 가까이.","hero");hero->setAlignment(Qt::AlignCenter);welcomeLayout->addWidget(hero);
    auto *intro=label("LumaPlayer · Blu-ray & Media","muted");intro->setAlignment(Qt::AlignCenter);welcomeLayout->addWidget(intro);welcomeLayout->addSpacing(18);
    auto *hint=label("미디어 파일을 놓거나 Blu-ray를 재생하세요.","muted");hint->setAlignment(Qt::AlignCenter);hint->setWordWrap(true);welcomeLayout->addWidget(hint);welcomeLayout->addStretch();videoLayout->addWidget(welcome_);centerLayout->addWidget(surface_,1);
    transport_=new QWidget;transport_->setObjectName("transport");transport_->setFixedHeight(108);auto *transportLayout=new QVBoxLayout(transport_);transportLayout->setContentsMargins(18,7,18,7);transportLayout->setSpacing(3);
    transportTitleRow_=new QWidget;auto *titleRow=new QHBoxLayout(transportTitleRow_);titleRow->setContentsMargins(0,0,0,0);nowTitle_=label("재생할 미디어를 선택하세요","nowTitle");nowTitle_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);titleRow->addWidget(nowTitle_,1);codec_=label({},"muted");titleRow->addWidget(codec_);transportLayout->addWidget(transportTitleRow_);
    seek_=new QSlider(Qt::Horizontal);seek_->setObjectName("seekSlider");seek_->setAccessibleName("재생 위치");seek_->setRange(0,10000);seek_->setFixedHeight(12);transportLayout->addWidget(seek_);
    auto *buttons=new QHBoxLayout;buttons->setSpacing(6);auto *previous=new IconButton(PlayerIcon::Previous,"이전 챕터");auto *back=new IconButton(PlayerIcon::Rewind,"10초 뒤로 · Shift+Left");
    auto *play=new IconButton(PlayerIcon::Play,"재생 / 일시정지 · Space");play_=play;play->setObjectName("playButton");play->setFixedSize(38,38);
    auto *forward=new IconButton(PlayerIcon::Forward,"10초 앞으로 · Shift+Right");auto *next=new IconButton(PlayerIcon::Next,"다음 챕터");
    for(auto *control:{previous,back,play,forward,next})buttons->addWidget(control);
    clock_=label("00:00 / 00:00","muted");clock_->setMinimumWidth(115);buttons->addSpacing(5);buttons->addWidget(clock_);buttons->addStretch();
    rate_=new ChoiceBox;rate_->setObjectName("playbackRate");rate_->setAccessibleName("재생 속도");for(double speed:{.5,.75,1.,1.25,1.5,2.})rate_->addItem(QString::number(speed)+"×",speed);rate_->setCurrentIndex(2);rate_->setMinimumHeight(30);rate_->setFixedSize(70,32);buttons->addWidget(rate_);
    auto *volumeIcon=new IconButton(PlayerIcon::Volume,"음량");volumeIcon->setFocusPolicy(Qt::NoFocus);buttons->addWidget(volumeIcon);volume_=new QSlider(Qt::Horizontal);volume_->setObjectName("volumeSlider");volume_->setAccessibleName("음량");volume_->setRange(0,100);volume_->setValue(QSettings().value("playback/volume",70).toInt());volume_->setFixedWidth(65);buttons->addWidget(volume_);
    auto *quick=button("자막");quick->setObjectName("captionQuick");quick->setFixedHeight(32);buttons->addWidget(quick);
    auto *remoteButton=button("메뉴");remoteButton->setObjectName("remoteQuick");remoteButton->setToolTip("Blu-ray 디스크 리모컨");remoteButton->setFixedHeight(32);buttons->addWidget(remoteButton);
    auto *full=new IconButton(PlayerIcon::Fullscreen,"전체 화면 · F11");fullButton_=full;buttons->addWidget(full);transportLayout->addLayout(buttons);centerLayout->addWidget(transport_);
    status_=label("미디어를 선택하면 준비가 시작됩니다.","status");status_->setWordWrap(true);centerLayout->addWidget(status_);body->addWidget(center,1);
    panel_=new QWidget;panel_->setObjectName("settingsPanel");panel_->setFixedWidth(306);auto *panelLayout=new QVBoxLayout(panel_);panelLayout->setContentsMargins(0,0,0,0);auto *tabs=new QTabWidget;tabs->setObjectName("settingsTabs");panelLayout->addWidget(tabs);body->addWidget(panel_);
    auto *discTab=new QWidget;auto *discLayout=new QVBoxLayout(discTab);discLayout->setContentsMargins(17,18,17,16);discLayout->setSpacing(10);discLayout->addWidget(label("DISC REMOTE","section"));
    menuStatus_=label("디스크를 열면 현재 타이틀이 표시됩니다.","menuStatus");menuStatus_->setWordWrap(true);discLayout->addWidget(menuStatus_);
    auto *menus=new QHBoxLayout;auto *topMenu=button("메인 메뉴");auto *popup=button("팝업 메뉴");menus->addWidget(topMenu);menus->addWidget(popup);discLayout->addLayout(menus);
    auto *remote=new QGridLayout;remote->setSpacing(6);
    const QList<QPair<PlayerIcon,QPoint>> keys={{PlayerIcon::Up,{1,0}},{PlayerIcon::Left,{0,1}},{PlayerIcon::Confirm,{1,1}},{PlayerIcon::Right,{2,1}},{PlayerIcon::Down,{1,2}}};
    const QList<DiscAction> actions={DiscAction::Up,DiscAction::Left,DiscAction::Activate,DiscAction::Right,DiscAction::Down};const QStringList names={"위","왼쪽","확인","오른쪽","아래"};
    for(int i=0;i<keys.size();++i){auto *control=new IconButton(keys[i].first,"디스크 "+names[i]);control->setFixedSize(54,34);remote->addWidget(control,keys[i].second.y(),keys[i].second.x());connect(control,&QPushButton::clicked,this,[this,action=actions[i]]{if(active_==discEngine_.get())active_->navigate(discActionCode(action));});}
    discLayout->addLayout(remote);
    auto choice=[&](QVBoxLayout *layout,const QString &name,QComboBox *&box){layout->addWidget(label(name,"section"));box=new ChoiceBox;layout->addWidget(box);};
    choice(discLayout,"타이틀",titles_);titles_->setObjectName("discTitles");choice(discLayout,"오디오",audio_);audio_->setObjectName("audioTracks");choice(discLayout,"디스크 자막",subs_);subs_->setObjectName("subtitleTracks");auto *external=button("자막 파일 추가","subtle");external->setToolTip("UTF-8 SRT 원문을 보존하며 독음·번역을 별도로 표시합니다.");discLayout->addWidget(external);discLayout->addWidget(label("챕터","section"));chapters_=new QListWidget;chapters_->setMinimumHeight(120);discLayout->addWidget(chapters_,1);tabs->addTab(scrollPanel(discTab),"디스크");
    auto *aiTab=new QWidget;auto *aiLayout=new QVBoxLayout(aiTab);aiLayout->setContentsMargins(17,18,17,18);aiLayout->setSpacing(10);aiLayout->addWidget(label("LOCAL CAPTIONS","section"));
    auto *aiIntro=label("원문부터 번역까지.\n내 기기에서 함께.","nowTitle");aiIntro->setWordWrap(true);aiLayout->addWidget(aiIntro);aiEnabled_=new QCheckBox("AI 자동자막 켜기");aiEnabled_->setObjectName("aiEnabled");aiEnabled_->setChecked(QSettings().value("subtitles/aiEnabled",false).toBool());aiLayout->addWidget(aiEnabled_);
    bufferStatus_=label("재생 전·탐색 후 자막을 10초 이상 준비합니다.","muted");bufferStatus_->setObjectName("captionBufferStatus");bufferStatus_->setWordWrap(true);aiLayout->addWidget(bufferStatus_);
    auto *bufferActions=new QHBoxLayout;auto *retry=button("자막 다시 시도","subtle");retry->setObjectName("retryCaptions");auto *continuePlay=button("자막 없이 계속","subtle");continuePlay->setObjectName("continueWithoutCaptions");bufferActions->addWidget(retry);bufferActions->addWidget(continuePlay);aiLayout->addLayout(bufferActions);
    choice(aiLayout,"음성 언어",language_);language_->addItem("日本語 · 일본어","ja");language_->addItem("English · 영어","en");language_->addItem("한국어","ko");language_->addItem("자동 감지","auto");
    choice(aiLayout,"음성 인식 가속",aiMode_);aiMode_->setObjectName("aiMode");aiMode_->addItem("자동 · NPU 우선","auto");aiMode_->addItem("NPU","npu");aiMode_->addItem("GPU","gpu");aiMode_->addItem("CPU","cpu");setSavedChoice(aiMode_,"ai/mode","auto");
    choice(aiLayout,"AI GPU",aiGpu_);aiGpu_->setObjectName("aiGpu");aiGpu_->addItem("자동 GPU 선택",QString{});const auto savedGpu=QSettings().value("ai/gpu").toString();if(!savedGpu.isEmpty()){aiGpu_->addItem("저장된 GPU · "+savedGpu,savedGpu);aiGpu_->setCurrentIndex(1);}
    auto *model=button("음성 인식 모델 선택","subtle");aiLayout->addWidget(model);accelerator_=label("음성 인식 · 실행 대기","muted");accelerator_->setObjectName("accelerator");accelerator_->setWordWrap(true);aiLayout->addWidget(accelerator_);
    aiLayout->addSpacing(6);aiLayout->addWidget(label("표시할 자막","section"));originalEnabled_=new QCheckBox("원문");originalEnabled_->setObjectName("originalEnabled");originalEnabled_->setChecked(QSettings().value("subtitles/original",true).toBool());aiLayout->addWidget(originalEnabled_);
    readingEnabled_=new QCheckBox("일본어 한글 독음");readingEnabled_->setObjectName("readingEnabled");readingEnabled_->setChecked(QSettings().value("subtitles/hangul",true).toBool());readingEnabled_->setEnabled(reading_.available());if(!reading_.available())readingEnabled_->setToolTip(reading_.error());aiLayout->addWidget(readingEnabled_);
    translationEnabled_=new QCheckBox("한국어 번역");translationEnabled_->setObjectName("translationEnabled");translationEnabled_->setChecked(QSettings().value("subtitles/translation",true).toBool());aiLayout->addWidget(translationEnabled_);
    translationBackend_=label("번역 · 로컬 모델 실행 대기","muted");translationBackend_->setWordWrap(true);aiLayout->addWidget(translationBackend_);translationStatus_=label("원문이 준비되면 가까운 구간부터 번역합니다.","muted");translationStatus_->setWordWrap(true);aiLayout->addWidget(translationStatus_);
    auto *translationModel=button("번역 모델 선택","subtle");aiLayout->addWidget(translationModel);
    aiStatus_=label("AI 자동자막 꺼짐","muted");aiStatus_->setWordWrap(true);aiLayout->addWidget(aiStatus_);auto *note=label("독음·번역은 원문과 별도로 보관됩니다. 노래나 배경음이 강하면 인식 정확도가 낮아질 수 있습니다.","muted");note->setWordWrap(true);aiLayout->addWidget(note);auto *save=button("표시 자막 저장","primary");aiLayout->addWidget(save);aiLayout->addStretch();tabs->addTab(scrollPanel(aiTab),"AI 자막");
    auto *settingsTab=new QWidget;auto *settingsLayout=new QVBoxLayout(settingsTab);settingsLayout->setContentsMargins(17,18,17,18);settingsLayout->setSpacing(12);settingsLayout->addWidget(label("PLAYBACK & PRIVACY","section"));
    choice(settingsLayout,"파일 영상 GPU",videoGpu_);videoGpu_->setObjectName("videoGpu");videoGpu_->addItem("자동 선택",QString{});for(const auto &entry:videoAdapters()){const auto adapter=entry.toObject();videoGpu_->addItem(adapter.value("name").toString(),adapter.value("id").toString());}setSavedChoice(videoGpu_,"video/adapter");
    auto *videoNote=label("파일 재생에 적용됩니다. 재생 중 변경하면 현재 위치에서 다시 연결합니다.","muted");videoNote->setWordWrap(true);settingsLayout->addWidget(videoNote);
    choice(settingsLayout,"프레임 보간",motionMode_);motionMode_->setObjectName("motionMode");motionMode_->addItem("꺼짐","off");motionMode_->addItem("부드러운 표시 · GPU 블렌딩","display");motionMode_->addItem("모션 보간 · 실험적 CPU 처리","optical");setSavedChoice(motionMode_,"motion/mode","off");
    choice(settingsLayout,"모션 보간 목표 FPS",motionFps_);motionFps_->setObjectName("motionFps");for(int fps:{60,90,120,144,165,240,360})motionFps_->addItem(QString::number(fps)+" fps",fps);setSavedChoice(motionFps_,"motion/fps","120");
    motionStatus_=label("파일 영상에서 사용할 수 있습니다.","muted");motionStatus_->setObjectName("motionStatus");motionStatus_->setWordWrap(true);settingsLayout->addWidget(motionStatus_);
    auto *motionNote=label("파일 전용 · 디스크 메뉴에는 적용하지 않습니다.\nCPU 모션 보간은 부하가 높습니다. 목표 FPS와 실제 처리·출력 FPS는 다를 수 있습니다.","muted");motionNote->setWordWrap(true);settingsLayout->addWidget(motionNote);
    displayStatus_=label("디스플레이 정보 확인 중…","displayStatus");displayStatus_->setWordWrap(true);settingsLayout->addWidget(displayStatus_);
    captureEnabled_=new QCheckBox("보호 디스크의 Windows 캡처 차단");captureEnabled_->setObjectName("captureEnabled");captureEnabled_->setChecked(QSettings().value("privacy/captureProtectedDisc",true).toBool());settingsLayout->addWidget(captureEnabled_);
    captureStatus_=label("보호 디스크를 재생하면 적용합니다.","captureStatus");captureStatus_->setWordWrap(true);settingsLayout->addWidget(captureStatus_);auto *privacyNote=label("Windows 캡처 제외 API의 실제 결과를 표시합니다. DRM·HDCP 인증과는 별개입니다.","muted");privacyNote->setWordWrap(true);settingsLayout->addWidget(privacyNote);
    auto *developer=new QCheckBox("개발자 모드");developer->setObjectName("developerMode");developer->setChecked(QSettings().value("ui/developerMode",false).toBool());settingsLayout->addWidget(developer);
    auto *developerPanel=new QWidget;developerPanel->setObjectName("developerDiagnostics");auto *developerLayout=new QVBoxLayout(developerPanel);developerLayout->setContentsMargins(0,0,0,0);developerLayout->addWidget(label("PLAYBACK LOG","section"));log_=new QPlainTextEdit;log_->setReadOnly(true);log_->setMaximumBlockCount(500);log_->setMinimumHeight(170);developerLayout->addWidget(log_);developerPanel->setVisible(developer->isChecked());codec_->setVisible(developer->isChecked());settingsLayout->addWidget(developerPanel,1);settingsLayout->addStretch();tabs->addTab(scrollPanel(settingsTab),"설정");
    connect(developer,&QCheckBox::toggled,this,[this,developerPanel](bool enabled){QSettings().setValue("ui/developerMode",enabled);developerPanel->setVisible(enabled);codec_->setVisible(enabled);});
    auto *captionMenu=new QMenu(quick);quick->setMenu(captionMenu);
    for(auto *check:{originalEnabled_,readingEnabled_,translationEnabled_,aiEnabled_}){auto *action=captionMenu->addAction(check->text());action->setCheckable(true);action->setChecked(check->isChecked());action->setEnabled(check->isEnabled());connect(action,&QAction::toggled,check,&QCheckBox::setChecked);connect(check,&QCheckBox::toggled,action,&QAction::setChecked);}
    auto *remoteMenu=new QMenu(remoteButton);remoteButton->setMenu(remoteMenu);auto *remoteHint=remoteMenu->addAction("영상 영역: 마우스 / 방향키 / Enter");remoteHint->setEnabled(false);remoteMenu->addSeparator();
    remoteMenu->addAction("메인 메뉴",topMenu,&QPushButton::click);remoteMenu->addAction("팝업 메뉴",popup,&QPushButton::click);remoteMenu->addSeparator();
    for(int i=0;i<actions.size();++i)remoteMenu->addAction(names[i],this,[this,action=actions[i]]{if(active_==discEngine_.get())active_->navigate(discActionCode(action));});
    connect(themeButton_,&QPushButton::clicked,this,[this]{setPlayerDarkTheme(!playerUsesDarkTheme());applyTheme();});
    connect(open,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"미디어 열기",{},"미디어 (*.mkv *.mp4 *.webm *.mov *.avi *.m2ts *.ts *.flac *.alac *.m4a *.mp3 *.wav);;모든 파일 (*)");if(!path.isEmpty())openSource(path);});connect(files,&QPushButton::clicked,open,&QPushButton::click);
    connect(rescan,&QPushButton::clicked,this,&MainWindow::discoverDisc);connect(discButton_,&QPushButton::clicked,this,[this]{if(discPath_.isEmpty())discoverDisc();if(!discPath_.isEmpty())openSource(discPath_);else status_->setText("연결된 Blu-ray를 찾지 못했습니다.");});
    connect(play_,&QPushButton::clicked,this,&MainWindow::togglePlayback);
    connect(back,&QPushButton::clicked,this,[this]{if(active_)seekTo(active_->state().positionMs-10000);});connect(forward,&QPushButton::clicked,this,[this]{if(active_)seekTo(active_->state().positionMs+10000);});
    connect(previous,&QPushButton::clicked,this,[this]{if(active_)selectChapter(std::max(0,active_->state().currentChapter-1));});connect(next,&QPushButton::clicked,this,[this]{if(active_)selectChapter(active_->state().currentChapter+1);});
    connect(seek_,&QSlider::sliderReleased,this,[this]{if(active_)seekTo(active_->state().durationMs*seek_->value()/10000);});
    connect(volume_,&QSlider::valueChanged,this,[this](int volume){QSettings().setValue("playback/volume",volume);if(active_)active_->setVolume(volume);});
    connect(volumeIcon,&QPushButton::clicked,this,[this]{volume_->setValue(volume_->value()?0:70);});
    connect(rate_,&QComboBox::activated,this,[this]{if(active_)active_->setRate(rate_->currentData().toDouble());});connect(full,&QPushButton::clicked,this,&MainWindow::toggleFullScreen);
    connect(topMenu,&QPushButton::clicked,this,[this]{if(active_==discEngine_.get()){selectTitle(0);surface_->setFocus();}});
    connect(popup,&QPushButton::clicked,this,[this]{if(active_==discEngine_.get()){active_->navigate(discActionCode(DiscAction::Popup));surface_->setFocus();}});
    connect(titles_,&QComboBox::activated,this,[this]{selectTitle(titles_->currentData().toInt());});
    connect(audio_,&QComboBox::activated,this,[this]{selectAudio(audio_->currentData().toInt());});
    connect(subs_,&QComboBox::activated,this,[this]{if(active_){aiEnabled_->setChecked(false);externalCues_.clear();rebuildManagedCaptions();active_->setSubtitle(subs_->currentData().toInt());}});
    connect(chapters_,&QListWidget::itemActivated,this,[this](QListWidgetItem *item){selectChapter(item->data(Qt::UserRole).toInt());});
    connect(external,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"자막 열기",{},"자막 (*.srt *.ass *.ssa *.vtt *.sup)");if(!path.isEmpty())loadCaptionFile(path);});
    connect(model,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"Whisper 모델",{},"Whisper 모델 (*.bin)");if(!path.isEmpty()){resetSubtitleSession();model_=path;QSettings().setValue("ai/model",path);aiStatus_->setText("음성 인식 모델 선택됨");if(aiEnabled_->isChecked())job_.warmUp(model_);scheduleSubtitles();}});
    connect(translationModel,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"로컬 번역 모델",{},"GGUF 모델 (*.gguf)");if(!path.isEmpty()){translation_->cancel();translationPending_.clear();clearTranslationSkips();translations_.clear();translationFailed_=false;QSettings().setValue("translation/model",path);translation_->setModel(path);rebuildManagedCaptions();scheduleTranslations();}});
    connect(aiMode_,&QComboBox::activated,this,&MainWindow::updateAiPreferences);connect(aiGpu_,&QComboBox::activated,this,&MainWindow::updateAiPreferences);connect(videoGpu_,&QComboBox::activated,this,&MainWindow::changeVideoAdapter);
    connect(motionMode_,&QComboBox::activated,this,&MainWindow::updateMotionSettings);connect(motionFps_,&QComboBox::activated,this,&MainWindow::updateMotionSettings);
    connect(captureEnabled_,&QCheckBox::toggled,this,[this](bool enabled){QSettings().setValue("privacy/captureProtectedDisc",enabled);applyCapturePolicy();});
    connect(aiEnabled_,&QCheckBox::toggled,this,[this](bool enabled){
        QSettings().setValue("subtitles/aiEnabled",enabled);
        if(enabled){externalCues_.clear();aiFailed_=false;bufferBypass_=false;aiRetryCount_=0;nextAiAttemptMs_=0;if(active_)active_->setSubtitle(-1);rebuildManagedCaptions();job_.warmUp(model_);scheduleSubtitles();}
        else{cancelSubtitleWork();job_.shutdown();releaseCaptionBuffer(true);if(active_)active_->clearManagedSubtitles();aiStatus_->setText("AI 자동자막 꺼짐");bufferStatus_->setText("AI 자동자막 꺼짐 · 일반 재생");rebuildManagedCaptions();}
    });
    connect(retry,&QPushButton::clicked,this,&MainWindow::retryCaptions);
    connect(continuePlay,&QPushButton::clicked,this,[this]{if(aiFailed_ && nextAiAttemptMs_<=0){aiFailed_=false;aiRetryCount_=0;}bufferBypass_=true;releaseCaptionBuffer(true);bufferStatus_->setText("자막을 기다리지 않고 재생합니다. AI는 계속 준비합니다.");scheduleSubtitles();});
    for(const auto &entry:QList<QPair<QCheckBox*,QString>>{{originalEnabled_,"subtitles/original"},{readingEnabled_,"subtitles/hangul"},{translationEnabled_,"subtitles/translation"}}){
        connect(entry.first,&QCheckBox::toggled,this,[this,check=entry.first,key=entry.second](bool enabled){QSettings().setValue(key,enabled);if(check==translationEnabled_){translation_->cancel();translationPending_.clear();clearTranslationSkips();translationFailed_=false;}rebuildManagedCaptions();scheduleTranslations();});
    }
    connect(save,&QPushButton::clicked,this,[this]{
        const auto cues=displayCues();if(cues.isEmpty()){aiStatus_->setText("표시할 자막을 켜고 원문을 생성하거나 불러오세요.");return;}
        const auto path=QFileDialog::getSaveFileName(this,"표시 자막 저장","captions.srt","SRT (*.srt)");if(path.isEmpty())return;
        QSaveFile file(path);if(!file.open(QIODevice::WriteOnly) || file.write(writeSrt(cues).toUtf8())<0 || !file.commit()){aiStatus_->setText("자막을 저장하지 못했습니다.");return;}aiStatus_->setText("현재 표시 형식의 UTF-8 SRT를 저장했습니다.");
    });
    auto shortcut=[this](const QKeySequence &key,QWidget *owner,Qt::ShortcutContext context,auto action){auto *binding=new QShortcut(key,owner);binding->setContext(context);connect(binding,&QShortcut::activated,this,[this,action]{if(fullscreenControls_)fullscreenControls_->activity();action();});};
    shortcut(QKeySequence("Ctrl+O"),this,Qt::WindowShortcut,[open]{open->click();});shortcut(QKeySequence("Ctrl+D"),this,Qt::WindowShortcut,[this]{discButton_->click();});
    shortcut(QKeySequence("F11"),this,Qt::WindowShortcut,[this]{toggleFullScreen();});shortcut(QKeySequence("Escape"),this,Qt::WindowShortcut,[this]{if(full_)toggleFullScreen();});
    shortcut(QKeySequence("Space"),surface_,Qt::WidgetWithChildrenShortcut,[this]{togglePlayback();});
    shortcut(QKeySequence("Shift+Left"),surface_,Qt::WidgetWithChildrenShortcut,[back]{back->click();});shortcut(QKeySequence("Shift+Right"),surface_,Qt::WidgetWithChildrenShortcut,[forward]{forward->click();});
    const QList<QPair<QString,DiscAction>> arrows={{"Up",DiscAction::Up},{"Down",DiscAction::Down},{"Left",DiscAction::Left},{"Right",DiscAction::Right},{"Return",DiscAction::Activate}};
    for(const auto &key:arrows){auto *binding=new QShortcut(QKeySequence(key.first),surface_);binding->setContext(Qt::WidgetWithChildrenShortcut);binding->setEnabled(false);discShortcuts_.append(binding);connect(binding,&QShortcut::activated,this,[this,action=key.second]{if(fullscreenControls_)fullscreenControls_->activity();if(active_==discEngine_.get())active_->navigate(discActionCode(action));});}
    connect(language_,&QComboBox::activated,this,[this]{resetSubtitleSession();scheduleSubtitles();});
}
void MainWindow::applyTheme(){
    const bool dark=playerUsesDarkTheme();themeButton_->setText(dark?"라이트 모드":"다크 모드");
#ifdef Q_OS_WIN
    const BOOL enabled=dark?TRUE:FALSE;DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()),20,&enabled,sizeof(enabled));
#endif
    update();
}
void MainWindow::applyCapturePolicy(){
    if(!captureGuard_)return;
    const bool protectedDisc=active_==discEngine_.get() && (discInfo_.aacs || discInfo_.bdplus || !discInfo_.valid);
    captureGuard_->setEnabled(protectedDisc && captureEnabled_->isChecked());
    captureStatus_->setText(protectedDisc?captureGuard_->status():"보호 디스크를 재생하면 Windows 캡처 제외를 적용합니다.");
    captureStatus_->setProperty("error",captureGuard_->enabled() && !captureGuard_->applied());
}
void MainWindow::updateDisplayStatus(){
    const auto display=displayCapabilities(winId());
    if(!display.value("verified").toBool()){displayStatus_->setText("현재 디스플레이의 HDR 상태를 확인하지 못했습니다.");return;}
    const QString mode=display.value("hdrActive").toBool()?"HDR 출력 활성":"SDR 출력";
    displayStatus_->setText(QString("%1 · %2비트\n%3").arg(mode).arg(display.value("bitsPerColor").toInt()).arg(display.value("display").toString()));
    displayStatus_->setToolTip("현재 Windows 출력 상태입니다. Dolby Vision 인증 출력은 확인되지 않았습니다.");
}
void MainWindow::updateAiPreferences(){
    const AiPreferences preferences{aiMode_->currentData().toString(),aiGpu_->currentData().toString()};
    QSettings().setValue("ai/mode",preferences.mode);QSettings().setValue("ai/gpu",preferences.gpuDevice);
    if(job_.preferences().mode!=preferences.mode || job_.preferences().gpuDevice!=preferences.gpuDevice){cancelSubtitleWork();job_.setPreferences(preferences);}
    if(translation_ && (translation_->preferences().mode!=preferences.mode || translation_->preferences().gpuDevice!=preferences.gpuDevice)){translation_->setPreferences(preferences);translationPending_.clear();clearTranslationSkips();}
    aiGpu_->setEnabled(preferences.mode=="auto" || preferences.mode=="gpu");
    aiFailed_=false;translationFailed_=false;nextAiAttemptMs_=0;aiRetryCount_=0;
    accelerator_->setText("음성 인식 · 선택한 기기에서 실행 대기");
    if(aiEnabled_->isChecked())job_.warmUp(model_);
    if(active_)scheduleSubtitles();
}
void MainWindow::changeVideoAdapter(){
    const auto selected=videoGpu_->currentData().toString();if(QSettings().value("video/adapter").toString()==selected)return;
    QSettings().setValue("video/adapter",selected);
    const bool reopen=active_==fileEngine_.get();const qint64 position=reopen?active_->state().positionMs:0;
    const bool playing=reopen?wantsPlayback():false;
    if(reopen){cancelSubtitleWork();releaseCaptionBuffer(false);active_->stop();active_=nullptr;}
    fileEngine_.reset();fileEngine_=std::make_unique<PlaybackEngine>(PlaybackEngine::Kind::File,QString{},this);bindEngine(fileEngine_.get());updateMotionSettings();
    if(reopen){
        active_=fileEngine_.get();if(active_->open(source_,surface_->winId())){active_->setVolume(volume_->value());active_->setRate(rate_->currentData().toDouble());active_->setPaused(true);active_->seek(position);pendingSeek_=position;seekRequestedAt_=QDateTime::currentMSecsSinceEpoch();if(aiEnabled_->isChecked()){bufferHolding_=true;bufferResume_=playing;}else active_->setPaused(!playing);}
        else status_->setText(active_->lastError());
    }
}
void MainWindow::updateMotionSettings(){
    const auto mode=motionMode_->currentData().toString();const int fps=motionFps_->currentData().toInt();
    QSettings().setValue("motion/mode",mode);QSettings().setValue("motion/fps",fps);motionFps_->setEnabled(mode=="optical");
    motionStatus_->setText(mode=="off"?"프레임 보간 꺼짐":active_==discEngine_.get()?"Blu-ray 메뉴 재생은 프레임 보간을 지원하지 않습니다.":mode=="optical"?QString("CPU 모션 보간 · 목표 %1 fps · 실제 처리 속도는 재생 후 확인").arg(fps):"GPU 블렌딩 · 현재 디스플레이 주기에 맞춤");
    if(fileEngine_)fileEngine_->setFrameInterpolation(mode,fps);
}
void MainWindow::discoverDisc(){
    discPath_.clear();
#ifdef Q_OS_WIN
    for(wchar_t drive=L'A';drive<=L'Z';++drive){const QString root=QString(QChar(drive))+":/";if(GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()))==DRIVE_CDROM && QFileInfo::exists(root+"BDMV/index.bdmv")){discPath_=root;break;}}
#else
    for(const auto &volume:QStorageInfo::mountedVolumes())if(QFileInfo::exists(volume.rootPath()+"/BDMV/index.bdmv")){discPath_=volume.rootPath();break;}
#endif
    if(discPath_.isEmpty()){discLabel_->setText("연결된 디스크 없음");discDetails_->clear();return;}
    driveInfo_=inspectDisc(discPath_);QString title=driveInfo_.label;title.replace('_',' ');discLabel_->setText(title.isEmpty()?"Blu-ray":title);
    discDetails_->setText(QString("%1\n%2개 플레이리스트\n%3").arg(discPath_).arg(driveInfo_.titles.size()).arg(driveInfo_.aacs || driveInfo_.bdplus?"보호 디스크 감지":"일반 디스크"));
}
void MainWindow::openSource(const QString &source){
    resetSubtitleSession();releaseCaptionBuffer(false);if(active_)active_->stop();lastTitle_=-1;lastNavigation_=false;lastAudio_=-2;decodedSamples_=0;pendingSeek_=-1;pendingAudio_=-2;pendingChapter_=-2;pendingTitle_=-2;requestedPlaying_=true;
    const bool disc=isDiscSource(source);source_=disc?localDiscPath(source):source;if(disc)discInfo_=inspectDisc(source_);
    active_=disc?discEngine_.get():fileEngine_.get();nowTitle_->setText(disc?(discInfo_.label.isEmpty()?"Blu-ray":discInfo_.label):QFileInfo(source).fileName());
    for(auto *binding:discShortcuts_)binding->setEnabled(disc);surface_->setFocus(Qt::OtherFocusReason);welcome_->hide();status_->setText("미디어를 여는 중…");
    if(active_->open(source_,surface_->winId())){active_->setVolume(volume_->value());active_->setRate(rate_->currentData().toDouble());if(!disc && aiEnabled_->isChecked()){bufferHolding_=true;bufferResume_=true;active_->setPaused(true);}}
    else{status_->setText(active_->lastError());welcome_->show();}
    applyCapturePolicy();updateDisplayStatus();updateMotionSettings();
}
void MainWindow::enableAutomaticSubtitles(const QString &language){const int index=language_->findData(language);if(index>=0){language_->setCurrentIndex(index);aiEnabled_->setChecked(true);scheduleSubtitles();}}
void MainWindow::refresh(){
    if(!active_)return;const auto state=active_->state();
    if(requestedPlaying_ && !bufferHolding_ && state.playing==*requestedPlaying_)requestedPlaying_.reset();
    if(pendingAudio_!=-2 && state.currentAudio==pendingAudio_)pendingAudio_=-2;
    if(pendingChapter_!=-2 && state.currentChapter==pendingChapter_)pendingChapter_=-2;
    if(pendingTitle_!=-2 && state.currentTitle==pendingTitle_)pendingTitle_=-2;
    if((pendingAudio_!=-2 || pendingChapter_!=-2 || pendingTitle_!=-2) && QDateTime::currentMSecsSinceEpoch()-pendingControlAt_>8000){pendingAudio_=-2;pendingChapter_=-2;pendingTitle_=-2;aiStatus_->setText("선택한 트랙·챕터로 이동하지 못했습니다. 현재 위치에서 자막을 준비합니다.");}
    if(state.currentAudio>=0){if(lastAudio_!=-2 && lastAudio_!=state.currentAudio)resetSubtitleSession();lastAudio_=state.currentAudio;}
    if(active_==discEngine_.get() && (state.currentTitle!=lastTitle_ || state.navigationTitle!=lastNavigation_)){resetSubtitleSession();lastTitle_=state.currentTitle;lastNavigation_=state.navigationTitle;if(state.navigationTitle)releaseCaptionBuffer(true);}
    if(pendingSeek_>=0 && (std::abs(state.positionMs-pendingSeek_)<=750 || QDateTime::currentMSecsSinceEpoch()-seekRequestedAt_>5000))pendingSeek_=-1;
    captions_->setPosition(state.positionMs);if(state.playing)++decodedSamples_;
    if(captureGuard_ && captureGuard_->enabled())captureStatus_->setText(captureGuard_->status());
    const bool playing=wantsPlayback();static_cast<IconButton*>(play_)->setPlayerIcon(playing?PlayerIcon::Pause:PlayerIcon::Play);
    play_->setAccessibleName(playing?"일시정지 · Space":"재생 · Space");clock_->setText(formatTime(state.positionMs)+" / "+formatTime(state.durationMs));
    if(!seek_->isSliderDown()){QSignalBlocker guard(seek_);seek_->setValue(state.durationMs>0?int(state.positionMs*10000/state.durationMs):0);}
    codec_->setText(QStringList{active_->name(),state.videoCodec,state.hardwareDecoder}.filter(QRegularExpression(".+")).join(" · "));
    if(active_==fileEngine_.get() && !state.motionStatus.isEmpty()){
        QString text=state.motionStatus;
        if(state.sourceFps>0)text+=QString("\n원본 %1 fps · 필터 %2 fps · 화면 %3 Hz").arg(state.sourceFps,0,'f',2).arg(state.filteredFps,0,'f',2).arg(state.displayFps,0,'f',2);
        motionStatus_->setText(text);
    }
    updateCaptionBuffer();
    if(!state.failed){if(bufferHolding_)status_->setText(bufferStatus_->text());else if(!aiFailed_)status_->setText(state.playing?"재생 중":"일시정지 / 준비 중");}
    syncCombo(titles_,state.titles);syncCombo(audio_,state.audioTracks);syncCombo(subs_,state.subtitleTracks);
    const auto select=[](QComboBox *box,int id){const int index=box->findData(id);if(index>=0 && index!=box->currentIndex()){QSignalBlocker guard(box);box->setCurrentIndex(index);}};
    select(titles_,state.currentTitle);select(audio_,state.currentAudio);select(subs_,state.currentSubtitle);
    if(chapters_->count()!=state.chapters.size()){chapters_->clear();for(const auto &chapter:state.chapters){auto *item=new QListWidgetItem(chapter.label.isEmpty()?QString("Chapter %1").arg(chapter.id+1):chapter.label);item->setData(Qt::UserRole,chapter.id);chapters_->addItem(item);}}
    if(active_==discEngine_.get())menuStatus_->setText(state.currentTitle<=0?"디스크 메뉴 · 영상 영역을 클릭하거나 방향키·Enter로 조작하세요.":state.navigationTitle?"디스크 시작 안내 · 메뉴를 준비하는 중…":QString("타이틀 %1 · 챕터 %2\n팝업 메뉴로 디스크 메뉴를 열 수 있습니다.").arg(state.currentTitle).arg(std::max(0,state.currentChapter)+1));
}
void MainWindow::appendLog(const QString &message){log_->appendPlainText(QTime::currentTime().toString("HH:mm:ss")+"  "+message);}
void MainWindow::toggleFullScreen(){
    full_=!full_;sidebar_->setVisible(!full_);panel_->setVisible(!full_);header_->setVisible(!full_);status_->setVisible(!full_);transportTitleRow_->setVisible(!full_);transport_->setFixedHeight(full_?74:108);
    static_cast<IconButton*>(fullButton_)->setPlayerIcon(full_?PlayerIcon::ExitFullscreen:PlayerIcon::Fullscreen);fullButton_->setToolTip(full_?"전체 화면 나가기 · Esc":"전체 화면 · F11");
    if(full_)showFullScreen();else showNormal();fullscreenControls_->setActive(full_);updateDisplayStatus();
}
bool MainWindow::wantsPlayback()const{return requestedPlaying_.value_or(bufferHolding_?bufferResume_:active_ && active_->state().playing);}
bool MainWindow::captionTimelineAvailable()const{
    if(!active_)return false;const auto &state=active_->state();
    return state.durationMs>0 && state.currentAudio>=0 && (active_!=discEngine_.get() || (!state.navigationTitle && state.currentTitle>0 && !state.audioTracks.isEmpty()));
}
void MainWindow::togglePlayback(){
    if(!active_)return;
    requestedPlaying_=!wantsPlayback();
    if(bufferHolding_){bufferResume_=*requestedPlaying_;active_->setPaused(true);updateCaptionBuffer();}
    else active_->setPaused(!*requestedPlaying_);
}
void MainWindow::selectAudio(int id){if(!active_)return;resetSubtitleSession();pendingAudio_=id;pendingControlAt_=QDateTime::currentMSecsSinceEpoch();active_->setAudio(id);if(id<0)releaseCaptionBuffer(true);else holdForCaptionBuffer();}
void MainWindow::selectChapter(int id){if(!active_)return;const auto resume=wantsPlayback();cancelSubtitleWork();pendingChapter_=id;pendingControlAt_=QDateTime::currentMSecsSinceEpoch();bufferBypass_=false;if(aiEnabled_->isChecked() && captionTimelineAvailable()){bufferHolding_=true;bufferResume_=resume;active_->setPaused(true);}active_->setChapter(id);}
void MainWindow::selectTitle(int id){if(!active_)return;resetSubtitleSession();pendingTitle_=id;pendingControlAt_=QDateTime::currentMSecsSinceEpoch();if(id<=0)releaseCaptionBuffer(true);active_->setTitle(id);}
void MainWindow::seekTo(qint64 position){
    if(!active_)return;const auto state=active_->state();const bool resume=wantsPlayback();
    cancelSubtitleWork();pendingChapter_=-2;position=std::clamp<qint64>(position,0,std::max<qint64>(0,state.durationMs));pendingSeek_=position;seekRequestedAt_=QDateTime::currentMSecsSinceEpoch();bufferBypass_=false;
    if(aiEnabled_->isChecked() && captionTimelineAvailable()){bufferHolding_=true;bufferResume_=resume;active_->setPaused(true);}active_->seek(position);scheduleSubtitles();
}
void MainWindow::holdForCaptionBuffer(){
    if(!active_ || !aiEnabled_->isChecked() || bufferBypass_ || !captionTimelineAvailable())return;
    const auto &state=active_->state();
    const auto position=pendingSeek_>=0?pendingSeek_:state.positionMs;if(position>=state.durationMs || coverage_.readyForPlayback(position,state.durationMs))return;
    if(!bufferHolding_){const bool resume=wantsPlayback();bufferHolding_=true;bufferResume_=resume;}
    active_->setPaused(true);
}
void MainWindow::updateCaptionBuffer(){
    if(!active_ || !aiEnabled_->isChecked())return;
    const auto &state=active_->state();
    if(active_==discEngine_.get() && state.navigationTitle){releaseCaptionBuffer(true);bufferStatus_->setText("디스크 메뉴 · 본편에서 자막을 준비합니다.");return;}
    if(state.durationMs<=0)return;
    const auto position=pendingSeek_>=0?pendingSeek_:state.positionMs;const auto ahead=coverage_.bufferedAhead(position,state.durationMs);const auto needed=std::min<qint64>(CaptionCoverage::lookAheadMs,std::max<qint64>(0,state.durationMs-position));
    if(position>=state.durationMs){releaseCaptionBuffer(true);return;}
    if(pendingAudio_!=-2 || pendingChapter_!=-2 || pendingTitle_!=-2){bufferStatus_->setText("선택한 트랙·챕터로 이동하는 중…");return;}
    if(full_ && bufferHolding_)clock_->setText(QString("자막 %1 / %2초").arg(ahead/1000.,0,'f',1).arg(needed/1000.,0,'f',1));
    if(!aiFailed_)bufferStatus_->setText(QString("자막 %1 / %2초 준비%3").arg(ahead/1000.,0,'f',1).arg(needed/1000.,0,'f',1).arg(bufferHolding_?(bufferResume_?" · 준비 후 자동 재생":" · 일시정지 유지"):""));
    if(coverage_.readyForPlayback(position,state.durationMs))releaseCaptionBuffer(true);
    else holdForCaptionBuffer();
}
void MainWindow::releaseCaptionBuffer(bool continuePlayback){
    const bool resume=bufferHolding_ && bufferResume_ && continuePlayback;bufferHolding_=false;bufferResume_=false;if(resume && active_){requestedPlaying_=true;active_->setPaused(false);}
}
void MainWindow::retryCaptions(){
    cancelSubtitleWork();aiRetryCount_=0;nextAiAttemptMs_=0;aiFailed_=false;bufferBypass_=false;translationFailed_=false;
    aiStatus_->setText(aiEnabled_->isChecked()?"자막 준비를 다시 시도합니다.":"AI 자동자막을 켜면 시작합니다.");scheduleSubtitles();scheduleTranslations();
}
void MainWindow::cancelSubtitleWork(){
    job_.cancel();coverage_.cancel();if(translation_)translation_->cancel();translationPending_.clear();clearTranslationSkips();aiFailed_=false;translationFailed_=false;aiRetryCount_=0;nextAiAttemptMs_=0;
}
void MainWindow::resetSubtitleSession(){
    cancelSubtitleWork();if(active_)active_->clearManagedSubtitles();generatedCues_.clear();externalCues_.clear();readings_.clear();translations_.clear();coverage_.reset();pendingSeek_=-1;bufferBypass_=false;if(captions_)captions_->setCues({});
}
QVector<SubtitleCue> MainWindow::originalCues()const{return !externalCues_.isEmpty()?externalCues_:aiEnabled_->isChecked()?generatedCues_:QVector<SubtitleCue>{};}
QVector<SubtitleCue> MainWindow::displayCues()const{
    QVector<SubtitleCue> result;for(const auto &source:originalCues()){
        QStringList lines;if(originalEnabled_->isChecked())lines.append(source.text);
        if(readingEnabled_->isChecked() && !readings_.value(source.text).isEmpty())lines.append(readings_.value(source.text));
        if(translationEnabled_->isChecked() && !translations_.value(source.text).isEmpty())lines.append(translations_.value(source.text));
        if(!lines.isEmpty())result.append({source.startMs,source.endMs,lines.join('\n')});
    }return result;
}
void MainWindow::rebuildManagedCaptions(){
    if(!captions_)return;
    if(readingEnabled_->isChecked())for(const auto &cue:originalCues())if(!readings_.contains(cue.text))readings_.insert(cue.text,reading_.reading(cue.text));
    captions_->setCues(displayCues());if(active_)captions_->setPosition(active_->state().positionMs);scheduleTranslations();
}
bool MainWindow::loadCaptionFile(const QString &path){
    if(!active_)return false;
    if(QFileInfo(path).suffix().compare("srt",Qt::CaseInsensitive)==0){
        QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()>8*1024*1024){status_->setText("자막 파일을 읽을 수 없습니다.");return false;}
        auto cues=parseSrt(QString::fromUtf8(file.readAll()));if(cues.isEmpty()){status_->setText("UTF-8 SRT 자막을 확인하세요.");return false;}
        aiEnabled_->setChecked(false);if(translation_)translation_->cancel();translationPending_.clear();clearTranslationSkips();readings_.clear();translations_.clear();translationFailed_=false;externalCues_=std::move(cues);active_->setSubtitle(-1);rebuildManagedCaptions();status_->setText("자막을 불러왔습니다.");return true;
    }
    aiEnabled_->setChecked(false);externalCues_.clear();rebuildManagedCaptions();return active_->loadSubtitle(path);
}
void MainWindow::scheduleSubtitles(){
    if(!aiEnabled_->isChecked() || !active_)return;
    updateCaptionBuffer();
    if(job_.busy() || pendingAudio_!=-2 || pendingChapter_!=-2 || pendingTitle_!=-2)return;
    if(!captionTimelineAvailable())return;const auto &state=active_->state();
    holdForCaptionBuffer();updateCaptionBuffer();
    if(aiFailed_){if(nextAiAttemptMs_<=0 || QDateTime::currentMSecsSinceEpoch()<nextAiAttemptMs_)return;aiFailed_=false;nextAiAttemptMs_=0;}
    const auto position=pendingSeek_>=0?pendingSeek_:state.positionMs;auto chunk=coverage_.begin(position,state.durationMs);
    const auto ahead=coverage_.bufferedAhead(position,state.durationMs);
    if(!chunk && ahead<30000 && position+ahead<state.durationMs)chunk=coverage_.begin(position+ahead,state.durationMs);
    if(!chunk)return;
    RecognitionRequest request;request.source=source_;request.disc=active_==discEngine_.get();request.title=std::max(1,state.currentTitle);request.language=language_->currentData().toString();request.startMs=*chunk;request.durationMs=std::min<qint64>(CaptionCoverage::chunkMs,state.durationMs-*chunk);request.audioTrackId=state.currentAudio;
    if(request.disc)request.playlist=matchDiscPlaylist(discInfo_,state.durationMs);
    int index=0;for(const auto &track:state.audioTracks){if(track.id<0)continue;if(track.id==state.currentAudio){request.audioIndex=index;break;}++index;}
    job_.start(request,model_);
}
void MainWindow::scheduleTranslations(){
    if(!translation_ || !translationEnabled_->isChecked() || translationFailed_)return;
    const qint64 position=active_?(pendingSeek_>=0?pendingSeek_:active_->state().positionMs):0;
    auto cues=originalCues();std::stable_sort(cues.begin(),cues.end(),[position](const SubtitleCue &a,const SubtitleCue &b){return std::abs(a.startMs-position)<std::abs(b.startMs-position);});
    QStringList work;for(const auto &cue:cues){if(work.size()>=16)break;if(cue.endMs<position-5000 || cue.startMs>position+90000 || cue.text.isEmpty() || translations_.contains(cue.text) || translationPending_.contains(cue.text) || translationSkipped_.contains(cue.text))continue;work.append(cue.text);translationPending_.insert(cue.text);}
    if(!work.isEmpty())translation_->request(work);
}
void MainWindow::clearTranslationSkips(){translationSkipped_.clear();translationSkippedOrder_.clear();}
QJsonObject MainWindow::diagnostics()const{
    QJsonObject result{{"version",LUMA_VERSION},{"playingSamples",decodedSamples_},{"discRuntime",discEngine_->available()},{"fileRuntime",fileEngine_->available()},{"aiCueCount",generatedCues_.size()},{"aiBackend",job_.selectedBackend()},{"aiEnabled",aiEnabled_->isChecked()},{"aiRetryCount",aiRetryCount_},{"aiRetryAt",nextAiAttemptMs_},{"captionBufferHolding",bufferHolding_},{"captionResumeRequested",bufferResume_},{"captionBufferBypass",bufferBypass_},{"hangulReadings",readingEnabled_->isChecked()},{"readingRuntime",reading_.available()},{"originalCaptions",originalEnabled_->isChecked()},{"koreanTranslations",translationEnabled_->isChecked()},{"translationCount",translations_.size()},{"translationBackend",translation_?translation_->selectedBackend():QString{}},{"bundledFonts",qApp->property("lumaFontsLoaded").toBool()},{"externalCueCount",externalCues_.size()},{"theme",playerUsesDarkTheme()?"dark":"light"},{"fullscreen",full_},{"transportVisible",transport_->isVisible()},{"transportHeight",transport_->height()},{"developerMode",QSettings().value("ui/developerMode",false).toBool()}};
    result.insert("translationSkippedCount",translationSkipped_.size());result.insert("translationPendingCount",translationPending_.size());result.insert("translationFailed",translationFailed_);
    const auto request=job_.currentRequest();
    if(active_)result.insert("navigationTitle",active_->state().navigationTitle);
    result.insert("pendingAudioTrack",pendingAudio_);result.insert("pendingChapter",pendingChapter_);result.insert("pendingTitle",pendingTitle_);
    result.insert("jobBusy",job_.busy());result.insert("requestedAudioTrack",request.audioTrackId);result.insert("requestedAudioIndex",request.audioIndex);result.insert("requestedPlaying",wantsPlayback());
    result.insert("discRoot",discInfo_.root);result.insert("discValid",discInfo_.valid);result.insert("discProtected",discInfo_.aacs || discInfo_.bdplus);
    if(active_)result.insert("currentAudioTrack",active_->state().currentAudio);
    if(captureGuard_){result.insert("captureRequested",captureGuard_->enabled());result.insert("captureApplied",captureGuard_->applied());result.insert("captureStatus",captureGuard_->status());}result.insert("display",displayCapabilities(winId()));
    if(active_){const auto &state=active_->state();result.insert("engine",active_->name());result.insert("positionMs",state.positionMs);result.insert("durationMs",state.durationMs);result.insert("playing",state.playing);result.insert("failed",state.failed);result.insert("title",state.currentTitle);result.insert("chapter",state.currentChapter);result.insert("chapterCount",state.chapters.size());result.insert("audioTrackCount",state.audioTracks.size());result.insert("videoCodec",state.videoCodec);result.insert("hardwareDecoder",state.hardwareDecoder);result.insert("audioCodec",state.audioCodec);result.insert("decodedVideo",state.decodedVideo);result.insert("decodedAudio",state.decodedAudio);result.insert("displayedPictures",state.displayedPictures);result.insert("playedAudioBuffers",state.playedAudioBuffers);result.insert("captionBufferedAheadMs",coverage_.bufferedAhead(state.positionMs,state.durationMs));result.insert("sourceFps",state.sourceFps);result.insert("filteredFps",state.filteredFps);result.insert("displayFps",state.displayFps);result.insert("motionStatus",state.motionStatus);}
    QJsonArray messages;const auto lines=log_->toPlainText().split('\n');for(const auto &line:lines.mid(std::max<qsizetype>(0,lines.size()-30)))messages.append(line);result.insert("recentMessages",messages);return result;
}
void MainWindow::dragEnterEvent(QDragEnterEvent *event){if(event->mimeData()->hasUrls())event->acceptProposedAction();}
void MainWindow::dropEvent(QDropEvent *event){const auto urls=event->mimeData()->urls();if(!urls.isEmpty()){openSource(urls.first().isLocalFile()?urls.first().toLocalFile():urls.first().toString());event->acceptProposedAction();}}
}
