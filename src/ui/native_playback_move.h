#pragma once

#include <initializer_list>
#include <QtTypes>

class QByteArray;
class QWidget;

namespace luma {
bool movePlaybackWindows(QWidget *host, const QByteArray &eventType, void *message,
                         qintptr *result, std::initializer_list<QWidget *> layers);
bool playbackWindowsMoving(const QWidget *host);
}
