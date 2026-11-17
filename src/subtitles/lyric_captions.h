#pragma once
#include "core/subtitle.h"

namespace luma {
bool isSakuraKimiWatashi(const QString &text);
QVector<SubtitleCue> splitTimedLyrics(const QVector<SubtitleCue> &cues,bool tukiBudokan);
}
