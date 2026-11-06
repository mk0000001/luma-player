#pragma once
#include <QDialog>
#include <QJsonObject>
#include <optional>
#include "subtitles/caption_preparation.h"
#include "subtitles/preparation_media_probe.h"
#include "subtitles/japanese_reading.h"
class QLabel;class QLineEdit;class QPushButton;class QComboBox;class QSpinBox;class QCheckBox;class QProgressBar;class QFormLayout;
namespace luma {
class CaptionPreparationDialog final:public QDialog {
    Q_OBJECT
public:
    explicit CaptionPreparationDialog(CaptionPreparationRequest defaults,QString discRoot={},QString initialFile={},QWidget *parent=nullptr,QString runtime={});
    ~CaptionPreparationDialog()override;
    void setFile(const QString &file);
    void setDisc(const QString &root);
    std::optional<CaptionPreparationResult> prepared()const{return result_;}
    QJsonObject diagnostics()const;
protected:
    void reject()override;
private:
    CaptionPreparationRequest request_;
    CaptionPreparationService service_;
    PreparationMediaProbe probe_;
    JapaneseReading reading_;
    std::optional<CaptionPreparationResult> result_;
    CaptionPreparationProgress progress_;
    quint64 operation_=0,probeGeneration_=0;
    QString runtime_,discRoot_;
    QLabel *sourceLabel_=nullptr,*durationLabel_=nullptr,*status_=nullptr,*backend_=nullptr;
    QLineEdit *output_=nullptr;
    QPushButton *chooseFile_=nullptr,*chooseDisc_=nullptr,*browseOutput_=nullptr,*start_=nullptr,*cancel_=nullptr,*play_=nullptr;
    QComboBox *playlists_=nullptr,*audio_=nullptr;
    QSpinBox *discAudio_=nullptr;
    QCheckBox *translate_=nullptr,*readingEnabled_=nullptr;
    QProgressBar *bar_=nullptr;
    QFormLayout *form_=nullptr;
    bool busy_=false,probing_=false,customOutput_=false;
    void setBusy(bool busy);
    void updateStart();
    void invalidateResult();
    void setDefaultOutput();
    void startPreparation();
    void reportProgress(const CaptionPreparationProgress &progress);
    bool exportDisplay(const CaptionPreparationResult &result,QString &error);
};
}
