#include "main_window.h"
#include "luma_version.h"
#include "caption_overlay.h"
#include "caption_preparation_dialog.h"
#include "prepared_caption_binding.h"
#include "video_surface.h"
#include "fullscreen_controls.h"
#include "player_controls.h"
#include "theme.h"
#include "media/platform_features.h"
#include "subtitles/ai_devices.h"
#include "subtitles/translation_service.h"
#include "subtitles/translation_support.h"
#include "subtitles/translation_devices.h"
#include "subtitles/subtitle_file.h"
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTextEdit>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QTabBar>
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
#include <QDialog>
#include <QFile>
#include <QSaveFile>
#include <QTimer>
#include <QStorageInfo>
#include <QStandardPaths>
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
constexpr int privacyConsentVersion=1;
bool confirmDeveloperUse(QWidget *parent,bool capture){
    QDialog dialog(parent);dialog.setObjectName(capture?"captureConsentDialog":"developerConsentDialog");
    dialog.setWindowTitle(capture?"영상 캡처 허용 확인":"개발자 모드 이용 확인");dialog.setModal(true);dialog.setMinimumWidth(520);
    auto *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(26,24,26,24);layout->setSpacing(18);
    layout->addWidget(label(capture?"영상과 자막이 캡처에 포함됩니다":"개발자 기능을 사용하기 전에","section"));
    auto *notice=label(capture?
        "캡처 차단을 해제하면 Windows 화면 캡처와 Parsec 등 원격 화면에 영상과 자막이 표시될 수 있습니다.\n\n"
        "본인에게 이용 권한이 있는 콘텐츠를 적법한 목적으로만 사용하고, 캡처·저장·공유에 필요한 권한과 관련 법규를 직접 확인해야 합니다. 본인의 이용 행위에 따른 책임은 사용자 본인에게 있습니다.\n\n"
        "이 설정은 콘텐츠 이용 권한을 부여하거나 DRM·HDCP를 해제하지 않습니다. 개발자는 법률이 허용하는 범위에서 GPLv3의 무보증·책임 제한 조항에 따릅니다.":
        "개발자 모드에는 재생 진단과 테스트용 설정이 포함됩니다. 테스트 결과와 콘텐츠의 이용 권한을 직접 확인하고 관련 법규와 라이선스를 준수해야 합니다.\n\n"
        "사용자가 수행하는 캡처·저장·공유 등 본인의 이용 행위에 따른 책임은 사용자 본인에게 있습니다. 개발자는 법률이 허용하는 범위에서 GPLv3의 무보증·책임 제한 조항에 따릅니다.\n\n"
        "이 동의가 법률상 책임을 일괄 면제하거나 저작권·콘텐츠 이용 권한을 부여하는 것은 아닙니다.");
    notice->setWordWrap(true);notice->setTextFormat(Qt::PlainText);notice->setMaximumWidth(560);layout->addWidget(notice);
    auto *ack=new QCheckBox("안내를 읽었으며, 적법한 이용 의무와 본인의 책임을 확인하고 동의합니다.");ack->setObjectName("consentAcknowledged");layout->addWidget(ack);
    auto *row=new QHBoxLayout;row->addStretch();auto *cancel=button("취소","consentCancel");auto *approve=button(capture?"동의하고 캡처 허용":"동의하고 개발자 모드 켜기","primary");approve->setObjectName("consentApprove");approve->setEnabled(false);row->addWidget(cancel);row->addWidget(approve);layout->addLayout(row);
    QObject::connect(ack,&QCheckBox::toggled,approve,&QPushButton::setEnabled);QObject::connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);QObject::connect(approve,&QPushButton::clicked,&dialog,&QDialog::accept);
    return dialog.exec()==QDialog::Accepted;
}
}

MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),job_({},this),subtitleCache_(QDir::tempPath()+"/luma-captions-XXXXXX"){
    setWindowTitle("LumaPlayer");resize(1440,900);setMinimumSize(1080,680);setAcceptDrops(true);setWindowIcon(playerApplicationIcon());
    buildUi();captions_=new CaptionOverlay(surface_);fullscreenControls_=new FullscreenControls(this,transport_,surface_);
    connect(static_cast<VideoSurface*>(surface_),&VideoSurface::urlsDropped,this,&MainWindow::openDroppedUrls);
    connect(fullscreenControls_,&FullscreenControls::reservedHeightChanged,captions_,&CaptionOverlay::setBottomInset);captions_->setBottomInset(fullscreenControls_->reservedHeight());
    captureGuard_=new CaptureGuard(this);captureGuard_->watch(static_cast<VideoSurface*>(surface_)->videoWindow());captureGuard_->watch(captions_);
    const auto qualityModel=QCoreApplication::applicationDirPath()+"/runtime/models/ggml-large-v3-turbo-q5_0.bin";
    model_=QSettings().value("ai/model",QFileInfo::exists(qualityModel)?qualityModel:QCoreApplication::applicationDirPath()+"/runtime/models/ggml-base.bin").toString();
    lastUnmutedVolume_=QSettings().value("playback/lastUnmutedVolume",70).toInt();qApp->installEventFilter(this);
    translation_=new TranslationService({},this);
    const auto qualityTranslation=QCoreApplication::applicationDirPath()+"/runtime/translation/models/Hy-MT2-7B-Q4_K_M.gguf";
    translation_->setModel(QSettings().value("translation/model",QFileInfo::exists(qualityTranslation)?qualityTranslation:QString{}).toString());
    connect(translation_,&TranslationService::translatedCue,this,[this](quint64 generation,const QString &key,const QString &,const QString &korean){
        if(generation!=translation_->generation() || !translationEnabled_->isChecked())return;
        translationPending_.remove(key);translations_.insert(key,korean);rebuildManagedCaptions();translationStatus_->setText(QString("문맥을 참고한 한국어 번역 %1개 준비됨").arg(translations_.size()));
    });
    connect(translation_,&TranslationService::skippedCue,this,[this](quint64 generation,const QString &key,const QString &,const QString &message){
        if(generation!=translation_->generation() || !translationEnabled_->isChecked())return;
        translationPending_.remove(key);
        if(!translationSkipped_.contains(key)){
            translationSkipped_.insert(key);translationSkippedOrder_.append(key);
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
    subtitleFileLoader_=new SubtitleFileLoader({},this);
    connect(subtitleFileLoader_,&SubtitleFileLoader::ready,this,[this](quint64 generation,QVector<SubtitleCue> cues){if(generation==subtitleFileLoader_->generation() && active_)applyExternalCaptions(std::move(cues));});
    connect(subtitleFileLoader_,&SubtitleFileLoader::failed,this,[this](quint64 generation,const QString &message){if(generation==subtitleFileLoader_->generation() && active_)subtitleImportFailed(message);});
    updateResumeButton();
    connect(&job_,&SubtitleJob::progress,this,[this](const QString &message){aiStatus_->setText(message);});
    connect(&job_,&SubtitleJob::backendSelected,this,[this](const QString &backend){
        auto text=backend=="NPU"?QString("음성 인코더 · NPU (실행 확인)"):backend=="GPU"?QString("음성 인식 · GPU (실행 확인)"):QString("음성 인식 · ")+backend+" (실행 확인)";
        if(job_.selectedAlignment()=="dtw")text+=" · 음성 시점 정렬";
        accelerator_->setText(text);
    });
    connect(&job_,&SubtitleJob::failed,this,[this](const QString &message){
        coverage_.cancel();aiFailed_=true;appendLog(message);
        if(aiEnabled_->isChecked() && aiRetryCount_<3){
            const int delay=2000*(1<<aiRetryCount_++);nextAiAttemptMs_=QDateTime::currentMSecsSinceEpoch()+delay;
            aiStatus_->setText(message+QString("\n%1초 후 다시 시도합니다. (%2/3)").arg(delay/1000).arg(aiRetryCount_));
            bufferStatus_->setText("자막 준비 재시도 중 · ‘자막 없이 계속’으로 바로 재생할 수 있습니다.");
        }else{
            nextAiAttemptMs_=0;holdForCaptionBuffer();
            aiStatus_->setText(message+"\n자동 재시도를 마쳤습니다. 모델·오디오를 확인하고 다시 시도하세요.");
            bufferStatus_->setText("최소 자막 버퍼를 기다립니다. 다시 시도하거나 ‘자막 없이 계속’을 선택하세요.");
        }
        status_->setText(bufferStatus_->text());
    });
    connect(&job_,&SubtitleJob::ready,this,[this](quint64 generation,QVector<SubtitleCue> cues){
        if(generation!=job_.generation() || !active_ || !aiEnabled_->isChecked())return;
        coverage_.complete();aiFailed_=false;aiRetryCount_=0;nextAiAttemptMs_=0;generatedCues_+=cues;presentationDirty_=true;rebuildManagedCaptions();updateCaptionBuffer();
        aiStatus_->setText(QString("자막 %1개 생성 · 기기에서 처리됨").arg(generatedCues_.size()));
        QTimer::singleShot(0,this,&MainWindow::scheduleSubtitles);
    });
    updateAiPreferences();updateTranslationPreferences();updateMotionSettings();
    deviceProbe_=new AiDeviceProbe({},this);
    connect(deviceProbe_,&AiDeviceProbe::devicesReady,this,[this](const QJsonArray &devices){
        const auto selected=QSettings().value("ai/gpu").toString();QSignalBlocker guard(aiGpu_);aiGpu_->clear();aiGpu_->addItem("자동 GPU 선택",QString{});
        for(const auto &entry:devices){const auto device=entry.toObject();if(device.value("type").toString().compare("GPU",Qt::CaseInsensitive)!=0)continue;aiGpu_->addItem(device.value("name").toString()+" · "+device.value("backend").toString(),device.value("id").toString());}
        int index=aiGpu_->findData(selected);if(index<0 && !selected.isEmpty()){aiGpu_->addItem("저장된 GPU · "+selected,selected);index=aiGpu_->count()-1;}if(index>=0)aiGpu_->setCurrentIndex(index);
    });
    connect(deviceProbe_,&AiDeviceProbe::failed,this,[this](const QString &message){appendLog("AI 기기 검색: "+message);aiGpu_->setToolTip(message);});
    auto *timer=new QTimer(this);timer->setInterval(500);connect(timer,&QTimer::timeout,this,[this]{scheduleSubtitles();scheduleTranslations();});timer->start();
    translationDeviceProbe_=new TranslationDeviceProbe({},this);
    connect(translationDeviceProbe_,&TranslationDeviceProbe::devicesReady,this,[this](const QJsonArray &devices){
        const auto saved=QSettings().value("translation/gpu").toString();QSignalBlocker blocked(translationGpu_);translationGpu_->clear();translationGpu_->addItem("자동 GPU 선택",QString{});
        for(const auto &entry:devices){const auto device=entry.toObject();if(device.value("type").toString()!="gpu")continue;translationGpu_->addItem(device.value("backend").toString()+" · "+device.value("name").toString(),device.value("id").toString());}
        int index=translationGpu_->findData(saved);if(index<0 && !saved.isEmpty()){translationGpu_->addItem("저장된 GPU · "+saved,saved);index=translationGpu_->count()-1;}if(index>=0)translationGpu_->setCurrentIndex(index);
    });
    connect(translationDeviceProbe_,&TranslationDeviceProbe::failed,this,[this](const QString &message){appendLog("번역 장치 확인: "+message);});
    QTimer::singleShot(0,this,[this]{discoverDisc();deviceProbe_->start();translationDeviceProbe_->start();updateDisplayStatus();applyTheme();});
}
MainWindow::~MainWindow(){savePlaybackProgress(true);qApp->removeEventFilter(this);subtitleFileLoader_->cancel();captions_->hide();static_cast<VideoSurface*>(surface_)->setVideoActive(false);job_.shutdown();translation_->cancel();active_=nullptr;fileEngine_.reset();discEngine_.reset();}
void MainWindow::bindEngine(PlaybackEngine *engine){
    connect(engine,&PlaybackEngine::updated,this,[this,engine]{if(engine==active_)refresh();});connect(engine,&PlaybackEngine::logMessage,this,&MainWindow::appendLog);
    connect(engine,&PlaybackEngine::errorOccurred,this,[this,engine](const QString &message){appendLog(message);if(engine==active_){requestedPlaying_.reset();releaseCaptionBuffer(false);status_->setText(message);status_->setProperty("error",true);}});
    connect(engine,&PlaybackEngine::videoOutputLost,this,[this,engine](const QString &message){
        const QPointer<PlaybackEngine> target(engine);const auto generation=sourceGeneration_;
        QTimer::singleShot(0,this,[this,target,generation,message]{if(target && target==active_ && generation==sourceGeneration_)recoverVideoOutput(target,message);});
    });
    if(!engine->available())appendLog(engine->lastError());
}
void MainWindow::recoverVideoOutput(PlaybackEngine *engine,const QString &message){
    if(engine!=active_ || source_.isEmpty())return;
    appendLog(message);
    if(videoRecoveryCount_>=1){
        videoRecoveryExhausted_=true;cancelSubtitleWork();pendingResume_.reset();directResume_=false;recoveryPlayback_.reset();recoveryExternalCues_.clear();pendingSeek_=-1;
        requestedPlaying_=false;releaseCaptionBuffer(false);engine->setPaused(true);status_->setText("영상 출력 복구 후 장치 오류가 다시 발생했습니다. 재생을 멈췄습니다. 미디어를 다시 열어 주세요.");status_->setProperty("error",true);return;
    }
    const auto state=engine->state();const bool disc=engine==discEngine_.get(),playing=wantsPlayback();
    const auto source=source_;const auto prepared=preparedCaptions_;const auto targetDuration=discTargetDurationMs_;
    const auto external=prepared?QVector<SubtitleCue>{}:externalCues_;
    std::optional<ResumePoint> point;
    if(state.timelineReady && state.durationMs>0 && (!disc || (!state.navigationTitle && state.currentTitle>=0)))
        point=ResumePoint{source,disc,disc?state.currentTitle:-1,pendingSeek_>=0?pendingSeek_:state.positionMs,state.durationMs,state.discMenus,preparedApplied_ && preparedCaptions_?preparedCaptions_->projectFile:QString{}};
    savePlaybackProgress(true);cancelSubtitleWork();releaseCaptionBuffer(false);engine->stop();active_=nullptr;
    static_cast<VideoSurface*>(surface_)->setVideoActive(false);
    auto &replacement=disc?discEngine_:fileEngine_;
    replacement=std::make_unique<PlaybackEngine>(disc?PlaybackEngine::Kind::Disc:PlaybackEngine::Kind::File,QString{},this);bindEngine(replacement.get());
    if(prepared)playPreparedCaptions(*prepared);else openSourceImpl(source,false,targetDuration);
    videoRecoveryCount_=1;requestedPlaying_=playing;recoveryAudio_=state.currentAudio;recoverySubtitle_=state.currentSubtitle;
    recoveryExternalCues_=external;
    if(!active_ || active_->state().failed){
        pendingResume_.reset();recoveryPlayback_.reset();recoveryExternalCues_.clear();
        if(!active_){requestedPlaying_=false;videoRecoveryExhausted_=true;preparedCaptions_.reset();preparedApplied_=false;resetSubtitleSession();status_->setText("원본 미디어가 변경되어 영상 출력을 복구하지 못했습니다. 미디어를 다시 열어 주세요.");status_->setProperty("error",true);}
        return;
    }
    pendingResume_=point;directResume_=disc && point.has_value();recoveryPlayback_=point?std::optional<bool>(playing):std::nullopt;
    if(!point && !recoveryExternalCues_.isEmpty()){const auto cues=std::move(recoveryExternalCues_);recoveryExternalCues_.clear();applyExternalCaptions(cues);}
    if(bufferHolding_)bufferResume_=playing;
    active_->setPaused(disc && point?false:point.has_value() || !playing);
    status_->setText("그래픽 장치 연결이 끊겨 영상 출력을 다시 만들었습니다. 같은 위치로 복구하는 중…");
}

void MainWindow::buildUi(){
    installPlayerTheme();
    auto *central=new QWidget;setCentralWidget(central);auto *outer=new QVBoxLayout(central);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    header_=new QWidget;header_->setObjectName("header");auto *top=new QHBoxLayout(header_);top->setContentsMargins(24,13,24,13);top->setSpacing(16);
    top->addWidget(new BrandMark);top->addWidget(label("나만의 시네마","muted"));top->addStretch();
    themeButton_=button("라이트 모드","themeToggle");themeButton_->setAccessibleName("화면 테마 전환");top->addWidget(themeButton_);
    auto *open=button("파일 열기","openMediaButton");open->setToolTip("미디어 열기 · Ctrl+O");top->addWidget(open);discButton_=button("Blu-ray 재생","primary");top->addWidget(discButton_);outer->addWidget(header_);
    auto *body=new QHBoxLayout;body->setSpacing(0);body->setContentsMargins(0,0,0,0);outer->addLayout(body,1);
    sidebar_=new QWidget;sidebar_->setObjectName("sidebar");sidebar_->setFixedWidth(174);auto *nav=new QVBoxLayout(sidebar_);nav->setContentsMargins(17,25,17,20);nav->setSpacing(14);
    nav->addWidget(label("YOUR COLLECTION","section"));auto *files=button("로컬 미디어");nav->addWidget(files);resumeButton_=button("이어서 보기","resumePlayback");resumeButton_->setEnabled(false);nav->addWidget(resumeButton_);connect(resumeButton_,&QPushButton::clicked,this,&MainWindow::resumeLatest);nav->addSpacing(22);nav->addWidget(label("연결된 디스크","section"));
    discLabel_=label("디스크 확인 중…");discLabel_->setWordWrap(true);nav->addWidget(discLabel_);discDetails_=label({},"muted");discDetails_->setWordWrap(true);nav->addWidget(discDetails_);
    auto *rescan=button("새로 확인","subtle");nav->addWidget(rescan);nav->addStretch();nav->addWidget(label("MADE FOR\nYOUR SCREEN","section"));body->addWidget(sidebar_);
    auto *center=new QWidget;auto *centerLayout=new QVBoxLayout(center);centerLayout->setContentsMargins(0,0,0,0);centerLayout->setSpacing(0);
    surface_=new VideoSurface;surface_->setObjectName("videoSurface");surface_->setFocusPolicy(Qt::StrongFocus);surface_->setMinimumSize(500,280);surface_->setMouseTracking(true);
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
    auto *aiIntro=label("원문부터 번역까지.\n내 기기에서 함께.","nowTitle");aiIntro->setWordWrap(true);aiLayout->addWidget(aiIntro);
    auto *prepare=button("재생 전 전체 자막 만들기","prepareCaptions");prepare->setProperty("primary",true);aiLayout->addWidget(prepare);connect(prepare,&QPushButton::clicked,this,&MainWindow::showCaptionPreparation);
    auto *loadPrepared=button("미리 만든 자막 프로젝트 열기","loadPreparedCaptions");aiLayout->addWidget(loadPrepared);connect(loadPrepared,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"미리 만든 자막 프로젝트",{},"자막 프로젝트 (*.json)");if(!path.isEmpty())loadPreparedCaptionProject(path);});
    aiEnabled_=new QCheckBox("AI 자동자막 켜기");aiEnabled_->setObjectName("aiEnabled");aiEnabled_->setChecked(QSettings().value("subtitles/aiEnabled",false).toBool());aiLayout->addWidget(aiEnabled_);
    bufferStatus_=label("재생 전·탐색 후 자막을 10초 이상 준비합니다.","muted");bufferStatus_->setObjectName("captionBufferStatus");bufferStatus_->setWordWrap(true);aiLayout->addWidget(bufferStatus_);
    auto *bufferActions=new QHBoxLayout;auto *retry=button("자막 다시 시도","subtle");retry->setObjectName("retryCaptions");auto *continuePlay=button("자막 없이 계속","subtle");continuePlay->setObjectName("continueWithoutCaptions");bufferActions->addWidget(retry);bufferActions->addWidget(continuePlay);aiLayout->addLayout(bufferActions);
    choice(aiLayout,"음성 언어",language_);language_->addItem("日本語 · 일본어","ja");language_->addItem("English · 영어","en");language_->addItem("한국어","ko");language_->addItem("자동 감지","auto");
    choice(aiLayout,"음성 인식 가속",aiMode_);aiMode_->setObjectName("aiMode");aiMode_->addItem("자동 · 설치 테스트 기준","auto");aiMode_->addItem("NPU 우선 · 수동 선택","npu");aiMode_->addItem("GPU","gpu");aiMode_->addItem("CPU","cpu");setSavedChoice(aiMode_,"ai/mode","auto");
    npuStatus_=label({},"muted");npuStatus_->setObjectName("npuCalibrationStatus");npuStatus_->setWordWrap(true);aiLayout->addWidget(npuStatus_);
    choice(aiLayout,"AI GPU",aiGpu_);aiGpu_->setObjectName("aiGpu");aiGpu_->addItem("자동 GPU 선택",QString{});const auto savedGpu=QSettings().value("ai/gpu").toString();if(!savedGpu.isEmpty()){aiGpu_->addItem("저장된 GPU · "+savedGpu,savedGpu);aiGpu_->setCurrentIndex(1);}
    auto *model=button("음성 인식 모델 선택","subtle");aiLayout->addWidget(model);accelerator_=label("음성 인식 · 실행 대기","muted");accelerator_->setObjectName("accelerator");accelerator_->setWordWrap(true);aiLayout->addWidget(accelerator_);
    aiLayout->addSpacing(6);aiLayout->addWidget(label("표시할 자막","section"));originalEnabled_=new QCheckBox("원문");originalEnabled_->setObjectName("originalEnabled");originalEnabled_->setChecked(QSettings().value("subtitles/original",true).toBool());aiLayout->addWidget(originalEnabled_);
    readingEnabled_=new QCheckBox("일본어 한글 독음");readingEnabled_->setObjectName("readingEnabled");readingEnabled_->setChecked(QSettings().value("subtitles/hangul",true).toBool());readingEnabled_->setEnabled(reading_.available());if(!reading_.available())readingEnabled_->setToolTip(reading_.error());aiLayout->addWidget(readingEnabled_);
    translationEnabled_=new QCheckBox("한국어 번역");translationEnabled_->setObjectName("translationEnabled");translationEnabled_->setChecked(QSettings().value("subtitles/translation",true).toBool());aiLayout->addWidget(translationEnabled_);
    translationBackend_=label("번역 · 로컬 모델 실행 대기","muted");translationBackend_->setWordWrap(true);aiLayout->addWidget(translationBackend_);translationStatus_=label("원문이 준비되면 가까운 구간부터 번역합니다.","muted");translationStatus_->setWordWrap(true);aiLayout->addWidget(translationStatus_);
    auto *translationModel=button("번역 모델 선택","subtle");aiLayout->addWidget(translationModel);
    choice(aiLayout,"번역 가속",translationMode_);translationMode_->setObjectName("translationMode");translationMode_->addItem("자동 · 사용 가능한 가속","auto");translationMode_->addItem("GPU 우선","gpu");translationMode_->addItem("CPU","cpu");translationMode_->addItem("NPU · 호환 모델/런타임 필요","npu");setSavedChoice(translationMode_,"translation/mode","auto");
    choice(aiLayout,"번역 GPU",translationGpu_);translationGpu_->setObjectName("translationGpu");translationGpu_->addItem("자동 GPU 선택",QString{});const auto savedTranslationGpu=QSettings().value("translation/gpu").toString();if(!savedTranslationGpu.isEmpty()){translationGpu_->addItem("저장된 GPU · "+savedTranslationGpu,savedTranslationGpu);translationGpu_->setCurrentIndex(1);}
    auto *translationNote=label("음성 인식과 별도로 선택합니다. 실제 사용 장치는 위에 표시됩니다.","muted");translationNote->setWordWrap(true);aiLayout->addWidget(translationNote);
    aiStatus_=label("AI 자동자막 꺼짐","muted");aiStatus_->setWordWrap(true);aiLayout->addWidget(aiStatus_);auto *note=label("독음·번역은 원문과 별도로 보관됩니다. 노래나 배경음이 강하면 인식 정확도가 낮아질 수 있습니다.","muted");note->setWordWrap(true);aiLayout->addWidget(note);auto *save=button("표시 자막 저장","primary");aiLayout->addWidget(save);aiLayout->addStretch();tabs->addTab(scrollPanel(aiTab),"AI 자막");
    auto *settingsTab=new QWidget;auto *settingsLayout=new QVBoxLayout(settingsTab);settingsLayout->setContentsMargins(17,18,17,18);settingsLayout->setSpacing(12);settingsLayout->addWidget(label("PLAYBACK & PRIVACY","section"));
    choice(settingsLayout,"파일 영상 GPU",videoGpu_);videoGpu_->setObjectName("videoGpu");videoGpu_->addItem("자동 선택",QString{});for(const auto &entry:videoAdapters()){const auto adapter=entry.toObject();videoGpu_->addItem(adapter.value("name").toString(),adapter.value("id").toString());}setSavedChoice(videoGpu_,"video/adapter");
    auto *videoNote=label("파일 재생에 적용됩니다. 재생 중 변경하면 현재 위치에서 다시 연결합니다.","muted");videoNote->setWordWrap(true);settingsLayout->addWidget(videoNote);
    choice(settingsLayout,"프레임 보간",motionMode_);motionMode_->setObjectName("motionMode");motionMode_->addItem("꺼짐","off");motionMode_->addItem("부드러운 표시 · GPU 블렌딩","display");motionMode_->addItem("모션 보간 · 실험적 CPU 처리","optical");setSavedChoice(motionMode_,"motion/mode","off");
    choice(settingsLayout,"모션 보간 목표 FPS",motionFps_);motionFps_->setObjectName("motionFps");for(int fps:{60,90,120,144,165,240,360})motionFps_->addItem(QString::number(fps)+" fps",fps);setSavedChoice(motionFps_,"motion/fps","120");
    motionStatus_=label("파일 영상에서 사용할 수 있습니다.","muted");motionStatus_->setObjectName("motionStatus");motionStatus_->setWordWrap(true);settingsLayout->addWidget(motionStatus_);
    auto *motionNote=label("파일 전용 · 디스크 메뉴에는 적용하지 않습니다.\nCPU 모션 보간은 부하가 높습니다. 목표 FPS와 실제 처리·출력 FPS는 다를 수 있습니다.","muted");motionNote->setWordWrap(true);settingsLayout->addWidget(motionNote);
    displayStatus_=label("디스플레이 정보 확인 중…","displayStatus");displayStatus_->setWordWrap(true);settingsLayout->addWidget(displayStatus_);
    captureStatus_=label("보호 디스크를 재생하면 캡처 차단을 적용합니다.","captureStatus");captureStatus_->setWordWrap(true);settingsLayout->addWidget(captureStatus_);auto *privacyNote=label("개발용 캡처 허용은 개발자 모드에서 별도 동의 후 설정할 수 있습니다. Windows 캡처 제외는 DRM·HDCP 인증과는 별개입니다.","muted");privacyNote->setWordWrap(true);settingsLayout->addWidget(privacyNote);
    developerEnabled_=new QCheckBox("개발자 모드");developerEnabled_->setObjectName("developerMode");
    developerEnabled_->setChecked(QSettings().value("ui/developerMode",false).toBool() && QSettings().value("privacy/developerConsentVersion",0).toInt()==privacyConsentVersion);settingsLayout->addWidget(developerEnabled_);
    developerApproved_=developerEnabled_->isChecked();
    auto *developerPanel=new QWidget;developerPanel->setObjectName("developerDiagnostics");auto *developerLayout=new QVBoxLayout(developerPanel);developerLayout->setContentsMargins(0,0,0,0);
    captureAllowed_=new QCheckBox("개발용 영상 캡처 허용");captureAllowed_->setObjectName("captureAllowed");captureAllowed_->setToolTip("캡처 차단을 해제하면 Parsec과 화면 캡처에 영상·자막이 보일 수 있습니다.");
    captureAllowed_->setChecked(developerEnabled_->isChecked() && QSettings().value("privacy/captureAllowed",false).toBool() && QSettings().value("privacy/captureConsentVersion",0).toInt()==privacyConsentVersion);developerLayout->addWidget(captureAllowed_);
    captureApproved_=captureAllowed_->isChecked();
    if(!captureAllowed_->isChecked()){QSettings().setValue("privacy/captureAllowed",false);QSettings().setValue("privacy/captureConsentVersion",0);}
    developerLayout->addWidget(label("PLAYBACK LOG","section"));log_=new QPlainTextEdit;log_->setReadOnly(true);log_->setMaximumBlockCount(500);log_->setMinimumHeight(170);developerLayout->addWidget(log_);developerPanel->setVisible(developerEnabled_->isChecked());codec_->setVisible(developerEnabled_->isChecked());settingsLayout->addWidget(developerPanel,1);settingsLayout->addStretch();tabs->addTab(scrollPanel(settingsTab),"설정");
    connect(developerEnabled_,&QCheckBox::toggled,this,[this,developerPanel](bool enabled){
        if(enabled && !confirmDeveloperUse(this,false)){QSignalBlocker guard(developerEnabled_);developerEnabled_->setChecked(false);enabled=false;}
        developerApproved_=enabled;
        QSettings().setValue("ui/developerMode",enabled);QSettings().setValue("privacy/developerConsentVersion",enabled?privacyConsentVersion:0);
        if(!enabled)captureAllowed_->setChecked(false);
        developerPanel->setVisible(enabled);codec_->setVisible(enabled);applyCapturePolicy();
    });
    connect(captureAllowed_,&QCheckBox::toggled,this,[this](bool allowed){
        if(allowed && (!developerEnabled_->isChecked() || !confirmDeveloperUse(this,true))){QSignalBlocker guard(captureAllowed_);captureAllowed_->setChecked(false);allowed=false;}
        captureApproved_=allowed;
        QSettings().setValue("privacy/captureAllowed",allowed);QSettings().setValue("privacy/captureConsentVersion",allowed?privacyConsentVersion:0);applyCapturePolicy();
    });
    auto *captionMenu=new QMenu(quick);quick->setMenu(captionMenu);
    for(auto *check:{originalEnabled_,readingEnabled_,translationEnabled_,aiEnabled_}){auto *action=captionMenu->addAction(check->text());action->setCheckable(true);action->setChecked(check->isChecked());action->setEnabled(check->isEnabled());connect(action,&QAction::toggled,check,&QCheckBox::setChecked);connect(check,&QCheckBox::toggled,action,&QAction::setChecked);}
    auto *remoteMenu=new QMenu(remoteButton);remoteButton->setMenu(remoteMenu);auto *remoteHint=remoteMenu->addAction("디스크 조작: 마우스 / Ctrl+방향키 / Ctrl+Enter");remoteHint->setEnabled(false);remoteMenu->addSeparator();
    remoteMenu->addAction("메인 메뉴",topMenu,&QPushButton::click);remoteMenu->addAction("팝업 메뉴",popup,&QPushButton::click);remoteMenu->addSeparator();
    for(int i=0;i<actions.size();++i)remoteMenu->addAction(names[i],this,[this,action=actions[i]]{if(active_==discEngine_.get())active_->navigate(discActionCode(action));});
    connect(themeButton_,&QPushButton::clicked,this,[this]{setPlayerDarkTheme(!playerUsesDarkTheme());applyTheme();});
    connect(open,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"미디어 열기",{},"미디어 (*.mkv *.mp4 *.webm *.mov *.avi *.m2ts *.ts *.flac *.alac *.m4a *.mp3 *.wav);;모든 파일 (*)");if(!path.isEmpty())openSource(path);});connect(files,&QPushButton::clicked,open,&QPushButton::click);
    connect(rescan,&QPushButton::clicked,this,&MainWindow::discoverDisc);connect(discButton_,&QPushButton::clicked,this,[this]{if(discPath_.isEmpty())discoverDisc();if(!discPath_.isEmpty())openSource(discPath_);else status_->setText("연결된 Blu-ray를 찾지 못했습니다.");});
    connect(play_,&QPushButton::clicked,this,&MainWindow::togglePlayback);
    connect(back,&QPushButton::clicked,this,[this]{if(active_)seekTo(active_->state().positionMs-10000);});connect(forward,&QPushButton::clicked,this,[this]{if(active_)seekTo(active_->state().positionMs+10000);});
    connect(previous,&QPushButton::clicked,this,[this]{if(active_)selectChapter(std::max(0,active_->state().currentChapter-1));});connect(next,&QPushButton::clicked,this,[this]{if(active_)selectChapter(active_->state().currentChapter+1);});
    connect(seek_,&QSlider::sliderReleased,this,[this]{if(active_)seekTo(active_->state().durationMs*seek_->value()/10000);});
    connect(volume_,&QSlider::valueChanged,this,[this,volumeIcon](int volume){QSettings().setValue("playback/volume",volume);if(volume>0){lastUnmutedVolume_=volume;QSettings().setValue("playback/lastUnmutedVolume",volume);}volumeIcon->setToolTip(volume?"음소거 · M":"음소거 해제 · M");if(active_)active_->setVolume(volume);});
    connect(volumeIcon,&QPushButton::clicked,this,&MainWindow::toggleMute);
    connect(rate_,&QComboBox::activated,this,[this]{if(active_)active_->setRate(rate_->currentData().toDouble());});connect(full,&QPushButton::clicked,this,&MainWindow::toggleFullScreen);
    connect(topMenu,&QPushButton::clicked,this,[this]{if(active_==discEngine_.get()){selectTitle(0);surface_->setFocus();}});
    connect(popup,&QPushButton::clicked,this,[this]{if(active_==discEngine_.get()){active_->navigate(discActionCode(DiscAction::Popup));surface_->setFocus();}});
    connect(titles_,&QComboBox::activated,this,[this]{selectTitle(titles_->currentData().toInt());});
    connect(audio_,&QComboBox::activated,this,[this]{selectAudio(audio_->currentData().toInt());});
    connect(subs_,&QComboBox::activated,this,[this]{if(active_){subtitleFileLoader_->cancel();aiEnabled_->setChecked(false);externalCues_.clear();presentationDirty_=true;rebuildManagedCaptions();active_->setSubtitle(subs_->currentData().toInt());}});
    connect(chapters_,&QListWidget::itemActivated,this,[this](QListWidgetItem *item){selectChapter(item->data(Qt::UserRole).toInt());});
    connect(external,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"자막 열기",{},"텍스트 자막 (*.srt *.ass *.ssa *.vtt *.smi *.sami)");if(!path.isEmpty())loadCaptionFile(path);});
    connect(model,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"Whisper 모델",{},"Whisper 모델 (*.bin)");if(!path.isEmpty()){resetSubtitleSession(true);model_=path;QSettings().setValue("ai/model",path);updateNpuStatus();aiStatus_->setText("음성 인식 모델 선택됨");if(liveCaptioningActive())job_.warmUp(model_);scheduleSubtitles();}});
    connect(translationModel,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"로컬 번역 모델",{},"GGUF 모델 (*.gguf)");if(!path.isEmpty()){translation_->cancel();translationPending_.clear();clearTranslationSkips();if(!preparedCaptions_)translations_.clear();translationFailed_=false;QSettings().setValue("translation/model",path);translation_->setModel(path);rebuildManagedCaptions();scheduleTranslations();if(preparedCaptions_)translationStatus_->setText("선택한 모델은 다음 자막 생성부터 사용합니다.");}});
    connect(aiMode_,&QComboBox::activated,this,&MainWindow::updateAiPreferences);connect(aiGpu_,&QComboBox::activated,this,&MainWindow::updateAiPreferences);connect(videoGpu_,&QComboBox::activated,this,&MainWindow::changeVideoAdapter);
    connect(translationMode_,&QComboBox::activated,this,&MainWindow::updateTranslationPreferences);connect(translationGpu_,&QComboBox::activated,this,&MainWindow::updateTranslationPreferences);
    connect(motionMode_,&QComboBox::activated,this,&MainWindow::updateMotionSettings);connect(motionFps_,&QComboBox::activated,this,&MainWindow::updateMotionSettings);
    connect(aiEnabled_,&QCheckBox::toggled,this,&MainWindow::automaticCaptionsChanged);
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
    // Playback keys are dispatched by the application event filter so the
    // owned video and transport windows have the same keyboard behavior.
    connect(language_,&QComboBox::activated,this,[this]{resetSubtitleSession(true);scheduleSubtitles();});
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
    captureGuard_->setEnabled(protectedDisc && !(developerApproved_ && captureApproved_));
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
    aiGpu_->setEnabled(preferences.mode=="auto" || preferences.mode=="gpu");
    aiFailed_=false;translationFailed_=false;nextAiAttemptMs_=0;aiRetryCount_=0;
    accelerator_->setText("음성 인식 · 선택한 기기에서 실행 대기");
    updateNpuStatus();
    if(liveCaptioningActive())job_.warmUp(model_);
    if(active_)scheduleSubtitles();
}
void MainWindow::updateNpuStatus(){
    const bool kotoba=QFileInfo(model_).completeBaseName()=="ggml-kotoba-whisper-v2.0-q5_0";
    npuStatus_->setVisible(kotoba);if(!kotoba)return;
    const auto result=npuCalibrationStatus(QCoreApplication::applicationDirPath()+"/runtime",model_);
    npuStatus_->setProperty("calibrationReason",result.reason);npuStatus_->setProperty("calibrationEligible",result.eligible);
    if(result.eligible)npuStatus_->setText(QString("NPU 설치 검사 통과 · 연속 %1회 · 최악 %2초").arg(result.completedRequests).arg(std::max(result.maxWarmInferenceMs,result.maxWarmWallMs)/1000.,0,'f',2));
    else if(result.reason=="too-slow")npuStatus_->setText("이 모델의 NPU 처리가 늦어 자동 모드에서 GPU를 우선합니다.");
    else if(result.state==NpuCalibrationState::Missing)npuStatus_->setText("NPU 설치 검사 전에는 자동 모드에서 GPU를 우선합니다.");
    else if(result.state==NpuCalibrationState::Stale)npuStatus_->setText("모델·드라이버·런타임이 바뀌어 NPU 재검사가 필요합니다. 자동 모드는 GPU를 사용합니다.");
    else npuStatus_->setText("안정적인 NPU 실행을 확인하지 못해 자동 모드에서 GPU를 우선합니다.");
    if(aiMode_->currentData()=="npu" && !result.eligible)npuStatus_->setText(npuStatus_->text()+"\n현재는 NPU를 수동 선택했습니다. 실패하면 GPU로 전환합니다.");
}
void MainWindow::updateTranslationPreferences(){
    const AiPreferences preferences{translationMode_->currentData().toString(),translationGpu_->currentData().toString()};
    QSettings().setValue("translation/mode",preferences.mode);QSettings().setValue("translation/gpu",preferences.gpuDevice);translationGpu_->setEnabled(preferences.mode=="auto" || preferences.mode=="gpu");
    if(translation_ && (translation_->preferences().mode!=preferences.mode || translation_->preferences().gpuDevice!=preferences.gpuDevice)){translation_->setPreferences(preferences);translationPending_.clear();translations_.clear();clearTranslationSkips();translationFailed_=false;rebuildManagedCaptions();}
}
void MainWindow::changeVideoAdapter(){
    const auto selected=videoGpu_->currentData().toString();if(QSettings().value("video/adapter").toString()==selected)return;
    QSettings().setValue("video/adapter",selected);
    const bool reopen=active_==fileEngine_.get();const qint64 position=reopen?active_->state().positionMs:0;
    const bool playing=reopen?wantsPlayback():false;
    if(reopen){cancelSubtitleWork();preparedApplied_=false;releaseCaptionBuffer(false);active_->stop();active_=nullptr;}
    fileEngine_.reset();fileEngine_=std::make_unique<PlaybackEngine>(PlaybackEngine::Kind::File,QString{},this);bindEngine(fileEngine_.get());updateMotionSettings();
    if(reopen){
        active_=fileEngine_.get();if(active_->open(source_,static_cast<VideoSurface*>(surface_)->videoWindowId())){active_->setVolume(volume_->value());active_->setRate(rate_->currentData().toDouble());active_->setPaused(true);active_->seek(position);pendingSeek_=position;seekRequestedAt_=QDateTime::currentMSecsSinceEpoch();if(liveCaptioningActive()){bufferHolding_=true;bufferResume_=playing;}else active_->setPaused(!playing);}
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
void MainWindow::showCaptionPreparation(){
    const auto initialFile=active_==fileEngine_.get()?source_:QString{};
    CaptionPreparationRequest defaults;defaults.model=model_;defaults.source.language=language_->currentData().toString();defaults.recognitionPreferences=job_.preferences();defaults.translationPreferences=translation_->preferences();defaults.translate=translationEnabled_->isChecked();
    const auto root=QCoreApplication::applicationDirPath()+"/runtime/translation/models/";
    const auto quality=root+"Hy-MT2-7B-Q4_K_M.gguf";
    defaults.translationModel=QSettings().value("translation/model",QFileInfo::exists(quality)?quality:root+"Qwen3-4B-Q4_K_M.gguf").toString();
    savePlaybackProgress(true);preparedCaptions_.reset();preparedApplied_=false;resetSubtitleSession();job_.shutdown();translation_->setModel(defaults.translationModel);
    releaseCaptionBuffer(false);if(active_)active_->stop();active_=nullptr;pendingResume_.reset();requestedPlaying_=false;
    static_cast<VideoSurface*>(surface_)->setVideoActive(false);welcome_->show();applyCapturePolicy();status_->setText("영상 재생 없이 전체 자막을 준비합니다.");
    CaptionPreparationDialog dialog(defaults,discPath_,initialFile,this);preparationDialog_=&dialog;
    const int result=dialog.exec();preparationDialog_.clear();
    if(result==QDialog::Accepted)if(auto prepared=dialog.prepared())playPreparedCaptions(std::move(*prepared));
}
bool MainWindow::loadPreparedCaptionProject(const QString &path){
    QString error;auto result=CaptionPreparationService::loadProject(path,&error);
    if(!result){status_->setText("자막 프로젝트를 열 수 없습니다. "+error);return false;}
    return playPreparedCaptions(std::move(*result));
}
bool MainWindow::playPreparedCaptions(CaptionPreparationResult result){
    QString error;if(!CaptionPreparationService::sourceUnchanged(result,&error)){status_->setText("준비한 자막의 미디어가 변경됐습니다. "+error);return false;}
    const auto source=result.request.source.source;openSourceImpl(source,true,result.request.source.disc?result.request.durationMs:0);subtitleFileLoader_->cancel();
    if(!active_ || active_->state().failed)return false;
    if(pendingResume_ && std::abs(pendingResume_->durationMs-result.request.durationMs)>1000)pendingResume_.reset();
    preparedCaptions_=std::move(result);preparedApplied_=false;applyPreparedCaptions();
    aiStatus_->setText("전체 자막 프로젝트 준비됨 · 해당 본편에서 표시합니다.");
    return true;
}
void MainWindow::applyPreparedCaptions(){
    if(!preparedCaptions_ || preparedApplied_ || !active_)return;
    const auto &project=*preparedCaptions_;const auto &state=active_->state();
    if(!state.timelineReady || state.navigationTitle || state.failed)return;
    if(!preparedCaptionSourceTimelineMatches(project.request.source,project.request.durationMs,source_,state,active_==discEngine_.get()?&discInfo_:nullptr))return;
    QVector<int> audio;for(const auto &track:state.audioTracks)if(track.id>=0)audio.append(track.id);
    const int index=project.request.source.audioIndex;
    if(index>=0 && index<audio.size() && state.currentAudio!=audio[index]){active_->setAudio(audio[index]);return;}
    if(!preparedCaptionTimelineMatches(project.request.source,project.request.durationMs,source_,state,active_==discEngine_.get()?&discInfo_:nullptr))return;
    QString error;if(!CaptionPreparationService::sourceUnchanged(project,&error)){preparedCaptions_.reset();aiStatus_->setText("미디어가 변경돼 준비한 자막을 적용하지 않았습니다. "+error);return;}
    job_.shutdown();translation_->cancel();translationPending_.clear();clearTranslationSkips();
    externalCues_=project.originals;readings_.clear();translations_=project.translations;translationFailed_=false;preparedApplied_=true;presentationDirty_=true;
    active_->setSubtitle(-1);rebuildManagedCaptions();releaseCaptionBuffer(true);
    bufferStatus_->setText("전체 자막 준비됨 · 탐색 후에도 바로 표시합니다.");
    aiStatus_->setText(QString("미리 만든 원문 %1개 · 한국어 %2개").arg(externalCues_.size()).arg(translations_.size()));
    translationStatus_->setText(project.request.translate?QString("전체 문맥으로 미리 준비한 한국어 %1개%2").arg(translations_.size()).arg(project.skippedTranslations?QString(" · %1구간 제외").arg(project.skippedTranslations):QString{}):"원문 자막을 미리 준비했습니다.");
}
void MainWindow::openSource(const QString &source){openSourceImpl(source,false);}
void MainWindow::openSourceImpl(const QString &source,bool preparedOpen,qint64 preparedDiscDurationMs){
    savePlaybackProgress(true);
    preparedCaptions_.reset();preparedApplied_=false;
    ++sourceGeneration_;videoRecoveryCount_=0;videoRecoveryExhausted_=false;recoveryPlayback_.reset();recoveryAudio_=-1;recoverySubtitle_=-1;recoveryExternalCues_.clear();
    if(subtitleFileLoader_)subtitleFileLoader_->cancel();pendingResume_.reset();directResume_=false;
    resetSubtitleSession();releaseCaptionBuffer(false);if(active_)active_->stop();lastTitle_=-1;lastNavigation_=false;lastAudio_=-2;decodedSamples_=0;pendingSeek_=-1;pendingAudio_=-2;pendingChapter_=-2;pendingTitle_=-2;requestedPlaying_=true;
    const bool disc=isDiscSource(source);source_=disc?localDiscPath(source):source;if(disc)discInfo_=inspectDisc(source_);
    discTargetDurationMs_=disc?preparedDiscDurationMs:0;
    resumeIdentity_=resumeIdentity(source_,disc?&discInfo_:nullptr);pendingResume_=resumeHistory_.find(resumeIdentity_,-1,preparedDiscDurationMs==0);resumeSavedAt_=0;
    active_=disc?discEngine_.get():fileEngine_.get();nowTitle_->setText(disc?(discInfo_.label.isEmpty()?"Blu-ray":discInfo_.label):QFileInfo(source).fileName());
    surface_->setFocus(Qt::OtherFocusReason);welcome_->hide();status_->setProperty("error",false);status_->setText("미디어를 여는 중…");
    const auto videoWindow=static_cast<VideoSurface*>(surface_)->videoWindowId();
    const bool opened=disc && preparedDiscDurationMs>0?active_->openPreparedDisc(source_,videoWindow,preparedDiscDurationMs):active_->open(source_,videoWindow);
    if(opened){active_->setVolume(volume_->value());active_->setRate(rate_->currentData().toDouble());if(!disc && (preparedOpen || liveCaptioningActive())){bufferHolding_=true;bufferResume_=true;active_->setPaused(true);}}
    else{status_->setText(active_->lastError());welcome_->show();}
    applyCapturePolicy();static_cast<VideoSurface*>(surface_)->setVideoActive(opened);updateDisplayStatus();updateMotionSettings();
    if(opened && !disc && !preparedOpen){
        const QFileInfo media(source_);const auto base=media.dir().filePath(media.completeBaseName());
        for(const auto &extension:QStringList{"srt","ass","ssa","vtt","smi","sami"})if(QFileInfo::exists(base+'.'+extension)){loadCaptionFile(base+'.'+extension);break;}
    }
}
void MainWindow::enableAutomaticSubtitles(const QString &language){const int index=language_->findData(language);if(index>=0){language_->setCurrentIndex(index);if(aiEnabled_->isChecked())automaticCaptionsChanged(true);else aiEnabled_->setChecked(true);}}
void MainWindow::automaticCaptionsChanged(bool enabled){
    if(subtitleFileLoader_)subtitleFileLoader_->cancel();presentationDirty_=true;QSettings().setValue("subtitles/aiEnabled",enabled);
    if(enabled){preparedCaptions_.reset();preparedApplied_=false;externalCues_.clear();aiFailed_=false;bufferBypass_=false;aiRetryCount_=0;nextAiAttemptMs_=0;if(active_)active_->setSubtitle(-1);rebuildManagedCaptions();job_.warmUp(model_);scheduleSubtitles();}
    else{cancelSubtitleWork();job_.shutdown();releaseCaptionBuffer(true);if(active_)active_->clearManagedSubtitles();aiStatus_->setText("AI 자동자막 꺼짐");bufferStatus_->setText("AI 자동자막 꺼짐 · 일반 재생");rebuildManagedCaptions();}
}
void MainWindow::updateResumeButton(){
    if(!resumeButton_)return;const auto point=resumeHistory_.latest();resumeButton_->setEnabled(point.has_value());
    resumeButton_->setToolTip(point?QString("%1 · %2부터").arg(point->disc?"Blu-ray":QFileInfo(point->source).fileName(),formatTime(point->positionMs)):"재생 기록이 생기면 이어서 볼 수 있습니다.");
}
void MainWindow::savePlaybackProgress(bool force){
    if(!active_ || resumeIdentity_.isEmpty() || pendingResume_ || pendingSeek_>=0 || pendingTitle_!=-2 || pendingChapter_!=-2 || pendingAudio_!=-2)return;
    const auto now=QDateTime::currentMSecsSinceEpoch();if(!force && now-resumeSavedAt_<5000)return;
    const auto &state=active_->state();const bool disc=active_==discEngine_.get();if(state.failed || !state.timelineReady || (disc && (state.navigationTitle || state.currentTitle<0)))return;
    resumeHistory_.save(resumeIdentity_,{source_,disc,disc?state.currentTitle:-1,state.positionMs,state.durationMs,state.discMenus,preparedApplied_ && preparedCaptions_?preparedCaptions_->projectFile:QString{}});resumeSavedAt_=now;updateResumeButton();
}
void MainWindow::restorePlaybackProgress(){
    if(!active_ || !pendingResume_ || videoRecoveryExhausted_)return;const auto state=active_->state();if(state.failed)return;
    const bool disc=active_==discEngine_.get();
    if(disc && state.discMenus!=pendingResume_->discMenus){pendingResume_.reset();return;}
    if(disc && !state.discMenus && !state.timelineReady)return;
    if(disc && directResume_){
        if(state.titles.isEmpty())return;
        // Disc-authored First Play may forbid title selection. Let it reach
        // the interactive menu rather than consuming the one restore attempt.
        if(state.navigationTitle && state.currentTitle!=0)return;
        const int title=pendingResume_->title;directResume_=false;
        if(std::none_of(state.titles.begin(),state.titles.end(),[title](const MediaItem &item){return item.id==title;})){pendingResume_.reset();status_->setText("저장된 디스크 타이틀을 찾을 수 없습니다.");return;}
        if(state.currentTitle!=title || state.navigationTitle){selectTitle(title);return;}
    }
    if(disc && (state.navigationTitle || state.currentTitle<0 || pendingTitle_!=-2))return;
    if(!state.timelineReady || state.durationMs<=0)return;
    if(disc && state.currentTitle!=pendingResume_->title){pendingResume_=resumeHistory_.find(resumeIdentity_,state.currentTitle,state.discMenus);if(!pendingResume_)return;}
    const auto point=*pendingResume_;pendingResume_.reset();
    if(std::abs(point.durationMs-state.durationMs)>2000 || (!recoveryPlayback_ && point.positionMs>=state.durationMs-5000)){
        resumeHistory_.remove(resumeIdentity_,point.title,point.discMenus);updateResumeButton();
        if(recoveryPlayback_){recoveryPlayback_.reset();recoveryExternalCues_.clear();requestedPlaying_=false;active_->setPaused(true);videoRecoveryExhausted_=true;status_->setText("미디어 길이가 달라 재생 위치를 복원하지 못했습니다. 미디어를 다시 열어 주세요.");status_->setProperty("error",true);}
        return;
    }
    if(recoveryPlayback_)requestedPlaying_=*recoveryPlayback_;
    seekTo(point.positionMs);appendLog(QString("이어서 보기 · %1").arg(formatTime(point.positionMs)));
    if(recoveryPlayback_){
        const bool playing=*recoveryPlayback_;
        if(std::any_of(state.audioTracks.begin(),state.audioTracks.end(),[this](const MediaItem &item){return item.id==recoveryAudio_;}))active_->setAudio(recoveryAudio_);
        if(std::any_of(state.subtitleTracks.begin(),state.subtitleTracks.end(),[this](const MediaItem &item){return item.id==recoverySubtitle_;}))active_->setSubtitle(recoverySubtitle_);
        requestedPlaying_=playing;if(bufferHolding_)bufferResume_=playing;else active_->setPaused(!playing);recoveryPlayback_.reset();
        if(!recoveryExternalCues_.isEmpty()){const auto cues=std::move(recoveryExternalCues_);recoveryExternalCues_.clear();applyExternalCaptions(cues);}
    }
}
void MainWindow::resumeLatest(){
    const auto point=resumeHistory_.latest();if(!point)return;QString source=point->source;const auto identity=resumeHistory_.latestIdentity();
    if(point->disc){
        if(!discPath_.isEmpty())source=discPath_;const auto disc=inspectDisc(source);
        if(resumeIdentity(source,&disc)!=identity){status_->setText("이어볼 Blu-ray 디스크를 연결해 주세요.");return;}
    }else if(resumeIdentity(source)!=identity){status_->setText("미디어 파일이 변경되었거나 이동해 저장 위치를 복원할 수 없습니다.");return;}
    if(!point->preparedProject.isEmpty()){
        QString error;auto project=CaptionPreparationService::loadProject(point->preparedProject,&error);
        if(project && preparedCaptionResumeMatches(*project,*point) && playPreparedCaptions(std::move(*project))){pendingResume_=point;directResume_=point->disc;return;}
    }
    openSourceImpl(source,false,point->disc && !point->discMenus?point->durationMs:0);pendingResume_=point;directResume_=point->disc;
}
void MainWindow::refresh(){
    if(!active_)return;const auto state=active_->state();
    if(videoRecoveryExhausted_)active_->setPaused(true);
    if(recoveryPlayback_ && pendingResume_){
        const bool targetReady=state.timelineReady && (active_!=discEngine_.get() || (!state.navigationTitle && state.currentTitle==pendingResume_->title));
        active_->setPaused(active_!=discEngine_.get() || targetReady);if(bufferHolding_)bufferResume_=*recoveryPlayback_;
    }
    if(requestedPlaying_ && !recoveryPlayback_ && !bufferHolding_ && state.playing==*requestedPlaying_)requestedPlaying_.reset();
    if(pendingAudio_!=-2 && state.currentAudio==pendingAudio_)pendingAudio_=-2;
    if(pendingChapter_!=-2 && state.currentChapter==pendingChapter_)pendingChapter_=-2;
    if(pendingTitle_!=-2 && state.currentTitle==pendingTitle_)pendingTitle_=-2;
    if((pendingAudio_!=-2 || pendingChapter_!=-2 || pendingTitle_!=-2) && QDateTime::currentMSecsSinceEpoch()-pendingControlAt_>8000){pendingAudio_=-2;pendingChapter_=-2;pendingTitle_=-2;aiStatus_->setText("선택한 트랙·챕터로 이동하지 못했습니다. 현재 위치에서 자막을 준비합니다.");}
    if(state.currentAudio>=0){
        if(lastAudio_!=-2 && lastAudio_!=state.currentAudio){
            const bool sameTimeline=active_!=discEngine_.get() || (state.currentTitle==lastTitle_ && state.navigationTitle==lastNavigation_);
            const bool detach=preparedApplied_ && sameTimeline;
            if(detach){preparedCaptions_.reset();preparedApplied_=false;}
            resetSubtitleSession(!detach);
            if(detach)aiStatus_->setText("오디오가 바뀌었습니다. 해당 오디오로 자막을 다시 준비할 수 있습니다.");
        }
        lastAudio_=state.currentAudio;
    }
    if(active_==discEngine_.get() && (state.currentTitle!=lastTitle_ || state.navigationTitle!=lastNavigation_)){resetSubtitleSession();lastTitle_=state.currentTitle;lastNavigation_=state.navigationTitle;if(state.navigationTitle)releaseCaptionBuffer(true);}
    if(pendingSeek_>=0 && (std::abs(state.positionMs-pendingSeek_)<=750 || QDateTime::currentMSecsSinceEpoch()-seekRequestedAt_>5000))pendingSeek_=-1;
    restorePlaybackProgress();applyPreparedCaptions();savePlaybackProgress();
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
    if(!state.failed && !videoRecoveryExhausted_){
        if(pendingResume_ && active_==discEngine_.get() && state.navigationTitle)status_->setText(state.currentTitle==0?"디스크 메뉴에서 재생을 선택하면 저장 위치로 이동합니다.":"디스크 시작 안내를 진행한 뒤 재생 위치를 복원합니다.");
        else if(bufferHolding_)status_->setText(bufferStatus_->text());else if(!aiFailed_)status_->setText(state.playing?"재생 중":"일시정지 / 준비 중");
    }
    syncCombo(titles_,state.titles);syncCombo(audio_,state.audioTracks);syncCombo(subs_,state.subtitleTracks);
    const auto select=[](QComboBox *box,int id){const int index=box->findData(id);if(index>=0 && index!=box->currentIndex()){QSignalBlocker guard(box);box->setCurrentIndex(index);}};
    select(titles_,state.currentTitle);select(audio_,state.currentAudio);select(subs_,state.currentSubtitle);
    if(chapters_->count()!=state.chapters.size()){chapters_->clear();for(const auto &chapter:state.chapters){auto *item=new QListWidgetItem(chapter.label.isEmpty()?QString("Chapter %1").arg(chapter.id+1):chapter.label);item->setData(Qt::UserRole,chapter.id);chapters_->addItem(item);}}
    if(active_==discEngine_.get())menuStatus_->setText(!state.discMenus?QString("준비한 타이틀 · 챕터 %1\n디스크 메뉴는 상단 ‘Blu-ray 재생’에서 열 수 있습니다.").arg(std::max(0,state.currentChapter)+1):state.currentTitle<=0?"디스크 메뉴 · 마우스 또는 Ctrl+방향키·Ctrl+Enter로 조작하세요.":state.navigationTitle?"디스크 시작 안내 · 메뉴를 준비하는 중…":QString("타이틀 %1 · 챕터 %2\n팝업 메뉴로 디스크 메뉴를 열 수 있습니다.").arg(state.currentTitle).arg(std::max(0,state.currentChapter)+1));
}
void MainWindow::appendLog(const QString &message){log_->appendPlainText(QTime::currentTime().toString("HH:mm:ss")+"  "+message);}
void MainWindow::toggleFullScreen(){
    full_=!full_;sidebar_->setVisible(!full_);panel_->setVisible(!full_);header_->setVisible(!full_);status_->setVisible(!full_);transportTitleRow_->setVisible(!full_);transport_->setFixedHeight(full_?74:108);
    static_cast<IconButton*>(fullButton_)->setPlayerIcon(full_?PlayerIcon::ExitFullscreen:PlayerIcon::Fullscreen);fullButton_->setToolTip(full_?"전체 화면 나가기 · Esc":"전체 화면 · F11");
    if(full_)showFullScreen();else showNormal();fullscreenControls_->setActive(full_);updateDisplayStatus();
}
bool MainWindow::wantsPlayback()const{if(videoRecoveryExhausted_ || (active_ && active_->state().failed))return false;if(recoveryPlayback_)return *recoveryPlayback_;return requestedPlaying_.value_or(bufferHolding_?bufferResume_:active_ && active_->state().playing);}
bool MainWindow::liveCaptioningActive()const{return aiEnabled_->isChecked() && !preparedCaptions_ && externalCues_.isEmpty();}
bool MainWindow::captionTimelineAvailable()const{
    if(!active_)return false;const auto &state=active_->state();
    return state.timelineReady && state.durationMs>0 && state.currentAudio>=0 && (active_!=discEngine_.get() || (!state.navigationTitle && state.currentTitle>=0 && !state.audioTracks.isEmpty()));
}
void MainWindow::togglePlayback(){
    if(!active_ || videoRecoveryExhausted_)return;
    requestedPlaying_=!wantsPlayback();
    if(recoveryPlayback_)recoveryPlayback_=*requestedPlaying_;
    if(bufferHolding_){bufferResume_=*requestedPlaying_;active_->setPaused(true);updateCaptionBuffer();}
    else active_->setPaused(!*requestedPlaying_);
}
void MainWindow::selectAudio(int id){if(!active_ || (active_->state().currentAudio==id && pendingAudio_==-2))return;const bool prepared=preparedCaptions_.has_value();preparedCaptions_.reset();preparedApplied_=false;resetSubtitleSession(!prepared);pendingAudio_=id;pendingControlAt_=QDateTime::currentMSecsSinceEpoch();active_->setAudio(id);if(id<0)releaseCaptionBuffer(true);else holdForCaptionBuffer();}
void MainWindow::selectChapter(int id){if(!active_)return;const auto resume=wantsPlayback();cancelSubtitleWork();pendingSeek_=-1;pendingChapter_=id;pendingControlAt_=QDateTime::currentMSecsSinceEpoch();bufferBypass_=false;if(liveCaptioningActive() && captionTimelineAvailable()){bufferHolding_=true;bufferResume_=resume;active_->setPaused(true);}active_->setChapter(id);}
void MainWindow::selectTitle(int id){if(!active_)return;savePlaybackProgress(true);resetSubtitleSession();pendingTitle_=id;pendingControlAt_=QDateTime::currentMSecsSinceEpoch();if(id<=0)releaseCaptionBuffer(true);active_->setTitle(id);}
void MainWindow::seekTo(qint64 position){
    if(!active_)return;const auto state=active_->state();const bool resume=wantsPlayback();
    cancelSubtitleWork();pendingChapter_=-2;position=std::clamp<qint64>(position,0,std::max<qint64>(0,state.durationMs));pendingSeek_=position;seekRequestedAt_=QDateTime::currentMSecsSinceEpoch();bufferBypass_=false;
    if(liveCaptioningActive() && captionTimelineAvailable()){bufferHolding_=true;bufferResume_=resume;active_->setPaused(true);}active_->seek(position);scheduleSubtitles();
}
void MainWindow::holdForCaptionBuffer(){
    if(!liveCaptioningActive() || !active_ || bufferBypass_ || !captionTimelineAvailable())return;
    const auto &state=active_->state();
    const auto position=pendingSeek_>=0?pendingSeek_:state.positionMs;if(position>=state.durationMs || coverage_.readyForPlayback(position,state.durationMs))return;
    if(!bufferHolding_){const bool resume=wantsPlayback();bufferHolding_=true;bufferResume_=resume;}
    active_->setPaused(true);
}
void MainWindow::updateCaptionBuffer(){
    if(!liveCaptioningActive() || !active_)return;
    const auto &state=active_->state();
    if(active_==discEngine_.get() && state.navigationTitle){releaseCaptionBuffer(true);bufferStatus_->setText("디스크 메뉴 · 본편에서 자막을 준비합니다.");return;}
    if(!state.timelineReady){
        if(active_==discEngine_.get() && state.currentTitle>0 && !bufferBypass_){if(!bufferHolding_){const bool resume=wantsPlayback();bufferHolding_=true;bufferResume_=resume;}active_->setPaused(true);}
        bufferStatus_->setText("타이틀 정보를 확인한 뒤 자막을 준비합니다.");return;
    }
    if(state.durationMs<=0)return;
    const auto position=pendingSeek_>=0?pendingSeek_:state.positionMs;const auto ahead=coverage_.bufferedAhead(position,state.durationMs);const auto needed=std::min<qint64>(CaptionCoverage::lookAheadMs,std::max<qint64>(0,state.durationMs-position));
    if(position>=state.durationMs){releaseCaptionBuffer(true);return;}
    if(pendingAudio_!=-2 || pendingChapter_!=-2 || pendingTitle_!=-2){bufferStatus_->setText("선택한 트랙·챕터로 이동하는 중…");return;}
    if(full_ && bufferHolding_)clock_->setText(QString("자막 %1 / %2초").arg(ahead/1000.,0,'f',1).arg(needed/1000.,0,'f',1));
    if(!aiFailed_)bufferStatus_->setText(QString("자막 %1 / %2초 준비%3").arg(ahead/1000.,0,'f',1).arg(needed/1000.,0,'f',1).arg(bufferHolding_?(bufferResume_?" · 준비 후 자동 재생":" · 일시정지 유지"):""));
    if(coverage_.readyForPlayback(position,state.durationMs) && coverage_.readyForPlayback(state.positionMs,state.durationMs))releaseCaptionBuffer(true);
    else holdForCaptionBuffer();
}
void MainWindow::releaseCaptionBuffer(bool continuePlayback){
    const bool resume=bufferHolding_ && bufferResume_ && continuePlayback;bufferHolding_=false;bufferResume_=false;if(resume && active_){requestedPlaying_=true;active_->setPaused(false);}
}
void MainWindow::retryCaptions(){
    updateNpuStatus();
    cancelSubtitleWork();aiRetryCount_=0;nextAiAttemptMs_=0;aiFailed_=false;bufferBypass_=false;translationFailed_=false;
    aiStatus_->setText(aiEnabled_->isChecked()?"자막 준비를 다시 시도합니다.":"AI 자동자막을 켜면 시작합니다.");scheduleSubtitles();scheduleTranslations();
}
void MainWindow::cancelSubtitleWork(){
    job_.cancel();coverage_.cancel();if(translation_)translation_->cancel();translationPending_.clear();if(!preparedApplied_)translations_.clear();clearTranslationSkips();aiFailed_=false;translationFailed_=false;aiRetryCount_=0;nextAiAttemptMs_=0;
    if(captions_)captions_->setCues(displayCues());
}
void MainWindow::resetSubtitleSession(bool preserveExternal){
    if(!preserveExternal)preparedApplied_=false;
    if(!preserveExternal && subtitleFileLoader_)subtitleFileLoader_->cancel();
    cancelSubtitleWork();if(active_)active_->clearManagedSubtitles();generatedCues_.clear();
    if(!preserveExternal){externalCues_.clear();readings_.clear();translations_.clear();}
    presentationCues_.clear();presentationDirty_=true;coverage_.reset();pendingSeek_=-1;bufferBypass_=false;
    if(preserveExternal)rebuildManagedCaptions();else if(captions_)captions_->setCues({});
}
QVector<SubtitleCue> MainWindow::originalCues()const{return !externalCues_.isEmpty()?externalCues_:aiEnabled_->isChecked()?generatedCues_:QVector<SubtitleCue>{};}
QVector<SubtitleCue> MainWindow::displayCues()const{
    QVector<SubtitleCue> result;for(const auto &source:presentationCues_){
        QStringList lines;if(originalEnabled_->isChecked())lines.append(source.text);
        if(readingEnabled_->isChecked() && !readings_.value(source.text).isEmpty())lines.append(readings_.value(source.text));
        if(translationEnabled_->isChecked()){const auto korean=translations_.value(translationCueKey(source));if(!korean.isEmpty())lines.append(korean);}
        if(!lines.isEmpty())result.append({source.startMs,source.endMs,lines.join('\n')});
    }return result;
}
void MainWindow::rebuildManagedCaptions(){
    if(!captions_)return;
    if(preparedApplied_ && preparedCaptions_)translations_=preparedCaptions_->translations;
    if(presentationDirty_){
        presentationCues_=captionPages(originalCues());presentationDirty_=false;
        // Reconsider future interpretations after new raw originals arrive.
        // Completed text already on screen stays stable for that entire cue.
        if(translation_ && !preparedCaptions_){
            translation_->setContextCues(originalCues());
            const auto position=active_?(pendingSeek_>=0?pendingSeek_:active_->state().positionMs):0;
            for(const auto &cue:presentationCues_){const auto key=translationCueKey(cue);if(cue.startMs>position && translations_.contains(key) && translation_->needsTranslation(cue))translations_.remove(key);}
        }
    }
    if(readingEnabled_->isChecked())for(const auto &cue:presentationCues_)if(!readings_.contains(cue.text))readings_.insert(cue.text,reading_.reading(cue.text));
    captions_->setCues(displayCues());if(active_)captions_->setPosition(active_->state().positionMs);scheduleTranslations();
}
bool MainWindow::loadCaptionFile(const QString &path){
    preparedCaptions_.reset();preparedApplied_=false;
    if(!active_)return false;
    subtitleFileLoader_->cancel();
    if(!QFileInfo::exists(path)){subtitleImportFailed("자막 파일을 찾을 수 없습니다.");return false;}
    if(QFileInfo(path).suffix().compare("srt",Qt::CaseInsensitive)==0){auto result=readSrtFile(path);if(result.valid()){applyExternalCaptions(std::move(result.cues));return true;}}
    status_->setText("자막의 형식과 문자 인코딩을 확인하는 중…");subtitleFileLoader_->load(path);return true;
}
void MainWindow::applyExternalCaptions(QVector<SubtitleCue> cues){
    aiEnabled_->setChecked(false);if(translation_)translation_->cancel();translationPending_.clear();clearTranslationSkips();readings_.clear();translations_.clear();translationFailed_=false;externalCues_=std::move(cues);presentationDirty_=true;active_->setSubtitle(-1);rebuildManagedCaptions();status_->setText(QString("자막 %1개를 불러왔습니다.").arg(externalCues_.size()));
}
void MainWindow::subtitleImportFailed(const QString &message){enableAutomaticSubtitles(language_->currentData().toString());status_->setText(message+" 자동 자막을 켰습니다.");appendLog("자막 불러오기: "+message);}
void MainWindow::toggleMute(){if(volume_->value()>0){lastUnmutedVolume_=volume_->value();volume_->setValue(0);}else volume_->setValue(std::clamp(lastUnmutedVolume_,1,100));}
bool MainWindow::eventFilter(QObject *object,QEvent *event){
    if(event->type()!=QEvent::KeyPress || QApplication::activePopupWidget() || QApplication::activeModalWidget())return QMainWindow::eventFilter(object,event);
    auto *target=qobject_cast<QWidget*>(object);if(!target)return QMainWindow::eventFilter(object,event);
    bool belongs=false;for(auto *owner=target;owner;owner=owner->parentWidget())if(owner==this){belongs=true;break;}if(!belongs)return false;
    auto *key=static_cast<QKeyEvent*>(event);const auto modifiers=key->modifiers() & ~Qt::KeypadModifier;const auto code=key->key();
    if(modifiers==Qt::NoModifier && (code==Qt::Key_F11 || (code==Qt::Key_Escape && full_))){toggleFullScreen();if(fullscreenControls_)fullscreenControls_->activity();return true;}
    if(auto *focused=QApplication::focusWidget();focused && focused->window()==target->window())target=focused;
    for(auto *owner=target;owner && owner!=this;owner=owner->parentWidget())if(qobject_cast<QLineEdit*>(owner) || qobject_cast<QTextEdit*>(owner) || qobject_cast<QPlainTextEdit*>(owner) || qobject_cast<QComboBox*>(owner) || qobject_cast<QAbstractSpinBox*>(owner) || qobject_cast<QAbstractScrollArea*>(owner) || qobject_cast<QTabBar*>(owner) || qobject_cast<QSlider*>(owner))return false;
    if(modifiers==Qt::ControlModifier){
        if(code==Qt::Key_O)findChild<QPushButton*>("openMediaButton")->click();
        else if(code==Qt::Key_D)discButton_->click();
        else if(active_==discEngine_.get()){
            std::optional<DiscAction> action;
            if(code==Qt::Key_Up)action=DiscAction::Up;else if(code==Qt::Key_Down)action=DiscAction::Down;else if(code==Qt::Key_Left)action=DiscAction::Left;else if(code==Qt::Key_Right)action=DiscAction::Right;else if(code==Qt::Key_Return || code==Qt::Key_Enter)action=DiscAction::Activate;else return false;
            active_->navigate(discActionCode(*action));
        }else return false;
    }else if(modifiers==Qt::ShiftModifier && (code==Qt::Key_Left || code==Qt::Key_Right)){if(active_)seekTo(active_->state().positionMs+(code==Qt::Key_Left?-10000:10000));}
    else if(modifiers!=Qt::NoModifier)return false;
    else if(code==Qt::Key_F || code==Qt::Key_Return || code==Qt::Key_Enter)toggleFullScreen();
    else if(code==Qt::Key_Left || code==Qt::Key_Right){if(active_)seekTo(active_->state().positionMs+(code==Qt::Key_Left?-10000:10000));}
    else if(code==Qt::Key_Up || code==Qt::Key_Down)volume_->setValue(std::clamp(volume_->value()+(code==Qt::Key_Up?5:-5),0,100));
    else if(code==Qt::Key_M)toggleMute();else if(code==Qt::Key_Space){if(qobject_cast<QAbstractButton*>(target))return false;togglePlayback();}else return false;
    if(fullscreenControls_)fullscreenControls_->activity();return true;
}
void MainWindow::scheduleSubtitles(){
    if(!liveCaptioningActive() || !active_ || videoRecoveryExhausted_)return;
    updateCaptionBuffer();
    if(job_.busy() || pendingAudio_!=-2 || pendingChapter_!=-2 || pendingTitle_!=-2)return;
    if(!captionTimelineAvailable())return;const auto &state=active_->state();
    holdForCaptionBuffer();updateCaptionBuffer();
    if(aiFailed_){if(nextAiAttemptMs_<=0 || QDateTime::currentMSecsSinceEpoch()<nextAiAttemptMs_)return;aiFailed_=false;nextAiAttemptMs_=0;}
    const auto position=pendingSeek_>=0?pendingSeek_:state.positionMs;auto chunk=coverage_.begin(position,state.durationMs,active_==discEngine_.get()?2000:0);
    if(!chunk)return;
    RecognitionRequest request;request.source=source_;request.disc=active_==discEngine_.get();request.title=std::max(1,state.currentTitle);request.language=language_->currentData().toString();request.startMs=*chunk;request.durationMs=coverage_.pendingDuration();request.audioTrackId=state.currentAudio;
    if(request.disc)request.playlist=matchDiscPlaylist(discInfo_,state.durationMs);
    int index=0;for(const auto &track:state.audioTracks){if(track.id<0)continue;if(track.id==state.currentAudio){request.audioIndex=index;break;}++index;}
    job_.start(request,model_);
}
void MainWindow::scheduleTranslations(){
    if(preparedCaptions_ || !translation_ || !translationEnabled_->isChecked() || translationFailed_ || videoRecoveryExhausted_)return;
    translation_->setContextCues(originalCues());
    const qint64 position=active_?(pendingSeek_>=0?pendingSeek_:active_->state().positionMs):0;
    auto cues=presentationCues_;std::stable_sort(cues.begin(),cues.end(),[position](const SubtitleCue &a,const SubtitleCue &b){return std::abs(a.startMs-position)<std::abs(b.startMs-position);});
    QVector<SubtitleCue> work;for(const auto &cue:cues){if(work.size()>=16)break;const auto key=translationCueKey(cue);if(cue.endMs<position-5000 || cue.startMs>position+90000 || cue.text.isEmpty() || translations_.contains(key) || translationPending_.contains(key) || translationSkipped_.contains(key))continue;work.append(cue);translationPending_.insert(key);}
    if(!work.isEmpty())translation_->requestCues(work);
}
void MainWindow::clearTranslationSkips(){translationSkipped_.clear();translationSkippedOrder_.clear();}
QJsonObject MainWindow::diagnostics()const{
    QJsonObject result{{"version",LUMA_VERSION},{"playingSamples",decodedSamples_},{"discRuntime",discEngine_->available()},{"fileRuntime",fileEngine_->available()},{"aiCueCount",generatedCues_.size()},{"aiBackend",job_.selectedBackend()},{"aiEnabled",aiEnabled_->isChecked()},{"aiRetryCount",aiRetryCount_},{"aiRetryAt",nextAiAttemptMs_},{"captionBufferHolding",bufferHolding_},{"captionResumeRequested",bufferResume_},{"captionBufferBypass",bufferBypass_},{"hangulReadings",readingEnabled_->isChecked()},{"readingRuntime",reading_.available()},{"originalCaptions",originalEnabled_->isChecked()},{"koreanTranslations",translationEnabled_->isChecked()},{"translationCount",translations_.size()},{"translationBackend",translation_?translation_->selectedBackend():QString{}},{"bundledFonts",qApp->property("lumaFontsLoaded").toBool()},{"externalCueCount",externalCues_.size()},{"theme",playerUsesDarkTheme()?"dark":"light"},{"fullscreen",full_},{"transportVisible",transport_->isVisible()},{"transportHeight",transport_->height()},{"developerMode",developerApproved_},{"captureAllowed",captureApproved_},{"videoRecoveryCount",videoRecoveryCount_},{"videoRecoveryExhausted",videoRecoveryExhausted_}};
    result.insert("translationSkippedCount",translationSkipped_.size());result.insert("translationPendingCount",translationPending_.size());result.insert("translationFailed",translationFailed_);
    result.insert("preparedCaptionsApplied",preparedApplied_);result.insert("preparedCueCount",preparedCaptions_?preparedCaptions_->originals.size():0);result.insert("preparedTranslationCount",preparedCaptions_?preparedCaptions_->translations.size():0);
    if(preparationDialog_)result.insert("preparation",preparationDialog_->diagnostics());
    result.insert("aiTiming",job_.selectedAlignment());
    const auto request=job_.currentRequest();
    if(active_)result.insert("navigationTitle",active_->state().navigationTitle);
    if(active_)result.insert("discMenus",active_->state().discMenus);
    result.insert("pendingAudioTrack",pendingAudio_);result.insert("pendingChapter",pendingChapter_);result.insert("pendingTitle",pendingTitle_);
    result.insert("jobBusy",job_.busy());result.insert("requestedAudioTrack",request.audioTrackId);result.insert("requestedAudioIndex",request.audioIndex);result.insert("requestedPlaying",wantsPlayback());
    result.insert("asrModel",QFileInfo(model_).fileName());result.insert("requestedCaptionStartMs",request.startMs);result.insert("requestedCaptionDurationMs",request.durationMs);result.insert("resumePending",pendingResume_.has_value());
    if(active_)result.insert("timelineReady",active_->state().timelineReady);
    result.insert("discRoot",discInfo_.root);result.insert("discValid",discInfo_.valid);result.insert("discProtected",discInfo_.aacs || discInfo_.bdplus);
    if(active_)result.insert("currentAudioTrack",active_->state().currentAudio);
    if(captureGuard_){result.insert("captureRequested",captureGuard_->enabled());result.insert("captureApplied",captureGuard_->applied());result.insert("captureStatus",captureGuard_->status());}result.insert("display",displayCapabilities(winId()));
    if(active_){const auto &state=active_->state();result.insert("engine",active_->name());result.insert("positionMs",state.positionMs);result.insert("durationMs",state.durationMs);result.insert("playing",state.playing);result.insert("failed",state.failed);result.insert("title",state.currentTitle);result.insert("chapter",state.currentChapter);result.insert("chapterCount",state.chapters.size());result.insert("audioTrackCount",state.audioTracks.size());result.insert("videoCodec",state.videoCodec);result.insert("hardwareDecoder",state.hardwareDecoder);result.insert("audioCodec",state.audioCodec);result.insert("decodedVideo",state.decodedVideo);result.insert("decodedAudio",state.decodedAudio);result.insert("displayedPictures",state.displayedPictures);result.insert("playedAudioBuffers",state.playedAudioBuffers);result.insert("captionBufferedAheadMs",coverage_.bufferedAhead(state.positionMs,state.durationMs));result.insert("sourceFps",state.sourceFps);result.insert("filteredFps",state.filteredFps);result.insert("displayFps",state.displayFps);result.insert("motionStatus",state.motionStatus);}
    QJsonArray messages;const auto lines=log_->toPlainText().split('\n');for(const auto &line:lines.mid(std::max<qsizetype>(0,lines.size()-30)))messages.append(line);result.insert("recentMessages",messages);return result;
}
void MainWindow::dragEnterEvent(QDragEnterEvent *event){if(event->mimeData()->hasUrls())event->acceptProposedAction();}
void MainWindow::openDroppedUrls(const QList<QUrl> &urls){if(urls.isEmpty())return;const auto path=urls.first().isLocalFile()?urls.first().toLocalFile():urls.first().toString();const auto suffix=QFileInfo(path).suffix().toLower();if(QStringList{"srt","ass","ssa","vtt","smi","sami"}.contains(suffix))loadCaptionFile(path);else openSource(path);}
void MainWindow::dropEvent(QDropEvent *event){if(event->mimeData()->hasUrls()){openDroppedUrls(event->mimeData()->urls());event->acceptProposedAction();}}
}
