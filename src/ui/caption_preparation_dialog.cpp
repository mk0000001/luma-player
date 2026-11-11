#include "caption_preparation_dialog.h"
#include "core/disc.h"
#include "subtitles/subtitle_job.h"
#include "subtitles/translation_support.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <algorithm>
namespace luma {
namespace {
QPushButton *action(const QString &text,const char *name){auto *button=new QPushButton(text);button->setObjectName(name);button->setCursor(Qt::PointingHandCursor);return button;}
}
CaptionPreparationDialog::CaptionPreparationDialog(CaptionPreparationRequest defaults,QString discRoot,QString initialFile,QWidget *parent,QString runtime)
    :QDialog(parent),request_(std::move(defaults)),service_(runtime,this),probe_(runtime,this),reading_(runtime),runtime_(std::move(runtime)),discRoot_(std::move(discRoot)){
    setWindowTitle("재생 전 자막 만들기");setObjectName("captionPreparationDialog");setModal(true);resize(760,790);setMinimumWidth(660);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(26,22,26,22);layout->setSpacing(12);
    auto *heading=new QLabel("자막을 먼저, 감상은 그다음.");heading->setObjectName("nowTitle");layout->addWidget(heading);
    auto *intro=new QLabel("영상 재생 없이 전체 원문을 먼저 인식합니다. 그다음 앞뒤 원문을 참고해 한국어 해석을 준비합니다.");intro->setWordWrap(true);intro->setObjectName("muted");layout->addWidget(intro);
    auto *sourceRow=new QHBoxLayout;chooseFile_=action("파일 선택","prepareChooseFile");chooseDisc_=action("연결된 Blu-ray","prepareChooseDisc");sourceRow->addWidget(chooseFile_);sourceRow->addWidget(chooseDisc_);sourceRow->addStretch();layout->addLayout(sourceRow);
    sourceLabel_=new QLabel("파일 또는 Blu-ray 본편을 선택하세요.");sourceLabel_->setObjectName("prepareSource");sourceLabel_->setWordWrap(true);sourceLabel_->setTextFormat(Qt::PlainText);layout->addWidget(sourceLabel_);
    auto *form=new QFormLayout;form_=form;form->setSpacing(10);
    playlists_=new QComboBox;playlists_->setObjectName("preparePlaylists");form->addRow("Blu-ray 타이틀",playlists_);
    audio_=new QComboBox;audio_->setObjectName("prepareFileAudio");form->addRow("파일 오디오",audio_);
    discAudio_=new QSpinBox;discAudio_->setObjectName("prepareDiscAudio");discAudio_->setRange(1,64);discAudio_->setValue(1);discAudio_->setSuffix("번 오디오");discAudio_->setToolTip("디스크 오디오 순서입니다. 기본값은 첫 번째 오디오입니다.");form->addRow("디스크 오디오",discAudio_);
    durationLabel_=new QLabel("길이 확인 대기");durationLabel_->setObjectName("prepareDuration");form->addRow("준비할 전체 길이",durationLabel_);
    auto *models=new QLabel(QString("원문: %1\n한국어: %2").arg(QFileInfo(request_.model).fileName(),QFileInfo(request_.translationModel).fileName()));models->setTextFormat(Qt::PlainText);models->setWordWrap(true);models->setObjectName("muted");form->addRow("선택한 모델",models);
    auto *outputRow=new QHBoxLayout;output_=new QLineEdit;output_->setObjectName("prepareOutputDirectory");output_->setPlaceholderText("자막을 저장할 폴더");browseOutput_=action("폴더 선택","prepareChooseOutput");outputRow->addWidget(output_,1);outputRow->addWidget(browseOutput_);form->addRow("저장 위치",outputRow);layout->addLayout(form);
    translate_=new QCheckBox("한국어 해석 함께 만들기");translate_->setObjectName("prepareKorean");translate_->setChecked(request_.translate);layout->addWidget(translate_);
    readingEnabled_=new QCheckBox("일본어 한글 독음도 표시 자막에 저장");readingEnabled_->setObjectName("prepareReading");readingEnabled_->setChecked(request_.source.language=="ja" && reading_.available());readingEnabled_->setEnabled(request_.source.language=="ja" && reading_.available());layout->addWidget(readingEnabled_);
    auto *note=new QLabel("원문·한국어 SRT와 다시 열 수 있는 자막 프로젝트를 저장합니다. 생성 중에는 영상 재생을 멈추며, 취소 후 같은 폴더에서 다시 시작하면 완료 구간을 재사용합니다.");note->setWordWrap(true);note->setObjectName("muted");layout->addWidget(note);
    bar_=new QProgressBar;bar_->setObjectName("preparationProgress");bar_->setRange(0,1000);bar_->setValue(0);bar_->setFormat("%p%");layout->addWidget(bar_);
    status_=new QLabel("선택 후 ‘전체 자막 만들기’를 누르세요.");status_->setObjectName("preparationStatus");status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);layout->addWidget(status_);
    backend_=new QLabel;backend_->setObjectName("muted");backend_->setWordWrap(true);layout->addWidget(backend_);layout->addStretch();
    auto *buttons=new QHBoxLayout;cancel_=action("닫기","prepareCancel");start_=action("전체 자막 만들기","prepareStart");retryAccelerator_=action("가속 장치 다시 시도","prepareRetryAccelerator");retryAccelerator_->setEnabled(false);retryAccelerator_->hide();play_=action("준비한 자막으로 재생","preparePlay");play_->setProperty("primary",true);play_->setEnabled(false);buttons->addWidget(cancel_);buttons->addStretch();buttons->addWidget(retryAccelerator_);buttons->addWidget(start_);buttons->addWidget(play_);layout->addLayout(buttons);
    connect(chooseFile_,&QPushButton::clicked,this,[this]{const auto file=QFileDialog::getOpenFileName(this,"자막을 만들 미디어",{},"미디어 파일 (*.*)");if(!file.isEmpty())setFile(file);});
    connect(chooseDisc_,&QPushButton::clicked,this,[this]{setDisc(discRoot_);});
    connect(browseOutput_,&QPushButton::clicked,this,[this]{const auto directory=QFileDialog::getExistingDirectory(this,"자막 저장 폴더",output_->text());if(!directory.isEmpty())output_->setText(directory);});
    connect(output_,&QLineEdit::textChanged,this,[this]{invalidateResult();updateStart();});connect(output_,&QLineEdit::textEdited,this,[this]{customOutput_=true;});
    connect(translate_,&QCheckBox::toggled,this,[this]{invalidateResult();if(!customOutput_)setDefaultOutput();updateStart();});
    connect(audio_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){request_.source.audioIndex=std::max(0,index);invalidateResult();if(!customOutput_)setDefaultOutput();});
    connect(discAudio_,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){if(request_.source.disc){request_.source.audioIndex=value-1;invalidateResult();if(!customOutput_)setDefaultOutput();}});
    connect(playlists_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(index<0)return;invalidateResult();request_.source.playlist=playlists_->itemData(index).toString();request_.durationMs=playlists_->itemData(index,Qt::UserRole+1).toLongLong();durationLabel_->setText(formatTime(request_.durationMs));if(!customOutput_)setDefaultOutput();updateStart();});
    connect(start_,&QPushButton::clicked,this,&CaptionPreparationDialog::startPreparation);
    connect(retryAccelerator_,&QPushButton::clicked,this,&CaptionPreparationDialog::retryAccelerator);
    connect(cancel_,&QPushButton::clicked,this,[this]{if(busy_){retryingAccelerator_=false;service_.cancel();}else reject();});
    connect(play_,&QPushButton::clicked,this,[this]{if(result_)accept();});
    connect(&probe_,&PreparationMediaProbe::ready,this,[this](quint64 generation,const PreparationMediaInfo &info){if(generation!=probeGeneration_)return;probing_=false;request_.durationMs=info.durationMs;durationLabel_->setText(formatTime(info.durationMs));audio_->clear();for(int i=0;i<info.audioTracks.size();++i)audio_->addItem(QString("%1 · %2").arg(i+1).arg(info.audioTracks[i]));request_.source.audioIndex=0;status_->setText("메타데이터를 확인했습니다. 영상을 재생하지 않고 준비할 수 있습니다.");updateStart();});
    connect(&probe_,&PreparationMediaProbe::failed,this,[this](quint64 generation,const QString &message){if(generation!=probeGeneration_)return;probing_=false;request_.durationMs=0;durationLabel_->setText("길이 확인 실패");status_->setText(message);updateStart();});
    connect(&service_,&CaptionPreparationService::progress,this,[this](quint64 generation,const CaptionPreparationProgress &progress){if(generation==operation_){progress_=progress;reportProgress(progress);}});
    connect(&service_,&CaptionPreparationService::failed,this,[this](quint64 generation,const QString &message){if(generation!=operation_)return;operation_=service_.generation()+1;retryingAccelerator_=false;setBusy(false);status_->setText(message);});
    connect(&service_,&CaptionPreparationService::canceled,this,[this](quint64 generation){
        if(generation!=operation_)return;
        if(retryingAccelerator_){
            retryingAccelerator_=false;progress_.recognitionNotice.clear();progress_.canRetryAccelerator=false;setBusy(true);
            status_->setText("저장된 완료 구간부터 같은 설정으로 가속 장치를 다시 시도합니다.");operation_=service_.generation()+1;service_.start(request_);return;
        }
        operation_=service_.generation()+1;setBusy(false);status_->setText("취소했습니다. 저장된 완료 구간은 같은 폴더에서 이어서 준비할 수 있습니다."+(progress_.recognitionNotice.isEmpty()?QString{}:'\n'+progress_.recognitionNotice));
    });
    connect(&service_,&CaptionPreparationService::ready,this,[this](quint64 generation,CaptionPreparationResult result){if(generation!=operation_)return;setBusy(false);result_=std::move(result);bar_->setValue(1000);QString error;if(!exportDisplay(*result_,error))status_->setText("자막 프로젝트는 준비됐지만 표시 SRT 저장에 실패했습니다. "+error);else status_->setText(QString("전체 자막을 저장했습니다.%1\n%2").arg(result_->skippedTranslations?QString(" 한국어 %1구간은 제외됐습니다.").arg(result_->skippedTranslations):QString{},result_->projectFile));play_->setEnabled(true);});
    chooseDisc_->setEnabled(!discRoot_.isEmpty());form_->setRowVisible(playlists_,false);form_->setRowVisible(audio_,false);form_->setRowVisible(discAudio_,false);updateStart();
    if(!initialFile.isEmpty())setFile(initialFile);else if(!discRoot_.isEmpty())setDisc(discRoot_);
}
CaptionPreparationDialog::~CaptionPreparationDialog(){retryingAccelerator_=false;probe_.cancel();service_.cancel();}
void CaptionPreparationDialog::setFile(const QString &file){
    if(busy_)return;probe_.cancel();invalidateResult();customOutput_=false;request_.source.source=QFileInfo(file).absoluteFilePath();request_.source.disc=false;request_.source.playlist.clear();request_.source.startMs=0;request_.source.audioTrackId=-1;request_.durationMs=0;request_.source.audioIndex=0;
    sourceLabel_->setText(request_.source.source);playlists_->clear();form_->setRowVisible(playlists_,false);form_->setRowVisible(discAudio_,false);form_->setRowVisible(audio_,true);audio_->clear();audio_->setEnabled(true);durationLabel_->setText("미디어 정보 확인 중…");status_->setText("영상 재생 없이 길이와 오디오를 확인합니다.");setDefaultOutput();probing_=true;probeGeneration_=probe_.generation()+1;probe_.inspect(request_.source.source);updateStart();
}
void CaptionPreparationDialog::setDisc(const QString &root){
    if(busy_)return;probe_.cancel();probing_=false;invalidateResult();customOutput_=false;const auto preferred=request_.source;const auto preferredDuration=request_.durationMs;const auto disc=inspectDisc(root);
    const bool usePreferred=disc.valid && preferred.disc && preferredDuration>0 && preferredDuration<=24LL*60*60*1000 && preferred.audioIndex>=0 && preferred.audioIndex<64 && !preferred.playlist.isEmpty() && QFileInfo(localDiscPath(preferred.source))==QFileInfo(disc.root) && matchDiscPlaylist(disc,preferredDuration)==preferred.playlist;
    {QSignalBlocker guard(discAudio_);discAudio_->setValue(usePreferred?preferred.audioIndex+1:1);}
    request_.source.source=disc.root;request_.source.disc=true;request_.source.startMs=0;request_.source.audioTrackId=-1;request_.source.audioIndex=discAudio_->value()-1;request_.durationMs=0;request_.source.playlist.clear();sourceLabel_->setText(disc.label.isEmpty()?root:disc.label+" · "+root);audio_->clear();form_->setRowVisible(audio_,false);form_->setRowVisible(playlists_,true);form_->setRowVisible(discAudio_,true);discAudio_->setEnabled(true);
    {QSignalBlocker guard(playlists_);playlists_->clear();for(const auto &title:disc.titles)if(title.durationMs>0){playlists_->addItem(QString("%1 · %2").arg(title.playlist,formatTime(title.durationMs)),title.playlist);playlists_->setItemData(playlists_->count()-1,title.durationMs,Qt::UserRole+1);}if(playlists_->count()){int selected=0;for(int index=1;index<playlists_->count();++index)if(playlists_->itemData(index,Qt::UserRole+1).toLongLong()>playlists_->itemData(selected,Qt::UserRole+1).toLongLong())selected=index;if(usePreferred)selected=playlists_->findData(preferred.playlist);playlists_->setCurrentIndex(selected);request_.source.playlist=playlists_->currentData().toString();request_.durationMs=playlists_->currentData(Qt::UserRole+1).toLongLong();}}
    playlists_->setEnabled(disc.valid && playlists_->count()>0);durationLabel_->setText(request_.durationMs>0?formatTime(request_.durationMs):"타이틀 확인 실패");status_->setText(!disc.valid?"Blu-ray 플레이리스트를 읽을 수 없습니다. 디스크 연결을 확인해 주세요.":usePreferred?"현재 재생하던 타이틀과 오디오를 선택했습니다. 생성 전에 확인해 주세요.":"가장 긴 타이틀을 선택했습니다. 본편·부가영상은 디스크 메뉴와 대조해 선택하세요.");setDefaultOutput();updateStart();
}
void CaptionPreparationDialog::setDefaultOutput(){
    auto source=request_.source.source+'\n'+request_.source.playlist+'\n'+request_.source.language+'\n'+request_.model+'\n'+request_.translationModel+'\n'+QString::number(request_.source.audioIndex)+'\n'+QString::number(translate_->isChecked());
    if(translate_->isChecked() && !request_.translationGlossary.isEmpty())source+='\n'+formatTranslationGlossary(request_.translationGlossary);
    const auto suffix=QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(),QCryptographicHash::Sha256).toHex().left(16));
    output_->setText(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("prepared-captions/"+suffix));
}
void CaptionPreparationDialog::updateStart(){start_->setEnabled(!busy_ && !probing_ && request_.durationMs>0 && !output_->text().trimmed().isEmpty() && QFileInfo::exists(request_.model) && (!translate_->isChecked() || QFileInfo::exists(request_.translationModel)));}
void CaptionPreparationDialog::invalidateResult(){if(!result_)return;service_.cancel();operation_=service_.generation()+1;result_.reset();play_->setEnabled(false);bar_->setValue(0);status_->setText("선택이 바뀌었습니다. 이 설정으로 전체 자막을 준비해 주세요.");}
void CaptionPreparationDialog::setBusy(bool busy){busy_=busy;chooseFile_->setEnabled(!busy);chooseDisc_->setEnabled(!busy && !discRoot_.isEmpty());playlists_->setEnabled(!busy && request_.source.disc && playlists_->count()>0);audio_->setEnabled(!busy && !request_.source.disc);discAudio_->setEnabled(!busy && request_.source.disc);output_->setEnabled(!busy);browseOutput_->setEnabled(!busy);translate_->setEnabled(!busy);readingEnabled_->setEnabled(!busy && request_.source.language=="ja" && reading_.available());cancel_->setText(busy?"생성 취소":"닫기");updateBackendLabel();updateAcceleratorRetry();updateStart();}
void CaptionPreparationDialog::startPreparation(){
    if(!start_->isEnabled())return;retryingAccelerator_=false;result_.reset();play_->setEnabled(false);request_.source.startMs=0;request_.outputDirectory=output_->text().trimmed();request_.translate=translate_->isChecked();bar_->setValue(0);progress_={};setBusy(true);status_->setText("전체 원문을 먼저 준비합니다…");operation_=service_.generation()+1;service_.start(request_);
}
void CaptionPreparationDialog::retryAccelerator(){
    if(!busy_||!service_.busy()||!retryAccelerator_->isEnabled())return;
    retryingAccelerator_=true;retryAccelerator_->setEnabled(false);service_.cancel();
}
void CaptionPreparationDialog::reportProgress(const CaptionPreparationProgress &progress){
    if(progress.phase=="translation"){
        const auto denominator=std::max(1,progress.totalTranslations);bar_->setValue(700+int(299LL*progress.completedTranslations/denominator));status_->setText(QString("문맥으로 한국어 해석 중 · %1 / %2개\n전체 원문 인식이 끝났습니다.").arg(progress.completedTranslations).arg(progress.totalTranslations));
    }else{
        const auto denominator=std::max<qint64>(1,progress.totalMs);bar_->setValue(int((request_.translate?700LL:999LL)*progress.recognizedMs/denominator));status_->setText(QString("전체 원문 인식 중 · %1 / %2\n재생은 시작하지 않습니다.").arg(formatTime(progress.recognizedMs),formatTime(progress.totalMs)));
    }
    if(!progress.recognitionNotice.isEmpty())status_->setText(status_->text()+'\n'+progress.recognitionNotice+(progress.canRetryAccelerator?QString("\nCPU에서 계속 준비합니다. 저장된 구간부터 가속 장치를 다시 시도할 수 있습니다."):QString{}));
    updateBackendLabel();updateAcceleratorRetry();
}
void CaptionPreparationDialog::updateBackendLabel(){
    const auto devices=QStringList{progress_.recognitionBackend.isEmpty()?QString{}:"원문 · "+progress_.recognitionBackend,progress_.translationBackend.isEmpty()?QString{}:"해석 · "+progress_.translationBackend}.filter(QRegularExpression(".+")).join('\n');
    backend_->setText(devices.isEmpty()?QString{}:(busy_?QString{}:QString("마지막 사용 장치\n"))+devices);
}
void CaptionPreparationDialog::updateAcceleratorRetry(){
    const bool available=busy_&&progress_.phase=="recognition"&&progress_.canRetryAccelerator&&!progress_.recognitionNotice.isEmpty()&&request_.recognitionPreferences.mode!="cpu";
    retryAccelerator_->setVisible(available);retryAccelerator_->setEnabled(available);
}
bool CaptionPreparationDialog::exportDisplay(const CaptionPreparationResult &result,QString &error){
    QVector<SubtitleCue> display;for(const auto &page:captionPages(result.originals)){QStringList lines{page.text};if(readingEnabled_->isChecked()){const auto reading=reading_.reading(page.text);if(!reading.isEmpty())lines.append(reading);}const auto korean=result.translations.value(translationCueKey(page));if(translate_->isChecked() && !korean.isEmpty())lines.append(korean);display.append({page.startMs,page.endMs,lines.join('\n')});}
    QSaveFile file(QDir(result.request.outputDirectory).filePath("display.srt"));const auto bytes=writeSrt(display).toUtf8();if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()){error=file.errorString();return false;}return true;
}
void CaptionPreparationDialog::reject(){retryingAccelerator_=false;probe_.cancel();service_.cancel();QDialog::reject();}
QJsonObject CaptionPreparationDialog::diagnostics()const{return {{"busy",busy_},{"probing",probing_},{"disc",request_.source.disc},{"playlist",request_.source.playlist},{"durationMs",request_.durationMs},{"phase",progress_.phase},{"recognizedMs",progress_.recognizedMs},{"completedTranslations",progress_.completedTranslations},{"totalTranslations",progress_.totalTranslations},{"recognitionBackend",progress_.recognitionBackend},{"translationBackend",progress_.translationBackend},{"recognitionNotice",progress_.recognitionNotice},{"canRetryAccelerator",retryAccelerator_->isEnabled()},{"deviceDisplayHistorical",!busy_},{"complete",result_.has_value()}};}
}
