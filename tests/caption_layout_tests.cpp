#include <QtTest>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QTextBoundaryFinder>
#include <QTransform>
#include "ui/caption_overlay.h"
#include "ui/caption_layout.h"

namespace {
QImage renderOverlay(const QString &text,const QSize &size,const luma::CaptionStyle &style={}){
    QWidget window;window.resize(size);QWidget anchor(&window);anchor.resize(size);window.show();
    luma::CaptionOverlay overlay(&anchor);overlay.setCues({{0,1000,text}});overlay.setPosition(1);QCoreApplication::processEvents();
    overlay.setCaptionStyle(style);
    QImage result(size,QImage::Format_ARGB32_Premultiplied);result.fill(Qt::transparent);
    QPainter painter(&result);overlay.render(&painter,QPoint(),QRegion(),QWidget::DrawChildren);return result;
}
int maxOpacity(const QImage &image){
    int opacity=0;
    for(int y=0;y<image.height();++y)
        for(int x=0;x<image.width();++x)
            opacity=qMax(opacity,qAlpha(image.pixel(x,y)));
    return opacity;
}
int whiteTextBands(const QImage &image){
    bool previous=false;int bands=0;
    for(int y=0;y<image.height();++y){
        bool white=false;for(int x=0;x<image.width();++x){const auto pixel=image.pixelColor(x,y);if(pixel.alpha()>220 && pixel.red()>220 && pixel.green()>220 && pixel.blue()>220){white=true;break;}}
        if(white && !previous)++bands;previous=white;
    }
    return bands;
}
}
class CaptionLayoutTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){
        const auto runtime=qEnvironmentVariable("LUMA_FONT_RUNTIME");if(runtime.isEmpty())return;
        for(const auto &name:{"NotoSans-Regular.ttf","NotoSans-Bold.ttf","NotoSansCJKkr-Regular.otf","NotoSansCJKjp-Regular.otf"})QFontDatabase::addApplicationFont(QDir(runtime).filePath("fonts/"+QString(name)));
    }
    void existingOverlayWrapsAnUnbrokenCaption(){
        const auto image=renderOverlay(QString(180,'W'),QSize(360,220));
        const int bands=whiteTextBands(image);QVERIFY2(bands>0,"The test must render visible caption glyphs");
        QVERIFY2(bands>=2,"An unbroken caption must wrap across visible lines instead of being clipped as one long word");
    }
    void captionStyleKeepsFallbackFontsAndScalesBeforeWrapping(){
        const luma::CaptionStyle style{"Custom caption font",150,100,71};
        const QSizeF viewport(800,500);
        const auto original=luma::layoutCaption("Caption sample",viewport);
        const auto changed=luma::layoutCaption("Caption sample",viewport,0,nullptr,style);
        QVERIFY(original.text && changed.text);
        const auto font=changed.text->font();
        QCOMPARE(font.families().first(),style.fontFamily);
        QVERIFY(font.families().contains("Noto Sans CJK JP"));
        QVERIFY(font.families().contains("Noto Sans CJK KR"));
        QCOMPARE(font.pixelSize(),qRound(original.text->font().pixelSize()*1.5));
    }
    void overlayPaintsTextAndBackgroundAtIndependentOpacities(){
        const QSize size(320,180);
        const QString text="Caption sample";
        QCOMPARE(maxOpacity(renderOverlay(text,size,{{},100,0,0})),0);
        const int textOpacity=maxOpacity(renderOverlay(text,size,{{},100,40,0}));
        QVERIFY(textOpacity>=99 && textOpacity<=103);
        QCOMPARE(maxOpacity(renderOverlay(text,size,{{},100,0,60})),153);
    }
    void allTextIsLaidOutWithinTheVideo_data(){
        QTest::addColumn<QString>("source");QTest::addColumn<QSizeF>("viewport");QTest::addColumn<int>("inset");
        QTest::newRow("unbroken-Hangul")<<QString(240,QChar(0xac00))<<QSizeF(360,220)<<0;
        QTest::newRow("unbroken-Latin")<<QString(600,'W')<<QSizeF(360,220)<<0;
        QTest::newRow("unbroken-Japanese")<<QString(240,QChar(0x97ff))<<QSizeF(360,220)<<0;
        QTest::newRow("narrow")<<QString(400,QChar(0xd55c))<<QSizeF(220,120)<<0;
        QTest::newRow("tiny")<<QString(600,'W')<<QSizeF(80,60)<<0;
        QTest::newRow("three-lines")<<QString::fromUtf8("新しい景色へ。\n아타라시이 게시키에.\n새로운 풍경을 향해.")<<QSizeF(540,300)<<0;
        QTest::newRow("fullscreen")<<QString::fromUtf8("新しい景色へ。\n아타라시이 게시키에.\n새로운 풍경을 향해.")<<QSizeF(1920,1080)<<0;
        QTest::newRow("visible-toolbar")<<QString(200,QChar(0xd55c))<<QSizeF(360,220)<<74;
        QTest::newRow("emoji-dakuten-combining")<<QString::fromUtf8("👩‍👩‍👧‍👦がé한글").repeated(18)<<QSizeF(160,140)<<0;
    }
    void allTextIsLaidOutWithinTheVideo(){
        QFETCH(QString,source);QFETCH(QSizeF,viewport);QFETCH(int,inset);
        const auto layout=luma::layoutCaption(source,viewport,inset);QVERIFY(layout.text);QVERIFY(layout.text->lineCount()>0);
        QString actual=layout.text->text();actual.replace(QChar::LineSeparator,'\n');QCOMPARE(actual,source);
        const QRectF video(QPointF(),QSizeF(viewport.width(),viewport.height()-inset));QVERIFY(video.contains(layout.background));
        QTransform transform;transform.translate(layout.origin.x(),layout.origin.y());transform.scale(layout.scale,layout.scale);
        QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme,source);int consumed=0;
        for(int i=0;i<layout.text->lineCount();++i){
            const auto line=layout.text->lineAt(i);QCOMPARE(line.textStart(),consumed);consumed+=line.textLength();
            graphemes.setPosition(line.textStart());QVERIFY2(graphemes.isAtBoundary(),"A line starts inside a grapheme cluster");
            graphemes.setPosition(consumed);QVERIFY2(graphemes.isAtBoundary(),"A line ends inside a grapheme cluster");
            QVERIFY2(layout.background.adjusted(-.1,-.1,.1,.1).contains(transform.mapRect(line.naturalTextRect())),"Measured text extends outside its painted caption background");
        }
        QCOMPARE(consumed,source.size());
    }
    void explicitNewlinesKeepTheirOwnRows(){
        const auto layout=luma::layoutCaption(QString::fromUtf8("日本語\n한글\nEnglish"),QSizeF(800,500));QVERIFY(layout.text);
        QCOMPARE(layout.text->lineCount(),3);QCOMPARE(layout.text->lineAt(0).textStart(),0);QCOMPARE(layout.text->lineAt(1).textStart(),4);QCOMPARE(layout.text->lineAt(2).textStart(),7);
        const auto emptyLine=luma::layoutCaption("one\n\nthree",QSizeF(800,500));QVERIFY(emptyLine.text);QCOMPARE(emptyLine.text->lineCount(),3);
    }
    void toolbarInsetMovesOnlyTheCaption(){
        const auto without=luma::layoutCaption("A synthetic caption",QSizeF(640,360));
        const auto with=luma::layoutCaption("A synthetic caption",QSizeF(640,360),74);
        QVERIFY(with.background.bottom()<without.background.bottom());QVERIFY(with.background.bottom()<=286);
        QWidget window;window.resize(640,360);QWidget anchor(&window);anchor.resize(640,360);window.show();
        luma::CaptionOverlay overlay(&anchor);overlay.setCues({{0,1000,"A synthetic caption"}});overlay.setPosition(1);const auto video=anchor.geometry();
        overlay.setBottomInset(74);QCoreApplication::processEvents();QCOMPARE(anchor.geometry(),video);QCOMPARE(overlay.captionText(),QString("A synthetic caption"));
        window.move(100,80);
        QCOMPARE(overlay.geometry(),QRect(anchor.mapToGlobal(QPoint()),anchor.size()));
    }
};
QTEST_MAIN(CaptionLayoutTests)
#include "caption_layout_tests.moc"
