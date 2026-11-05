#include <QtTest>
#include <QComboBox>
#include "ui/main_window.h"
class UiTests:public QObject {
    Q_OBJECT
private slots:
    void menuShortcutsPreserveControlKeyboardInput(){
        luma::MainWindow window;window.show();QTest::qWait(80);
        QComboBox *rate=nullptr;for(auto *box:window.findChildren<QComboBox*>())if(box->findText("1×")>=0)rate=box;
        QVERIFY(rate);rate->setFocus();QCOMPARE(rate->currentIndex(),2);QTest::keyClick(rate,Qt::Key_Down);QCOMPARE(rate->currentIndex(),3);
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
