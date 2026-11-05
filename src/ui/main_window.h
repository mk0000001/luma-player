#pragma once
#include <QMainWindow>
#include <QTemporaryDir>
#include <QSet>
#include <QJsonObject>
#include "core/disc.h"
#include "media/playback.h"
#include "subtitles/subtitle_job.h"
class QLabel; class QPushButton; class QComboBox; class QSlider; class QListWidget; class QPlainTextEdit; class QCheckBox;
class QShortcut;
namespace luma {
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent=nullptr);
    ~MainWindow() override;
    void openSource(const QString &source);
    void enableAutomaticSubtitles(const QString &language);
    QJsonObject diagnostics() const;
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    QWidget *surface_,*welcome_,*sidebar_,*panel_,*header_;
    QLabel *nowTitle_,*status_,*clock_,*discLabel_,*discDetails_,*aiStatus_,*codec_;
    QPushButton *play_,*discButton_;
    QSlider *seek_,*volume_;
    QComboBox *titles_,*audio_,*subs_,*rate_,*language_;
    QListWidget *chapters_;
    QPlainTextEdit *log_;
    QCheckBox *aiEnabled_;
    std::unique_ptr<PlaybackEngine> discEngine_,fileEngine_;
    PlaybackEngine *active_=nullptr;
    SubtitleJob job_;
    QTemporaryDir subtitleCache_;
    QVector<SubtitleCue> generatedCues_;
    CaptionCoverage coverage_;
    QList<QShortcut*> discShortcuts_;
    DiscInfo discInfo_;
    QString source_,model_,discPath_;
    bool full_=false;
    int lastTitle_=-1;
    int decodedSamples_=0;
    void buildUi();
    void discoverDisc();
    void refresh();
    void appendLog(const QString &message);
    void toggleFullScreen();
    void scheduleSubtitles();
    void resetSubtitleSession();
    void cancelSubtitleWork();
};
}
