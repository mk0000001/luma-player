#include <QtTest>
#include <QVBoxLayout>
#include <QPainter>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QWindow>
#include "ui/performance_overlay.h"
#include "ui/video_surface.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace {
class VideoEventHost:public QWidget {
public:
    int closeCalls=0;bool vetoClose=false;
protected:
    void closeEvent(QCloseEvent *event)override{++closeCalls;event->setAccepted(!vetoClose);}
};
}
class VideoSurfaceTests:public QObject {
    Q_OBJECT
private slots:
    void videoHasItsOwnOwnedTopLevelWindow(){
        QWidget host;host.resize(640,360);auto *layout=new QVBoxLayout(&host);layout->setContentsMargins(0,0,0,0);luma::VideoSurface surface;layout->addWidget(&surface);
        host.show();QCoreApplication::processEvents();QTest::qWait(20);auto *video=surface.videoWindow();QVERIFY(video);
        QVERIFY2(video!=&surface,"Capture exclusion must target a separate top-level video window, not the main UI's child widget");
        QVERIFY(video->isWindow());QCOMPARE(video->parentWidget(),&host);QVERIFY(!video->isVisible());QVERIFY(surface.videoWindowId()!=0);QVERIFY(surface.videoWindowId()!=host.winId());QVERIFY(!video->isVisible());
        surface.setVideoActive(true);QTRY_VERIFY(video->isVisible());QCOMPARE(video->geometry(),QRect(surface.mapToGlobal(QPoint()),surface.size()));
        QVERIFY(!video->windowFlags().testFlag(Qt::WindowTransparentForInput));QVERIFY(!video->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));QVERIFY(!video->testAttribute(Qt::WA_TransparentForMouseEvents));
        surface.setCursor(Qt::BlankCursor);QCOMPARE(video->cursor().shape(),Qt::BlankCursor);surface.unsetCursor();QVERIFY(video->cursor().shape()!=Qt::BlankCursor);
    }
    void videoGeometryTracksOnlyItsPlaceholder(){
        QWidget host;host.resize(700,500);auto *layout=new QVBoxLayout(&host);layout->setContentsMargins(16,12,16,12);
        QWidget header,footer;header.setFixedHeight(30);footer.setFixedHeight(40);luma::VideoSurface surface;layout->addWidget(&header);layout->addWidget(&surface,1);layout->addWidget(&footer);
        host.show();surface.setVideoActive(true);QCoreApplication::processEvents();QTest::qWait(20);auto *video=surface.videoWindow();QVERIFY(video);const auto id=surface.videoWindowId();
        const auto original=surface.geometry();surface.setVideoActive(false);QCoreApplication::processEvents();QVERIFY(!video->isVisible());QCOMPARE(surface.geometry(),original);
        surface.setVideoActive(true);QCoreApplication::processEvents();QCOMPARE(surface.geometry(),original);
        host.move(120,80);
        QCOMPARE(video->geometry(),QRect(surface.mapToGlobal(QPoint()),surface.size()));
        host.resize(850,560);QCoreApplication::processEvents();QTest::qWait(20);QCOMPARE(video->geometry(),QRect(surface.mapToGlobal(QPoint()),surface.size()));
        host.showFullScreen();QCoreApplication::processEvents();QTest::qWait(20);QCOMPARE(video->geometry(),QRect(surface.mapToGlobal(QPoint()),surface.size()));QCOMPARE(surface.videoWindowId(),id);
        host.showNormal();QCoreApplication::processEvents();QTest::qWait(20);QCOMPARE(video->geometry(),QRect(surface.mapToGlobal(QPoint()),surface.size()));QCOMPARE(surface.videoWindowId(),id);
        host.showMinimized();QCoreApplication::processEvents();QTest::qWait(20);QVERIFY(!video->isVisible());host.showNormal();QCoreApplication::processEvents();QTest::qWait(20);QVERIFY(video->isVisible());
        host.hide();QCoreApplication::processEvents();QTest::qWait(20);QVERIFY(!video->isVisible());
    }
    void excludingVideoLeavesBlackBehindAndControlsIndependent(){
        QWidget host;host.resize(500,320);auto *layout=new QVBoxLayout(&host);layout->setContentsMargins(0,0,0,0);luma::VideoSurface surface;layout->addWidget(&surface);host.show();surface.setVideoActive(true);QCoreApplication::processEvents();QTest::qWait(20);
        auto *video=surface.videoWindow();QVERIFY(video);video->setStyleSheet("#nativeVideoWindow{background:#ff00ff;}");
        const auto native=video->grab().toImage();QCOMPARE(native.pixelColor(native.width()/2,native.height()/2),QColor(Qt::magenta));
        QImage image(surface.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);surface.render(&painter);painter.end();
        QCOMPARE(image.pixelColor(image.width()/2,image.height()/2),QColor(Qt::black));
        QVERIFY(video->window()!=host.window());QVERIFY(!host.windowFlags().testFlag(Qt::Tool));
    }
#ifdef Q_OS_WIN
    void menuPreviewReadsOnlyTheVideoWindow(){
        if (QGuiApplication::platformName()!="windows" || !qEnvironmentVariableIsSet("LUMA_TEST_MENU_PREVIEW"))
            QSKIP("Requires interactive Windows Graphics Capture");
        QWidget video;
        video.setStyleSheet("background: rgb(25, 72, 197)");
        video.resize(480,270);
        video.show();
        QTRY_VERIFY(video.isVisible());
        luma::DiscMenuPreview capture;
        QImage frame;
        const auto receivedBlue=[&]{
            frame=capture.latestFrame(video.winId());
            return !frame.isNull() && frame.pixelColor(frame.width()/2,frame.height()/2).blue()>150;
        };
        QTRY_VERIFY_WITH_TIMEOUT(receivedBlue(),3500);
        QVERIFY(SetWindowDisplayAffinity(reinterpret_cast<HWND>(video.winId()),0x11));
        QVERIFY(capture.latestFrame(video.winId()).isNull());
    }

    void performanceOverlayStaysAboveTheNativeVideo(){
        if(QGuiApplication::platformName()!="windows")QSKIP("Requires native Windows window stacking");
        QWidget host;
        host.resize(720,480);
        auto *layout=new QVBoxLayout(&host);
        layout->setContentsMargins(0,0,0,0);
        luma::VideoSurface surface;
        layout->addWidget(&surface);
        std::unique_ptr<luma::PlaybackEngine> playback;
        luma::PerformanceOverlay overlay(&surface,[&]{
            luma::PerformanceInput input;
            if(playback)input.playback=playback->state();
            return input;
        });
        host.show();
        surface.setVideoActive(true);
        auto *video=surface.videoWindow();
        const auto fixture=qEnvironmentVariable("LUMA_VIDEO_STACK_FIXTURE");
        if(fixture.isEmpty())video->setStyleSheet("#nativeVideoWindow{background:#3344cc;}");
        else{
            QVERIFY(QFileInfo(fixture).isFile());
            playback=std::make_unique<luma::PlaybackEngine>(luma::PlaybackEngine::Kind::File,qEnvironmentVariable("LUMA_TEST_RUNTIME"));
            QVERIFY2(playback->open(fixture,surface.videoWindowId()),qPrintable(playback->lastError()));
            playback->setVolume(0);
            QTRY_VERIFY_WITH_TIMEOUT(playback->state().positionMs>100,10000);
        }
        overlay.setMonitoring(true);
        QTRY_VERIFY(video->isVisible() && overlay.isVisible());
        QVERIFY(overlay.testAttribute(Qt::WA_TransparentForMouseEvents));
        QVERIFY(overlay.windowFlags().testFlag(Qt::WindowTransparentForInput));
        constexpr DWORD captureExclusion=0x11;
        const bool captureApplied=SetWindowDisplayAffinity(reinterpret_cast<HWND>(video->winId()),captureExclusion);
        const auto above=[&]{
            const auto layer=reinterpret_cast<HWND>(overlay.internalWinId());
            const auto nativeVideo=reinterpret_cast<HWND>(video->internalWinId());
            if(!layer || !nativeVideo)return false;
            for(auto previous=GetWindow(nativeVideo,GW_HWNDPREV);previous;previous=GetWindow(previous,GW_HWNDPREV))
                if(previous==layer)return true;
            return false;
        };
        const auto verifyMode=[&](const char *mode,bool topmost){
            QTRY_VERIFY(video->isVisible() && overlay.isVisible());
            const auto owner=reinterpret_cast<HWND>(host.winId());
            const auto layer=reinterpret_cast<HWND>(overlay.winId());
            const auto nativeVideo=reinterpret_cast<HWND>(video->winId());
            QCOMPARE(GetWindow(layer,GW_OWNER),owner);
            QCOMPARE(GetWindow(nativeVideo,GW_OWNER),owner);
            QCOMPARE(bool(GetWindowLongPtrW(owner,GWL_EXSTYLE)&WS_EX_TOPMOST),topmost);
            const bool overlayTopmost=GetWindowLongPtrW(layer,GWL_EXSTYLE)&WS_EX_TOPMOST;
            const bool videoTopmost=GetWindowLongPtrW(nativeVideo,GWL_EXSTYLE)&WS_EX_TOPMOST;
            if(!topmost){QVERIFY(!overlayTopmost);QVERIFY(!videoTopmost);}
            QTRY_VERIFY2_WITH_TIMEOUT(above(),mode,1500);
            // A renderer or native activation can raise the video without a QWidget z-order event.
            QVERIFY(SetWindowPos(nativeVideo,HWND_TOP,0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOSENDCHANGING));
            QTRY_VERIFY2_WITH_TIMEOUT(above(),mode,1500);
            QCOMPARE(bool(GetWindowLongPtrW(layer,GWL_EXSTYLE)&WS_EX_TOPMOST),overlayTopmost);
            QCOMPARE(bool(GetWindowLongPtrW(nativeVideo,GWL_EXSTYLE)&WS_EX_TOPMOST),videoTopmost);
            if(captureApplied){
                DWORD affinity=0;
                QVERIFY(GetWindowDisplayAffinity(nativeVideo,&affinity));
                QCOMPARE(affinity,captureExclusion);
            }
        };
        verifyMode("Windowed overlay must stay above a native video raise",false);
        if(QTest::currentTestFailed())return;
        host.showFullScreen();
        verifyMode("Fullscreen overlay must stay above a native video raise",false);
        if(QTest::currentTestFailed())return;
        host.showNormal();
        host.resize(440,270);
        host.windowHandle()->setFlag(Qt::WindowStaysOnTopHint,true);
        verifyMode("PiP overlay must stay above a native video raise",true);
        if(QTest::currentTestFailed())return;
        host.windowHandle()->setFlag(Qt::WindowStaysOnTopHint,false);
        host.resize(720,480);
        verifyMode("Restored overlay must stay above a native video raise",false);
        if(captureApplied && !QTest::currentTestFailed())qInfo("Video capture exclusion survived all native stacking checks.");
    }
#endif
    void closingVideoToolClosesTheHost(){
        VideoEventHost host;host.resize(500,320);auto *layout=new QVBoxLayout(&host);luma::VideoSurface surface;layout->addWidget(&surface);
        host.show();surface.setVideoActive(true);QCoreApplication::processEvents();auto *video=surface.videoWindow();QVERIFY(video->isVisible());
        video->close();QCOMPARE(host.closeCalls,1);QTRY_VERIFY(!host.isVisible());QTRY_VERIFY(!video->isVisible());
    }
    void closingVideoToolPreservesAHostCloseVeto(){
        VideoEventHost host;host.vetoClose=true;host.resize(500,320);auto *layout=new QVBoxLayout(&host);luma::VideoSurface surface;layout->addWidget(&surface);
        host.show();surface.setVideoActive(true);QCoreApplication::processEvents();auto *video=surface.videoWindow();QVERIFY(video->isVisible());
        video->close();QCOMPARE(host.closeCalls,1);QVERIFY(host.isVisible());QVERIFY(video->isVisible());
    }
    void videoToolEmitsUrlDropExactlyOnceAndRejectsPlainText_data(){
        QTest::addColumn<QList<QUrl>>("urls");
        const auto local=QUrl::fromLocalFile(QString::fromUtf8("C:/owned/日本語 한글.mkv")),remote=QUrl("https://example.invalid/owned.webm");
        QTest::newRow("Unicode-local-file")<<QList<QUrl>{local};QTest::newRow("remote-URL")<<QList<QUrl>{remote};QTest::newRow("ordered-multiple-URLs")<<QList<QUrl>{local,remote};
    }
    void videoToolEmitsUrlDropExactlyOnceAndRejectsPlainText(){
        QFETCH(QList<QUrl>,urls);
        VideoEventHost host;host.setAcceptDrops(true);host.resize(500,320);auto *layout=new QVBoxLayout(&host);luma::VideoSurface surface;layout->addWidget(&surface);
        host.show();surface.setVideoActive(true);QCoreApplication::processEvents();auto *video=surface.videoWindow();QVERIFY(video->acceptDrops());
        QSignalSpy dropped(&surface,&luma::VideoSurface::urlsDropped);QMimeData mime;mime.setUrls(urls);
        QDragEnterEvent enter(QPoint(10,10),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(video,&enter);
        QVERIFY(enter.isAccepted());QCOMPARE(dropped.size(),0);
        QDropEvent drop(QPointF(10,10),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(video,&drop);
        QVERIFY(drop.isAccepted());QCOMPARE(dropped.size(),1);QCOMPARE(qvariant_cast<QList<QUrl>>(dropped[0][0]),urls);
        QMimeData text;text.setText("owned plain text");QDragEnterEvent rejected(QPoint(10,10),Qt::CopyAction,&text,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(video,&rejected);
        QVERIFY(!rejected.isAccepted());QCOMPARE(dropped.size(),1);
    }
};
QTEST_MAIN(VideoSurfaceTests)
#include "video_surface_tests.moc"
