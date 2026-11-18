#include <QtTest>
#include <QTemporaryDir>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include "ui/caption_preparation_dialog.h"
#include "subtitles/translation_support.h"
using namespace luma;
class CaptionPreparationDialogTests:public QObject {
    Q_OBJECT
    CaptionPreparationResult complete(QTemporaryDir &root){
        CaptionPreparationResult result;result.request.outputDirectory=root.path();result.request.durationMs=2000;result.request.translate=true;result.originals={{0,2000,"Synthetic original"}};result.translations.insert(translationCueKey(result.originals[0]),"합성 해석");result.projectFile=root.filePath("project.json");return result;
    }
private slots:
    void changedGenerationInputsInvalidateCompletedPlayback_data(){QTest::addColumn<QString>("control");QTest::newRow("audio")<<QString("audio");QTest::newRow("translation")<<QString("translation");QTest::newRow("output")<<QString("output");}
    void changedGenerationInputsInvalidateCompletedPlayback(){
        QFETCH(QString,control);QTemporaryDir root;CaptionPreparationRequest defaults;defaults.translate=true;CaptionPreparationDialog dialog(defaults,{}, {},nullptr,root.path());
        auto *service=dialog.findChild<CaptionPreparationService*>();auto *play=dialog.findChild<QPushButton*>("preparePlay");auto *audio=dialog.findChild<QComboBox*>("prepareFileAudio");QVERIFY(service);QVERIFY(play);QVERIFY(audio);audio->addItems({"Japanese","English"});
        service->ready(service->generation(),complete(root));QVERIFY(play->isEnabled());QVERIFY(dialog.prepared());QVERIFY(QFileInfo::exists(root.filePath("display.srt")));
        if(control=="audio")audio->setCurrentIndex(1);else if(control=="translation")dialog.findChild<QCheckBox*>("prepareKorean")->setChecked(false);else dialog.findChild<QLineEdit*>("prepareOutputDirectory")->setText(root.filePath("different"));
        QVERIFY(!play->isEnabled());QVERIFY(!dialog.prepared());QVERIFY(QFileInfo::exists(root.filePath("display.srt")));
    }
    void staleCompletionCannotEnablePlayback(){QTemporaryDir root;CaptionPreparationDialog dialog({}, {}, {},nullptr,root.path());auto *service=dialog.findChild<CaptionPreparationService*>();QVERIFY(service);service->ready(service->generation()+99,complete(root));QVERIFY(!dialog.prepared());QVERIFY(!dialog.findChild<QPushButton*>("preparePlay")->isEnabled());}
};
QTEST_MAIN(CaptionPreparationDialogTests)
#include "caption_preparation_dialog_tests.moc"
