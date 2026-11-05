#include <QtTest>
#include <QWidget>
#include <QSet>
#include "media/platform_features.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace luma;
class PlatformTests:public QObject {
    Q_OBJECT
private slots:
    void captureAffinityRestoresAndFollowsNewWindows(){
        if(!qEnvironmentVariableIsSet("LUMA_NATIVE_PLATFORM"))QSKIP("Native desktop capture test is opt in.");
        QWidget main,caption;main.show();
        caption.setWindowFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);
        caption.setAttribute(Qt::WA_TranslucentBackground);caption.show();
        CaptureGuard guard;guard.watch(&main);guard.setEnabled(true);guard.watch(&caption);
#ifdef Q_OS_WIN
        QTRY_VERIFY(guard.applied());DWORD value=0;
        QVERIFY(GetWindowDisplayAffinity(reinterpret_cast<HWND>(main.winId()),&value));QCOMPARE(value,DWORD(0x11));
        caption.hide();caption.setWindowFlag(Qt::WindowStaysOnTopHint);caption.show();QTRY_VERIFY(guard.applied());
        guard.setEnabled(false);QVERIFY(!guard.applied());
        QVERIFY(GetWindowDisplayAffinity(reinterpret_cast<HWND>(main.winId()),&value));QCOMPARE(value,DWORD(WDA_NONE));
        QVERIFY(GetWindowDisplayAffinity(reinterpret_cast<HWND>(caption.winId()),&value));QCOMPARE(value,DWORD(WDA_NONE));
#else
        QVERIFY(!guard.applied());
#endif
    }
    void displayProbeMakesNoDolbyCertificationClaim(){
        QWidget window;const auto caps=displayCapabilities(window.winId());
        QVERIFY(caps.contains("hdrActive"));QVERIFY(!caps.value("dolbyVisionCertified").toBool());
        QSet<QString> selectableIds;
        for(const auto &adapter:videoAdapters()){
            const auto obj=adapter.toObject();const auto id=obj.value("id").toString();
            QVERIFY(!id.isEmpty());QVERIFY(!obj.value("name").toString().isEmpty());
            QVERIFY2(!selectableIds.contains(id),qPrintable("Duplicate GPU selection: "+id));selectableIds.insert(id);
        }
    }
};
QTEST_MAIN(PlatformTests)
#include "platform_tests.moc"
