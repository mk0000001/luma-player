#pragma once
#include <QString>
#include <memory>

namespace luma {
QString kanaToHangul(const QString &kana);
class JapaneseReading {
public:
    explicit JapaneseReading(const QString &runtime = {});
    ~JapaneseReading();
    bool available() const;
    QString error() const;
    QString reading(const QString &text) const;
    QString annotate(const QString &text) const;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
