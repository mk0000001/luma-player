#include <QtTest>
#include <QVBoxLayout>
#include <QPainter>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include "ui/video_surface.h"
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
