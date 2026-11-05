#include "japanese_reading.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLibrary>
#include <QMutex>
#include <QRegularExpression>

namespace luma {
namespace {
bool kana(QChar ch){const auto c=ch.unicode();return (c>=0x3041 && c<=0x30fa) || (c>=0xff66 && c<=0xff9f);}
bool attachFinal(QString &text,int final){
    if(text.isEmpty())return false;const auto c=text.back().unicode();
    if(c<0xac00 || c>0xd7a3 || (c-0xac00)%28)return false;
    text.back()=QChar(c+final);return true;
}
const QHash<QString,QString> &moras(){
    static const QHash<QString,QString> table={
        {"ア","아"},{"イ","이"},{"ウ","우"},{"エ","에"},{"オ","오"},
        {"カ","카"},{"キ","키"},{"ク","쿠"},{"ケ","케"},{"コ","코"},
        {"ガ","가"},{"ギ","기"},{"グ","구"},{"ゲ","게"},{"ゴ","고"},
        {"サ","사"},{"シ","시"},{"ス","스"},{"セ","세"},{"ソ","소"},
        {"ザ","자"},{"ジ","지"},{"ズ","즈"},{"ゼ","제"},{"ゾ","조"},
        {"タ","타"},{"チ","치"},{"ツ","츠"},{"テ","테"},{"ト","토"},
        {"ダ","다"},{"ヂ","지"},{"ヅ","즈"},{"デ","데"},{"ド","도"},
        {"ナ","나"},{"ニ","니"},{"ヌ","누"},{"ネ","네"},{"ノ","노"},
        {"ハ","하"},{"ヒ","히"},{"フ","후"},{"ヘ","헤"},{"ホ","호"},
        {"バ","바"},{"ビ","비"},{"ブ","부"},{"ベ","베"},{"ボ","보"},
        {"パ","파"},{"ピ","피"},{"プ","푸"},{"ペ","페"},{"ポ","포"},
        {"マ","마"},{"ミ","미"},{"ム","무"},{"メ","메"},{"モ","모"},
        {"ヤ","야"},{"ユ","유"},{"ヨ","요"},{"ラ","라"},{"リ","리"},{"ル","루"},{"レ","레"},{"ロ","로"},
        {"ワ","와"},{"ヰ","위"},{"ヱ","웨"},{"ヲ","오"},{"ヴ","브"},
        {"ァ","아"},{"ィ","이"},{"ゥ","우"},{"ェ","에"},{"ォ","오"},{"ャ","야"},{"ュ","유"},{"ョ","요"},{"ヮ","와"},{"ヵ","카"},{"ヶ","케"},
        {"キャ","캬"},{"キュ","큐"},{"キョ","쿄"},{"ギャ","갸"},{"ギュ","규"},{"ギョ","교"},
        {"シャ","샤"},{"シュ","슈"},{"シェ","셰"},{"ショ","쇼"},{"ジャ","자"},{"ジュ","주"},{"ジェ","제"},{"ジョ","조"},
        {"チャ","차"},{"チュ","추"},{"チェ","체"},{"チョ","초"},{"ヂャ","자"},{"ヂュ","주"},{"ヂョ","조"},
        {"ニャ","냐"},{"ニュ","뉴"},{"ニョ","뇨"},{"ヒャ","햐"},{"ヒュ","휴"},{"ヒョ","효"},
        {"ビャ","뱌"},{"ビュ","뷰"},{"ビョ","뵤"},{"ピャ","퍄"},{"ピュ","퓨"},{"ピョ","표"},
        {"ミャ","먀"},{"ミュ","뮤"},{"ミョ","묘"},{"リャ","랴"},{"リュ","류"},{"リョ","료"},
        {"イェ","예"},{"ウィ","위"},{"ウェ","웨"},{"ウォ","워"},{"クァ","콰"},{"クィ","퀴"},{"クェ","퀘"},{"クォ","쿼"},{"グァ","과"},
        {"ツァ","차"},{"ツィ","치"},{"ツェ","체"},{"ツォ","초"},{"ティ","티"},{"テュ","튜"},{"トゥ","투"},{"ディ","디"},{"デュ","듀"},{"ドゥ","두"},
        {"ファ","파"},{"フィ","피"},{"フェ","페"},{"フォ","포"},{"フュ","퓨"},{"ヴァ","바"},{"ヴィ","비"},{"ヴェ","베"},{"ヴォ","보"},{"ヴュ","뷰"}
    };return table;
}
}
QString kanaToHangul(const QString &input){
    QString text=input.normalized(QString::NormalizationForm_KC);
    for(auto &c:text)if(c.unicode()>=0x3041 && c.unicode()<=0x3096)c=QChar(c.unicode()+0x60);
    QString result;bool wordStart=true;
    for(qsizetype i=0;i<text.size();++i){
        const auto ch=text[i];const auto next=i+1<text.size()?text[i+1]:QChar();
        if(ch==QChar(0x30f3)){
            const int final=QString("バビブベボパピプペポマミムメモ").contains(next)?16:QString("カキクケコガギグゲゴ").contains(next)?21:4;
            if(!attachFinal(result,final))result+="응";wordStart=false;continue;
        }
        if(ch==QChar(0x30c3)){
            const int final=QString("カキクケコガギグゲゴ").contains(next)?1:QString("パピプペポ").contains(next)?17:19;
            if(!attachFinal(result,final))result+="ㅅ";wordStart=false;continue;
        }
        if(ch==QChar(0x30fc) && !result.isEmpty()){
            const auto c=result.back().unicode();
            if(c>=0xac00 && c<=0xd7a3){static const QStringList vowels={"아","애","야","얘","어","에","여","예","오","와","왜","외","요","우","워","웨","위","유","으","의","이"};result+=vowels[(c-0xac00)/28%21];continue;}
        }
        auto found=moras().constFind(text.mid(i,2));int consumed=2;
        if(found==moras().cend()){found=moras().constFind(QString(ch));consumed=1;}
        if(found!=moras().cend()){
            QString syllable=found.value();
            // A word-initial Japanese /k/ is approximated with Korean ㄱ:
            // コンニチワ → 곤니치와. Medial /k/ retains ㅋ.
            if(wordStart && (syllable[0].unicode()-0xac00)/588==15)syllable[0]=QChar(syllable[0].unicode()-15*588);
            result+=syllable;i+=consumed-1;wordStart=false;
        }else{result+=ch;wordStart=ch.isSpace() || ch.isPunct();}
    }
    return result;
}

struct JapaneseReading::Impl {
    QLibrary library;
    void *tagger=nullptr;
    using Create=void*(*)(int,char**);using Destroy=void(*)(void*);using Parse=const char*(*)(void*,const char*);using Error=const char*(*)(void*);
    Destroy destroy=nullptr;Parse parse=nullptr;QString error;
    mutable QMutex mutex;
    ~Impl(){if(tagger && destroy)destroy(tagger);}
};
JapaneseReading::JapaneseReading(const QString &runtime):d(std::make_unique<Impl>()){
    const QString root=runtime.isEmpty()?QCoreApplication::applicationDirPath()+"/runtime":runtime;
#ifdef Q_OS_WIN
    d->library.setFileName(QDir(root).filePath("reading/mecab.dll"));
#else
    const auto bundled=QDir(root).filePath("reading/libmecab.so");d->library.setFileName(QFileInfo::exists(bundled)?bundled:runtime.isEmpty()?QString("mecab"):bundled);
#endif
    const auto create=reinterpret_cast<Impl::Create>(d->library.resolve("mecab_new"));
    d->destroy=reinterpret_cast<Impl::Destroy>(d->library.resolve("mecab_destroy"));
    d->parse=reinterpret_cast<Impl::Parse>(d->library.resolve("mecab_sparse_tostr"));
    if(!create || !d->destroy || !d->parse){d->error="일본어 독음 엔진을 찾을 수 없습니다.";return;}
    QList<QByteArray> args={"luma","-F","%m\t%f[8]\n","-U","%m\t%m\n","-E",""};
    const auto dictionary=QDir(root).filePath("reading/ipadic");
    if(QFileInfo::exists(dictionary+"/sys.dic")){args.append({"-r",QDir(root).filePath("reading/mecabrc").toUtf8(),"-d",dictionary.toUtf8()});}
    else if(!runtime.isEmpty()) {d->error="일본어 독음 사전을 찾을 수 없습니다.";return;}
    QList<char*> argv;for(auto &arg:args)argv.append(arg.data());d->tagger=create(int(argv.size()),argv.data());
    if(!d->tagger){const auto error=reinterpret_cast<Impl::Error>(d->library.resolve("mecab_strerror"));d->error="일본어 독음 사전을 열 수 없습니다.";if(error)d->error+=" "+QString::fromUtf8(error(nullptr));}
}
JapaneseReading::~JapaneseReading()=default;
bool JapaneseReading::available()const{return d->tagger!=nullptr;}
QString JapaneseReading::error()const{return d->error;}
QString JapaneseReading::reading(const QString &text)const{
    if(!available() || text.isEmpty() || text.size()>8192)return {};
    QString clean=text;clean.remove(QRegularExpression("<[^>]*>"));
    if(!std::any_of(clean.cbegin(),clean.cend(),[](QChar c){return kana(c) || (c.unicode()>=0x4e00 && c.unicode()<=0x9fff);}))return {};
    QMutexLocker lock(&d->mutex);const auto utf8=clean.toUtf8();const auto *output=d->parse(d->tagger,utf8.constData());if(!output)return {};
    QString result;bool converted=false;
    for(const auto &line:QString::fromUtf8(output).split('\n')){
        const auto fields=line.split('\t');if(fields.size()<2)continue;
        const auto phonetic=fields[1].isEmpty() || fields[1]=="*"?fields[0]:fields[1];
        const auto hangul=kanaToHangul(phonetic);if(hangul!=phonetic)converted=true;
        if(!result.isEmpty() && !fields[0].isEmpty() && !fields[0][0].isPunct())result+=' ';
        result+=hangul;
    }
    return converted?result:QString{};
}
QString JapaneseReading::annotate(const QString &text)const{const auto pronunciation=reading(text);return pronunciation.isEmpty()?text:text+'\n'+pronunciation;}
}
