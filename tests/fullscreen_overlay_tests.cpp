#include <QtTest>
#include <QVBoxLayout>
#include <QSlider>
#include <QMenu>
#include <QCursor>
#include <QCloseEvent>
#include <QDialog>
#include "ui/fullscreen_controls.h"
#include "ui/input_activity.h"
#include "ui/native_capture_observer.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <QScopeGuard>
#endif

namespace {
class ControlsCloseHost:public QWidget {
public:int closeCalls=0;bool vetoClose=false;
protected:void closeEvent(QCloseEvent *event)override{++closeCalls;event->setAccepted(!vetoClose);}
};
class RaisedControls:public QWidget {
public:int raises=0;
protected:bool event(QEvent *event)override{if(event->type()==QEvent::ZOrderChange)++raises;return QWidget::event(event);}
};
}
class FullscreenOverlayTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){QFont font=qApp->font();font.setPointSize(10);qApp->setFont(font);}
    void modalDialogKeepsTransportBehindUntilClosed_data(){QTest::addColumn<bool>("fullscreen");QTest::newRow("windowed")<<false;QTest::newRow("fullscreen")<<true;}
    void modalDialogKeepsTransportBehindUntilClosed(){
        QFETCH(bool,fullscreen);QWidget host;host.resize(800,600);auto *layout=new QVBoxLayout(&host);
        QWidget video,controls;controls.setFixedHeight(108);layout->addWidget(&video,1);layout->addWidget(&controls);
        luma::FullscreenControls controller(&host,&controls,&video);host.show();controller.setActive(fullscreen);QTRY_VERIFY(controls.isVisible());
        QDialog dialog(&host);dialog.setModal(true);dialog.show();QTRY_COMPARE(QApplication::activeModalWidget(),&dialog);
        QTRY_VERIFY(!controls.isVisible());QCOMPARE(controller.reservedHeight(),0);
        controller.activity();QCoreApplication::processEvents();QVERIFY(!controls.isVisible());
        dialog.close();QTRY_VERIFY(controls.isVisible());QVERIFY(controller.reservedHeight()>0);
    }
#ifdef Q_OS_WIN
    void nativeCaptureNotificationSurvivesCompletedClickAndIgnoresOtherEvents(){
        luma::NativeCaptureObserver observer;QVERIFY(observer.available());
        const auto window=CreateWindowExW(0,L"STATIC",L"Luma native input fixture",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
        QVERIFY(window);const auto cleanup=qScopeGuard([&]{DestroyWindow(window);});
        NotifyWinEvent(EVENT_SYSTEM_CAPTURESTART,window,OBJID_WINDOW,CHILDID_SELF);
        NotifyWinEvent(EVENT_SYSTEM_CAPTUREEND,window,OBJID_WINDOW,CHILDID_SELF);
        QTRY_VERIFY_WITH_TIMEOUT(observer.serial()>=2,2000);
        QCOMPARE(observer.window(),reinterpret_cast<quintptr>(window));
        const auto serial=observer.serial();NotifyWinEvent(EVENT_OBJECT_FOCUS,window,OBJID_WINDOW,CHILDID_SELF);
        QTest::qWait(60);QCOMPARE(observer.serial(),serial);
    }
    void nativeCaptureObserversUnregisterIndependentlyWithPendingEvents(){
        luma::NativeCaptureObserver survivor;QVERIFY(survivor.available());
        const auto window=CreateWindowExW(0,L"STATIC",L"Luma native input lifetime fixture",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
        QVERIFY(window);const auto cleanup=qScopeGuard([&]{DestroyWindow(window);});
        {
            luma::NativeCaptureObserver temporary;QVERIFY(temporary.available());
            NotifyWinEvent(EVENT_SYSTEM_CAPTURESTART,window,OBJID_WINDOW,CHILDID_SELF);
        }
        QTRY_VERIFY_WITH_TIMEOUT(survivor.serial()>0,2000);
        const auto serial=survivor.serial();NotifyWinEvent(EVENT_SYSTEM_CAPTUREEND,window,OBJID_WINDOW,CHILDID_SELF);
        QTRY_VERIFY_WITH_TIMEOUT(survivor.serial()!=serial,2000);
    }
#endif
    void completedNativeClickBetweenPollsIsDetectedWithoutPointerMovement(){
        luma::InputActivityTracker input;
        QVERIFY(!input.observe({100,true,true,false}));
        QVERIFY(input.observe({120,true,true,false}));
        QVERIFY(!input.observe({120,true,true,false}));
    }
    void nativeCaptureSerialWrapAndRegressionAreChanges(){
        luma::InputActivityTracker input;QVERIFY(!input.observe({0xfffffff0u,true,true,false}));
        QVERIFY(input.observe({5,true,true,false}));QVERIFY(input.observe({3,true,true,false}));
        QVERIFY(!input.observe({3,true,true,false}));
    }
    void foreignOrPopupInputIsConsumedWithoutStaleReplay_data(){
        QTest::addColumn<bool>("foreground");QTest::addColumn<bool>("pointer");QTest::addColumn<bool>("blocked");
        QTest::newRow("background-player")<<false<<true<<false;
        QTest::newRow("other-window-under-pointer")<<true<<false<<false;
        QTest::newRow("popup-or-modal")<<true<<true<<true;
    }
    void foreignOrPopupInputIsConsumedWithoutStaleReplay(){
        QFETCH(bool,foreground);QFETCH(bool,pointer);QFETCH(bool,blocked);
        luma::InputActivityTracker input;QVERIFY(!input.observe({100,true,true,false}));
        QVERIFY(!input.observe({120,foreground,pointer,blocked}));
        QVERIFY(!input.observe({120,true,true,false}));
        QVERIFY(input.observe({130,true,true,false}));
    }
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
    void videoActivationAndClicksRestoreTransportStack_data(){
        QTest::addColumn<bool>("active");QTest::addColumn<int>("button");
        QTest::newRow("windowed-right")<<false<<int(Qt::RightButton);
        QTest::newRow("windowed-middle")<<false<<int(Qt::MiddleButton);
        QTest::newRow("fullscreen-right")<<true<<int(Qt::RightButton);
        QTest::newRow("fullscreen-middle")<<true<<int(Qt::MiddleButton);
    }
    void videoActivationAndClicksRestoreTransportStack(){
        QFETCH(bool,active);QFETCH(int,button);
        QWidget host;host.resize(700,450);auto *layout=new QVBoxLayout(&host);
        QWidget surface;RaisedControls controls;controls.setFixedHeight(74);layout->addWidget(&surface);layout->addWidget(&controls);
        luma::FullscreenControls controller(&host,&controls,&surface);
        QWidget nativeVideo(&host,Qt::Tool|Qt::FramelessWindowHint);nativeVideo.setGeometry(40,40,600,350);
        host.show();nativeVideo.show();controller.setActive(active);QCoreApplication::processEvents();
        const auto geometry=surface.geometry();controls.raises=0;
        // Native video child activation can reorder owned windows without a
        // QWidget ZOrderChange, leaving a visible transport behind the video.
        QEvent activate(QEvent::WindowActivate);QCoreApplication::sendEvent(&nativeVideo,&activate);QCoreApplication::processEvents();
        QVERIFY2(controls.raises>0,"Activating video must restore the transport above it without a resize");
        controls.raises=0;QTest::mouseClick(&nativeVideo,Qt::MouseButton(button));QCoreApplication::processEvents();
        QVERIFY2(controls.raises>0,"Right/middle clicks must restore the transport stacking order");
        QVERIFY(controls.isVisible());QCOMPARE(surface.geometry(),geometry);
    }
    void closingTransportToolForwardsHostCloseAndVeto(){
        QFETCH(bool,active);ControlsCloseHost host;host.resize(600,400);auto *layout=new QVBoxLayout(&host);QWidget video,controls;controls.setFixedHeight(74);layout->addWidget(&video,1);layout->addWidget(&controls);
        luma::FullscreenControls controller(&host,&controls,&video);host.show();controller.setActive(active);QCoreApplication::processEvents();QVERIFY(controls.isVisible());
        host.vetoClose=true;controls.close();QCOMPARE(host.closeCalls,1);QVERIFY(host.isVisible());QVERIFY(controls.isVisible());
        host.vetoClose=false;controls.close();QCOMPARE(host.closeCalls,2);QTRY_VERIFY(!host.isVisible());QTRY_VERIFY(!controls.isVisible());
    }
};
QTEST_MAIN(FullscreenOverlayTests)
#include "fullscreen_overlay_tests.moc"
