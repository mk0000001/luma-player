#pragma once
#include <QByteArray>
#include <QString>
#include <optional>
#include "core/subtitle.h"
namespace luma {
struct RecognitionTiming {QVector<SubtitleCue> cues;QString alignment;};
// The sidecar contains WAV-relative times and UTF-8 byte ranges. The result
// contains media-relative times and QString/UTF-16 ranges, offset exactly once.
std::optional<RecognitionTiming> parseRecognitionTiming(const QByteArray &json,qint64 requestStartMs,qint64 requestDurationMs);
}
