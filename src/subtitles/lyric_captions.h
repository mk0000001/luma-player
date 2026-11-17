#pragma once
#include "core/subtitle.h"

namespace luma {
bool isSakuraKimiWatashi(QStringView text);
QVector<SubtitleCue> splitTimedLyrics(const QVector<SubtitleCue> &cues,bool tukiBudokan);
}
