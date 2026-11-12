#include "main_window.h"

#include "caption_overlay.h"
#include "video_surface.h"
#include "player_controls.h"
#include "theme.h"
#include "media/platform_features.h"
#include "subtitles/subtitle_file.h"
#include "subtitles/translation_service.h"
#ifdef Q_OS_WIN
#include "ai_acceleration_setup.h"
#include "update_manager.h"
#endif
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QUrl>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace luma {

namespace {
QLabel *label(const QString &text,const char *name=nullptr){
    auto *widget=new QLabel;
    widget->setTextFormat(Qt::PlainText);
    widget->setText(text);
    if(name)widget->setObjectName(name);
    return widget;
}
QPushButton *button(const QString &text,const char *name=nullptr){auto *widget=new QPushButton(text);widget->setCursor(Qt::PointingHandCursor);if(name)widget->setObjectName(name);return widget;}
void choice(QVBoxLayout *layout,const QString &name,QComboBox *&box){layout->addWidget(label(name,"section"));box=new ChoiceBox;layout->addWidget(box);}
QScrollArea *scrollPanel(QWidget *contents){auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setWidget(contents);return scroll;}
void setSavedChoice(QComboBox *box,const QString &key,const QString &fallback={}){const auto saved=QSettings().value(key,fallback).toString();for(int i=0;i<box->count();++i)if(box->itemData(i).toString()==saved){box->setCurrentIndex(i);break;}}
void addCaptionAppearance(QVBoxLayout *settingsLayout,CaptionOverlay *overlay){
    const CaptionStyle defaults;
    QSettings settings;
    settings.beginGroup("subtitles/appearance");
    settingsLayout->addWidget(label("자막 모양","section"));
    auto *form=new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    settingsLayout->addLayout(form);

    auto *font=new ChoiceBox;
    font->setObjectName("captionFont");
    font->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    font->setMinimumContentsLength(8);
    font->addItem("기본 · Noto Sans",QString{});
    for(const auto &family:QFontDatabase::families())
        font->addItem(family,family);
    font->setCurrentIndex(std::max(0,font->findData(settings.value("fontFamily",defaults.fontFamily))));
    form->addRow("글꼴",font);

    auto addPercent=[&](const QString &name,const char *objectName,const char *key,int minimum,int maximum,int fallback){
        auto *control=new QSpinBox;
        control->setObjectName(objectName);
        control->setRange(minimum,maximum);
        control->setSuffix("%");
        control->setSingleStep(5);
        control->setKeyboardTracking(false);
        control->setValue(settings.value(key,fallback).toInt());
        form->addRow(name,control);
        return control;
    };
    auto *size=addPercent("크기","captionSize","sizePercent",50,200,defaults.sizePercent);
    auto *text=addPercent("글자 불투명도","captionTextOpacity","textOpacity",0,100,defaults.textOpacity);
    auto *background=addPercent("배경 불투명도","captionBackgroundOpacity","backgroundOpacity",0,100,defaults.backgroundOpacity);
    text->setToolTip("0%는 투명, 100%는 불투명입니다.");
    background->setToolTip(text->toolTip());

    const auto currentStyle=[=]{
        return CaptionStyle{font->currentData().toString(),size->value(),text->value(),background->value()};
    };
    overlay->setCaptionStyle(currentStyle());
    const auto apply=[overlay,currentStyle]{
        const auto style=currentStyle();
        overlay->setCaptionStyle(style);
        QSettings settings;
        settings.beginGroup("subtitles/appearance");
        settings.setValue("fontFamily",style.fontFamily);
        settings.setValue("sizePercent",style.sizePercent);
        settings.setValue("textOpacity",style.textOpacity);
        settings.setValue("backgroundOpacity",style.backgroundOpacity);
    };
    QObject::connect(font,&QComboBox::currentIndexChanged,overlay,apply);
    for(auto *control:{size,text,background})
        QObject::connect(control,&QSpinBox::valueChanged,overlay,apply);

    auto *reset=button("자막 모양 기본값 복원","subtle");
    settingsLayout->addWidget(reset);
    QObject::connect(reset,&QPushButton::clicked,overlay,[=]{
        const QSignalBlocker fontBlocker(font),sizeBlocker(size),textBlocker(text),backgroundBlocker(background);
        font->setCurrentIndex(0);
        size->setValue(defaults.sizePercent);
        text->setValue(defaults.textOpacity);
        background->setValue(defaults.backgroundOpacity);
        apply();
    });
}
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

void MainWindow::buildAudioSettings(QVBoxLayout *layout){
    using Profile=AudioOutputOptions::Profile;
    choice(layout,"오디오 출력",audioOutputMode_);
    audioOutputMode_->setObjectName("audioOutputMode");
    audioOutputMode_->addItem("시스템 오디오 · PCM","system");
    audioOutputMode_->addItem("HDMI 비트스트림 · AV 리시버","bitstream");
    setSavedChoice(audioOutputMode_,"audio/output","system");
    choice(layout,"파일 PCM 음장",audioProfile_);
    audioProfile_->setObjectName("audioProfile");
    audioProfile_->addItem("자동 · 장치 채널 사용",int(Profile::Auto));
    audioProfile_->addItem("스테레오",int(Profile::Stereo));
    audioProfile_->addItem("5.1 서라운드 우선",int(Profile::Surround51));
    audioProfile_->addItem("7.1 서라운드 우선",int(Profile::Surround71));
    audioProfile_->addItem("야간 모드 · 큰 소리 완화",int(Profile::Night));
    setSavedChoice(audioProfile_,"audio/profile","0");
    audioProfile_->setEnabled(audioOutputMode_->currentData()=="system");
    auto *note=label("미디어를 다시 열면 적용합니다. Atmos와 DTS:X 비트스트림은 해당 트랙과 호환 리시버가 필요합니다.","muted");
    note->setWordWrap(true);layout->addWidget(note);
#ifdef Q_OS_WIN
    auto *spatial=button("Windows 공간 음향 설정","subtle");layout->addWidget(spatial);
    spatial->setToolTip("Windows Sonic, Dolby Atmos for Headphones, DTS Headphone:X는 Windows 출력 장치에서 선택합니다.");
    connect(spatial,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl("ms-settings:sound"));});
#endif
    audioOutputStatus_=label("재생을 시작하면 실제 오디오 출력을 표시합니다.","muted");
    audioOutputStatus_->setObjectName("audioOutputStatus");audioOutputStatus_->setWordWrap(true);layout->addWidget(audioOutputStatus_);
    connect(audioOutputMode_,&QComboBox::currentIndexChanged,this,[this]{
        QSettings().setValue("audio/output",audioOutputMode_->currentData());
        audioProfile_->setEnabled(audioOutputMode_->currentData()=="system");
    });
    connect(audioProfile_,&QComboBox::currentIndexChanged,this,[this]{QSettings().setValue("audio/profile",audioProfile_->currentData());});
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
    nav->addWidget(label("YOUR COLLECTION","section"));auto *files=button("로컬 미디어");nav->addWidget(files);resumeButton_=button("이어서 보기","resumePlayback");resumeButton_->setEnabled(false);nav->addWidget(resumeButton_);connect(resumeButton_,&QPushButton::clicked,this,&MainWindow::resumeLatest);
    auto *lanPlayback=button("내부망 재생","openLanPlayback");lanPlayback->setToolTip("같은 네트워크의 PC와 Android 연결을 관리합니다.");nav->addWidget(lanPlayback);
    connect(lanPlayback,&QPushButton::clicked,this,&MainWindow::showLanPlayback);
    nav->addSpacing(22);nav->addWidget(label("연결된 디스크","section"));
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
    seek_=new SeekSlider;seek_->setObjectName("seekSlider");seek_->setAccessibleName("재생 위치");seek_->setRange(0,10000);seek_->setFixedHeight(12);transportLayout->addWidget(seek_);
    auto *buttons=new QHBoxLayout;buttons->setSpacing(6);auto *previous=new IconButton(PlayerIcon::Previous,"이전 챕터");auto *back=new IconButton(PlayerIcon::Rewind,"10초 뒤로 · Shift+Left");
    auto *play=new IconButton(PlayerIcon::Play,"재생 / 일시정지 · Space");play_=play;play->setObjectName("playButton");play->setFixedSize(38,38);
    auto *forward=new IconButton(PlayerIcon::Forward,"10초 앞으로 · Shift+Right");auto *next=new IconButton(PlayerIcon::Next,"다음 챕터");
    for(auto *control:{previous,back,play,forward,next})buttons->addWidget(control);
    clock_=label("00:00 / 00:00","muted");clock_->setMinimumWidth(115);buttons->addSpacing(5);buttons->addWidget(clock_);buttons->addStretch();
    rate_=new ChoiceBox;rate_->setObjectName("playbackRate");rate_->setAccessibleName("재생 속도");for(double speed:{.5,.75,1.,1.25,1.5,2.})rate_->addItem(QString::number(speed)+"×",speed);rate_->setCurrentIndex(2);rate_->setMinimumHeight(30);rate_->setFixedSize(70,32);buttons->addWidget(rate_);
    volumeButton_=new IconButton(PlayerIcon::Volume,"음량");volumeButton_->setFocusPolicy(Qt::NoFocus);buttons->addWidget(volumeButton_);volume_=new QSlider(Qt::Horizontal);volume_->setObjectName("volumeSlider");volume_->setAccessibleName("음량");volume_->setRange(0,100);volume_->setValue(QSettings().value("playback/volume",70).toInt());volume_->setFixedWidth(65);buttons->addWidget(volume_);
    auto *quick=button("자막");quick->setObjectName("captionQuick");quick->setFixedHeight(32);buttons->addWidget(quick);
    auto *remoteButton=button("메뉴");remoteButton->setObjectName("remoteQuick");remoteButton->setToolTip("Blu-ray 디스크 리모컨");remoteButton->setFixedHeight(32);buttons->addWidget(remoteButton);
    auto *pip=new IconButton(PlayerIcon::PictureInPicture,"작은 화면 · Ctrl+Shift+P");pip->setObjectName("pictureInPicture");pipButton_=pip;pip->setEnabled(false);buttons->addWidget(pip);
    auto *full=new IconButton(PlayerIcon::Fullscreen,"전체 화면 · F11");fullButton_=full;buttons->addWidget(full);transportLayout->addLayout(buttons);centerLayout->addWidget(transport_);
    controlsHiddenInPip_={previous,back,forward,next,rate_,volumeButton_,volume_,quick,remoteButton,full};
    status_=label("미디어를 선택하면 준비가 시작됩니다.","status");status_->setWordWrap(true);centerLayout->addWidget(status_);body->addWidget(center,1);
    captions_=new CaptionOverlay(surface_);
    panel_=new QWidget;panel_->setObjectName("settingsPanel");panel_->setFixedWidth(306);auto *panelLayout=new QVBoxLayout(panel_);panelLayout->setContentsMargins(0,0,0,0);auto *tabs=new QTabWidget;tabs->setObjectName("settingsTabs");panelLayout->addWidget(tabs);body->addWidget(panel_);
    auto *discTab=new QWidget;auto *discLayout=new QVBoxLayout(discTab);discLayout->setContentsMargins(17,18,17,16);discLayout->setSpacing(10);discLayout->addWidget(label("DISC REMOTE","section"));
    menuStatus_=label("디스크를 열면 현재 타이틀이 표시됩니다.","menuStatus");menuStatus_->setWordWrap(true);discLayout->addWidget(menuStatus_);
    auto *menus=new QHBoxLayout;auto *topMenu=button("메인 메뉴");auto *popup=button("팝업 메뉴");menus->addWidget(topMenu);menus->addWidget(popup);discLayout->addLayout(menus);
    auto *remote=new QGridLayout;remote->setSpacing(6);
    const QList<QPair<PlayerIcon,QPoint>> keys={{PlayerIcon::Up,{1,0}},{PlayerIcon::Left,{0,1}},{PlayerIcon::Confirm,{1,1}},{PlayerIcon::Right,{2,1}},{PlayerIcon::Down,{1,2}}};
    const QList<DiscAction> actions={DiscAction::Up,DiscAction::Left,DiscAction::Activate,DiscAction::Right,DiscAction::Down};const QStringList names={"위","왼쪽","확인","오른쪽","아래"};
    for(int i=0;i<keys.size();++i){auto *control=new IconButton(keys[i].first,"디스크 "+names[i]);control->setFixedSize(54,34);remote->addWidget(control,keys[i].second.y(),keys[i].second.x());connect(control,&QPushButton::clicked,this,[this,action=actions[i]]{if(!lanDiscSharing_ && active_==discEngine_.get())active_->navigate(discActionCode(action));});}
    discLayout->addLayout(remote);
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
    auto *translationGlossary=button("번역 용어집","subtle");aiLayout->addWidget(translationGlossary);
    choice(aiLayout,"번역 가속",translationMode_);translationMode_->setObjectName("translationMode");translationMode_->addItem("자동 · 사용 가능한 가속","auto");translationMode_->addItem("GPU 우선","gpu");translationMode_->addItem("CPU","cpu");translationMode_->addItem("NPU · 호환 모델/런타임 필요","npu");setSavedChoice(translationMode_,"translation/mode","auto");
    choice(aiLayout,"번역 GPU",translationGpu_);translationGpu_->setObjectName("translationGpu");translationGpu_->addItem("자동 GPU 선택",QString{});const auto savedTranslationGpu=QSettings().value("translation/gpu").toString();if(!savedTranslationGpu.isEmpty()){translationGpu_->addItem("저장된 GPU · "+savedTranslationGpu,savedTranslationGpu);translationGpu_->setCurrentIndex(1);}
    auto *translationNote=label("음성 인식과 별도로 선택합니다. 실제 사용 장치는 위에 표시됩니다.","muted");translationNote->setWordWrap(true);aiLayout->addWidget(translationNote);
    aiStatus_=label("AI 자동자막 꺼짐","muted");aiStatus_->setWordWrap(true);aiLayout->addWidget(aiStatus_);auto *note=label("독음·번역은 원문과 별도로 보관됩니다. 노래나 배경음이 강하면 인식 정확도가 낮아질 수 있습니다.","muted");note->setWordWrap(true);aiLayout->addWidget(note);auto *save=button("표시 자막 저장","primary");aiLayout->addWidget(save);aiLayout->addStretch();tabs->addTab(scrollPanel(aiTab),"AI 자막");
    auto *settingsTab=new QWidget;auto *settingsLayout=new QVBoxLayout(settingsTab);settingsLayout->setContentsMargins(17,18,17,18);settingsLayout->setSpacing(12);
    addCaptionAppearance(settingsLayout,captions_);
    settingsLayout->addWidget(label("PLAYBACK & PRIVACY","section"));
    choice(settingsLayout,"파일 HDR 출력",hdrOutputMode_);hdrOutputMode_->setObjectName("hdrOutputMode");
    hdrOutputMode_->addItem("자동 · HDR 화면에 맞춤","auto");hdrOutputMode_->addItem("끔 · SDR 출력","sdr");
    setSavedChoice(hdrOutputMode_,"video/hdrOutput","auto");
    hdrPlaybackStatus_=label("파일을 열면 영상 출력 형식을 표시합니다.","muted");
    hdrPlaybackStatus_->setObjectName("hdrPlaybackStatus");hdrPlaybackStatus_->setWordWrap(true);settingsLayout->addWidget(hdrPlaybackStatus_);
    connect(hdrOutputMode_,&QComboBox::currentIndexChanged,this,[this]{
        QSettings().setValue("video/hdrOutput",hdrOutputMode_->currentData());applyVideoOutputPreferences();
    });
    choice(settingsLayout,"파일 영상 GPU",videoGpu_);videoGpu_->setObjectName("videoGpu");videoGpu_->addItem("자동 선택",QString{});for(const auto &entry:videoAdapters()){const auto adapter=entry.toObject();videoGpu_->addItem(adapter.value("name").toString(),adapter.value("id").toString());}setSavedChoice(videoGpu_,"video/adapter");
    auto *videoNote=label("파일 재생에 적용됩니다. 재생 중 변경하면 현재 위치에서 다시 연결합니다.","muted");videoNote->setWordWrap(true);settingsLayout->addWidget(videoNote);
    choice(settingsLayout,"프레임 보간",motionMode_);motionMode_->setObjectName("motionMode");motionMode_->addItem("꺼짐","off");motionMode_->addItem("부드러운 표시 · GPU 블렌딩","display");motionMode_->addItem("모션 보간 · 실험적 CPU 처리","optical");setSavedChoice(motionMode_,"motion/mode","off");
    choice(settingsLayout,"모션 보간 목표 FPS",motionFps_);motionFps_->setObjectName("motionFps");for(int fps:{60,90,120,144,165,240,360})motionFps_->addItem(QString::number(fps)+" fps",fps);setSavedChoice(motionFps_,"motion/fps","120");
    motionStatus_=label("파일 영상에서 사용할 수 있습니다.","muted");motionStatus_->setObjectName("motionStatus");motionStatus_->setWordWrap(true);settingsLayout->addWidget(motionStatus_);
    auto *motionNote=label("파일 전용 · 디스크 메뉴에는 적용하지 않습니다.\nCPU 모션 보간은 부하가 높습니다. 목표 FPS와 실제 처리·출력 FPS는 다를 수 있습니다.","muted");motionNote->setWordWrap(true);settingsLayout->addWidget(motionNote);
    displayStatus_=label("디스플레이 정보 확인 중…","displayStatus");displayStatus_->setObjectName("displayStatus");displayStatus_->setWordWrap(true);settingsLayout->addWidget(displayStatus_);
#ifdef Q_OS_WIN
    auto *displaySettings=button("Windows 디스플레이 설정","subtle");settingsLayout->addWidget(displaySettings);
    connect(displaySettings,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl("ms-settings:display"));});
#endif
    buildAudioSettings(settingsLayout);
#ifdef Q_OS_WIN
    auto *aiAcceleration=button("AI 가속 설정","aiAccelerationSettings");
    settingsLayout->addWidget(aiAcceleration);
    connect(aiAcceleration,&QPushButton::clicked,this,[this]{showAiAccelerationSetup(this);});
    auto *updates=new UpdateManager(this);
    updates->addControls(settingsLayout);
#endif
    captureStatus_=label("보호 디스크를 재생하면 캡처 차단을 적용합니다.","captureStatus");captureStatus_->setWordWrap(true);settingsLayout->addWidget(captureStatus_);
    developerEnabled_=new QCheckBox("개발자 모드");developerEnabled_->setObjectName("developerMode");
    developerEnabled_->setChecked(QSettings().value("ui/developerMode",false).toBool() && QSettings().value("privacy/developerConsentVersion",0).toInt()==privacyConsentVersion);settingsLayout->addWidget(developerEnabled_);
    developerApproved_=developerEnabled_->isChecked();
    auto *developerPanel=new QWidget;developerPanel->setObjectName("developerDiagnostics");auto *developerLayout=new QVBoxLayout(developerPanel);developerLayout->setContentsMargins(0,0,0,0);
    captureAllowed_=new QCheckBox("개발용 영상 캡처 허용");captureAllowed_->setObjectName("captureAllowed");captureAllowed_->setToolTip("캡처 차단을 해제하면 Parsec과 화면 캡처에 영상·자막이 보일 수 있습니다.");
    captureAllowed_->setChecked(developerEnabled_->isChecked() && QSettings().value("privacy/captureAllowed",false).toBool() && QSettings().value("privacy/captureConsentVersion",0).toInt()==privacyConsentVersion);developerLayout->addWidget(captureAllowed_);
    captureAllowed_->hide();
    captureApproved_=captureAllowed_->isChecked();
    if(!captureAllowed_->isChecked()){QSettings().setValue("privacy/captureAllowed",false);QSettings().setValue("privacy/captureConsentVersion",0);}
    buildDeveloperTools(developerLayout);
    developerLayout->addWidget(label("PLAYBACK LOG","section"));log_=new QPlainTextEdit;log_->setReadOnly(true);log_->setMaximumBlockCount(500);log_->setMinimumHeight(170);developerLayout->addWidget(log_);developerPanel->setVisible(developerEnabled_->isChecked());codec_->setVisible(developerEnabled_->isChecked());settingsLayout->addWidget(developerPanel,1);settingsLayout->addStretch();
    const int settingsIndex=tabs->addTab(scrollPanel(settingsTab),"설정");
    connect(tabs,&QTabWidget::tabBarClicked,this,[this,settingsIndex,clicks=0,window=QElapsedTimer{}](int index) mutable {
        if(index!=settingsIndex || !developerApproved_ || !captureAllowed_->isHidden()){
            clicks=0;
            window.invalidate();
            return;
        }
        if(!window.isValid() || window.hasExpired(4000)){
            clicks=0;
            window.start();
        }
        if(++clicks==5){
            captureAllowed_->show();
            clicks=0;
            window.invalidate();
        }
    });
    connect(developerEnabled_,&QCheckBox::toggled,this,[this,developerPanel](bool enabled){
        if(enabled && !confirmDeveloperUse(this,false)){QSignalBlocker guard(developerEnabled_);developerEnabled_->setChecked(false);enabled=false;}
        developerApproved_=enabled;
        QSettings().setValue("ui/developerMode",enabled);QSettings().setValue("privacy/developerConsentVersion",enabled?privacyConsentVersion:0);
        if(!enabled){captureAllowed_->hide();captureAllowed_->setChecked(false);performanceEnabled_->setChecked(false);}
        setDeveloperDiagnosticCollection(enabled);
        developerPanel->setVisible(enabled);codec_->setVisible(enabled);applyCapturePolicy();applyVideoOutputPreferences();
    });
    connect(captureAllowed_,&QCheckBox::toggled,this,[this](bool allowed){
        if(allowed && (!developerEnabled_->isChecked() || !confirmDeveloperUse(this,true))){QSignalBlocker guard(captureAllowed_);captureAllowed_->setChecked(false);allowed=false;}
        captureApproved_=allowed;
        QSettings().setValue("privacy/captureAllowed",allowed);QSettings().setValue("privacy/captureConsentVersion",allowed?privacyConsentVersion:0);applyCapturePolicy();
    });
    auto *captionMenu=new QMenu(quick);quick->setMenu(captionMenu);
    for(auto *check:{originalEnabled_,readingEnabled_,translationEnabled_,aiEnabled_}){auto *action=captionMenu->addAction(check->text());action->setCheckable(true);action->setChecked(check->isChecked());action->setEnabled(check->isEnabled());connect(action,&QAction::toggled,check,&QCheckBox::setChecked);connect(check,&QCheckBox::toggled,action,&QAction::setChecked);}
    captionMenu->addSeparator();
    captionMenu->addAction("번역 용어집",this,&MainWindow::editTranslationGlossary);
    auto *remoteMenu=new QMenu(remoteButton);remoteButton->setMenu(remoteMenu);auto *remoteHint=remoteMenu->addAction("디스크 조작: 마우스 / Ctrl+방향키 / Ctrl+Enter");remoteHint->setEnabled(false);remoteMenu->addSeparator();
    remoteMenu->addAction("메인 메뉴",topMenu,&QPushButton::click);remoteMenu->addAction("팝업 메뉴",popup,&QPushButton::click);remoteMenu->addSeparator();
    for(int i=0;i<actions.size();++i)remoteMenu->addAction(names[i],this,[this,action=actions[i]]{if(!lanDiscSharing_ && active_==discEngine_.get())active_->navigate(discActionCode(action));});
    connect(themeButton_,&QPushButton::clicked,this,[this]{setPlayerDarkTheme(!playerUsesDarkTheme());applyTheme();});
    connect(open,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"미디어 열기",{},"미디어 (*.mkv *.mp4 *.webm *.mov *.avi *.m2ts *.ts *.flac *.alac *.m4a *.mp3 *.wav);;모든 파일 (*)");if(!path.isEmpty())openSource(path);});connect(files,&QPushButton::clicked,open,&QPushButton::click);
    connect(rescan,&QPushButton::clicked,this,&MainWindow::discoverDisc);connect(discButton_,&QPushButton::clicked,this,&MainWindow::playConnectedDisc);
    connect(play_,&QPushButton::clicked,this,&MainWindow::togglePlayback);
    connect(back,&QPushButton::clicked,this,[this]{if(active_)seekTo(active_->state().positionMs-10000);});connect(forward,&QPushButton::clicked,this,[this]{if(active_)seekTo(active_->state().positionMs+10000);});
    connect(previous,&QPushButton::clicked,this,[this]{if(active_)selectChapter(std::max(0,active_->state().currentChapter-1));});connect(next,&QPushButton::clicked,this,[this]{if(active_)selectChapter(active_->state().currentChapter+1);});
    connect(seek_,&QSlider::sliderReleased,this,[this]{if(active_)seekTo(active_->state().durationMs*seek_->value()/10000);});
    connect(volume_,&QSlider::valueChanged,this,[this](int volume){QSettings().setValue("playback/volume",volume);if(volume>0){lastUnmutedVolume_=volume;QSettings().setValue("playback/lastUnmutedVolume",volume);}volumeButton_->setToolTip(volume?"음소거 · M":"음소거 해제 · M");if(active_)active_->setVolume(volume);});
    connect(volumeButton_,&QPushButton::clicked,this,&MainWindow::toggleMute);
    connect(rate_,&QComboBox::activated,this,[this]{if(active_)active_->setRate(rate_->currentData().toDouble());});connect(full,&QPushButton::clicked,this,&MainWindow::toggleFullScreen);
    connect(pip,&QPushButton::clicked,this,&MainWindow::togglePictureInPicture);
    connect(topMenu,&QPushButton::clicked,this,[this]{if(active_==discEngine_.get()){selectTitle(0);surface_->setFocus();}});
    connect(popup,&QPushButton::clicked,this,[this]{if(!lanDiscSharing_ && active_==discEngine_.get()){active_->navigate(discActionCode(DiscAction::Popup));surface_->setFocus();}});
    connect(titles_,&QComboBox::activated,this,[this]{selectTitle(titles_->currentData().toInt());});
    connect(audio_,&QComboBox::activated,this,[this]{selectAudio(audio_->currentData().toInt());});
    connect(subs_,&QComboBox::activated,this,[this]{if(active_){subtitleFileLoader_->cancel();aiEnabled_->setChecked(false);externalCues_.clear();presentationDirty_=true;rebuildManagedCaptions();active_->setSubtitle(subs_->currentData().toInt());}});
    connect(chapters_,&QListWidget::itemActivated,this,[this](QListWidgetItem *item){selectChapter(item->data(Qt::UserRole).toInt());});
    connect(external,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"자막 열기",{},"텍스트 자막 (*.srt *.ass *.ssa *.vtt *.smi *.sami)");if(!path.isEmpty())loadCaptionFile(path);});
    connect(model,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"Whisper 모델",{},"Whisper 모델 (*.bin)");if(!path.isEmpty()){resetSubtitleSession(true);model_=path;QSettings().setValue("ai/model",path);updateNpuStatus();aiStatus_->setText("음성 인식 모델 선택됨");if(liveCaptioningActive())job_.warmUp(model_);scheduleSubtitles();}});
    connect(translationModel,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"로컬 번역 모델",{},"GGUF 모델 (*.gguf)");if(!path.isEmpty()){translation_->cancel();translationPending_.clear();clearTranslationSkips();if(!preparedCaptions_)translations_.clear();translationFailed_=false;QSettings().setValue("translation/model",path);translation_->setModel(path);rebuildManagedCaptions();scheduleTranslations();if(preparedCaptions_)translationStatus_->setText("선택한 모델은 다음 자막 생성부터 사용합니다.");}});
    connect(translationGlossary,&QPushButton::clicked,this,&MainWindow::editTranslationGlossary);
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

}
