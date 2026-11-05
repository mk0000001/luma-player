#include <QtTest>
#include <QVBoxLayout>
#include <QSlider>
#include <QMenu>
#include <QCursor>
#include <QCloseEvent>
#include "ui/fullscreen_controls.h"

namespace {
class ControlsCloseHost:public QWidget {
public:int closeCalls=0;bool vetoClose=false;
protected:void closeEvent(QCloseEvent *event)override{++closeCalls;event->setAccepted(!vetoClose);}
};
}
class FullscreenOverlayTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){QFont font=qApp->font();font.setPointSize(10);qApp->setFont(font);}
    void showingAndHidingControlsDoesNotResizeVideo(){
        QWidget host;host.resize(800,600);auto *layout=new QVBoxLayout(&host);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
        QWidget video,controls;controls.setFixedHeight(74);layout->addWidget(&video,1);layout->addWidget(&controls);
        luma::FullscreenControls controller(&host,&controls,&video);QSignalSpy inset(&controller,&luma::FullscreenControls::reservedHeightChanged);host.show();QTest::qWait(40);controller.setActive(true);QTest::mouseMove(&host,QPoint(20,20));
        const auto original=video.geometry();QVERIFY(controls.isVisible());QTest::qWait(5300);QVERIFY(!controls.isVisible());
        QCOMPARE(controller.reservedHeight(),0);QVERIFY(!inset.isEmpty());QCOMPARE(inset.last()[0].toInt(),0);
        QCOMPARE(video.geometry(),original);
        QTest::mouseMove(&host,QPoint(40,40));QCoreApplication::processEvents();QVERIFY(controls.isVisible());QCOMPARE(video.geometry(),original);
        QCOMPARE(controller.reservedHeight(),74);QCOMPARE(inset.last()[0].toInt(),74);
        QVERIFY(controls.isWindow());QCOMPARE(controls.parentWidget(),&host);
        QVERIFY(!controls.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        const QRect target(video.mapToGlobal(QPoint(0,video.height()-controls.height())),QSize(video.width(),controls.height()));QCOMPARE(controls.geometry(),target);
    }
    void hostMoveResizeAndVisibilityKeepControlsAnchored(){
        QWidget host;host.resize(600,400);auto *layout=new QVBoxLayout(&host);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
        QWidget video,controls;controls.setFixedHeight(108);layout->addWidget(&video,1);layout->addWidget(&controls);
        luma::FullscreenControls controller(&host,&controls,&video);host.show();QTest::qWait(40);QCOMPARE(controller.reservedHeight(),108);
        host.move(80,90);host.resize(700,450);QCoreApplication::processEvents();QTest::qWait(20);
        QCOMPARE(controls.geometry(),QRect(video.mapToGlobal(QPoint(0,video.height()-108)),QSize(video.width(),108)));
        const auto original=video.geometry();controls.setFixedHeight(74);QCoreApplication::processEvents();QTest::qWait(20);QCOMPARE(video.geometry(),original);
        QCOMPARE(controller.reservedHeight(),74);
        QCOMPARE(controls.geometry(),QRect(video.mapToGlobal(QPoint(0,video.height()-74)),QSize(video.width(),74)));
        host.hide();QCoreApplication::processEvents();QVERIFY(!controls.isVisible());host.show();QCoreApplication::processEvents();QTest::qWait(20);QVERIFY(controls.isVisible());
        host.showMinimized();QCoreApplication::processEvents();QTest::qWait(20);QVERIFY(!controls.isVisible());host.showNormal();QCoreApplication::processEvents();QTest::qWait(20);QVERIFY(controls.isVisible());
    }
    void stationaryPointerOverControlsStillAllowsIdleHide(){
        QWidget host;host.resize(800,600);auto *layout=new QVBoxLayout(&host);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
        QWidget video,controls;controls.setFixedHeight(74);layout->addWidget(&video,1);layout->addWidget(&controls);
        luma::FullscreenControls controller(&host,&controls,&video);host.show();QTest::qWait(40);controller.setActive(true);
        const auto videoGeometry=video.geometry(),toolbarGeometry=controls.geometry();
        QCursor::setPos(controls.mapToGlobal(controls.rect().center()));QTest::mouseMove(&controls,controls.rect().center());controller.activity();
        QVERIFY(toolbarGeometry.contains(QCursor::pos()));QVERIFY(controls.isVisible());
        QTest::qWait(5300);
        QVERIFY2(!controls.isVisible(),"A stationary pointer over the toolbar must not count as ongoing interaction");
        QCOMPARE(video.geometry(),videoGeometry);QCOMPARE(host.cursor().shape(),Qt::BlankCursor);
        QTest::mouseMove(&host,QPoint(30,30));QCoreApplication::processEvents();
        QVERIFY(controls.isVisible());QCOMPARE(video.geometry(),videoGeometry);QVERIFY(host.cursor().shape()!=Qt::BlankCursor);
    }
    void sliderAndPopupKeepControlsVisible(){
        QWidget host;host.resize(800,600);auto *layout=new QVBoxLayout(&host);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
        QWidget video,controls;controls.setFixedHeight(74);auto *buttons=new QHBoxLayout(&controls);QSlider seek(Qt::Horizontal);buttons->addWidget(&seek);layout->addWidget(&video,1);layout->addWidget(&controls);
        luma::FullscreenControls controller(&host,&controls,&video);host.show();QTest::qWait(40);controller.setActive(true);QTest::mouseMove(&host,QPoint(20,20));const auto original=video.geometry();
        seek.setSliderDown(true);QTest::qWait(5300);QVERIFY(controls.isVisible());QCOMPARE(video.geometry(),original);seek.setSliderDown(false);
        QMenu popup(&controls);popup.addAction("Synthetic option");popup.popup(controls.mapToGlobal(QPoint(10,10)));QTest::qWait(5300);QVERIFY(controls.isVisible());QCOMPARE(video.geometry(),original);popup.close();
        controls.activateWindow();seek.setFocus();QTest::qWait(4800);const auto value=seek.value();QTest::keyClick(&seek,Qt::Key_Right);QVERIFY(seek.value()>value);QTest::qWait(600);QVERIFY(controls.isVisible());
    }
    void closingTransportToolForwardsHostCloseAndVeto_data(){QTest::addColumn<bool>("active");QTest::newRow("windowed")<<false;QTest::newRow("fullscreen")<<true;}
    void closingTransportToolForwardsHostCloseAndVeto(){
        QFETCH(bool,active);ControlsCloseHost host;host.resize(600,400);auto *layout=new QVBoxLayout(&host);QWidget video,controls;controls.setFixedHeight(74);layout->addWidget(&video,1);layout->addWidget(&controls);
        luma::FullscreenControls controller(&host,&controls,&video);host.show();controller.setActive(active);QCoreApplication::processEvents();QVERIFY(controls.isVisible());
        host.vetoClose=true;controls.close();QCOMPARE(host.closeCalls,1);QVERIFY(host.isVisible());QVERIFY(controls.isVisible());
        host.vetoClose=false;controls.close();QCOMPARE(host.closeCalls,2);QTRY_VERIFY(!host.isVisible());QTRY_VERIFY(!controls.isVisible());
    }
};
QTEST_MAIN(FullscreenOverlayTests)
#include "fullscreen_overlay_tests.moc"
