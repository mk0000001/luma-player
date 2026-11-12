#include "native_playback_move.h"

#include <QByteArray>
#include <QApplication>
#include <QDialog>
#include <QScopedValueRollback>
#include <QSet>
#include <QVarLengthArray>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace luma {
namespace {
thread_local const QWidget *movingHost = nullptr;
}

bool playbackWindowsMoving(const QWidget *host) {
    return host && host == movingHost;
}

void keepPlaybackOverlayBelowDialogs(QWidget *overlay, QWidget *host) {
#ifdef Q_OS_WIN
    if (!overlay || !host || !overlay->isVisible() || !overlay->internalWinId() ||
        QGuiApplication::platformName() != QStringLiteral("windows"))
        return;

    QSet<HWND> dialogs;
    for (auto *window : QApplication::topLevelWidgets()) {
        if (!window->isVisible() || !window->internalWinId() ||
            (!qobject_cast<QDialog *>(window) && window->windowType() != Qt::Popup))
            continue;
        for (auto *parent = window->parentWidget(); parent; parent = parent->parentWidget()) {
            if (parent == host) {
                dialogs.insert(reinterpret_cast<HWND>(window->internalWinId()));
                break;
            }
        }
    }
    const auto layer = reinterpret_cast<HWND>(overlay->internalWinId());
    HWND lowestDialog = nullptr;
    for (auto below = GetWindow(layer, GW_HWNDNEXT); below; below = GetWindow(below, GW_HWNDNEXT)) {
        if (dialogs.contains(below))
            lowestDialog = below;
    }
    if (lowestDialog) {
        SetWindowPos(layer, lowestDialog, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
    }
#else
    Q_UNUSED(overlay);
    Q_UNUSED(host);
#endif
}

void keepPlaybackOverlayAboveVideo(QWidget *overlay, QWidget *video) {
#ifdef Q_OS_WIN
    if (!overlay || !video || !overlay->isVisible() || !video->isVisible() ||
        !overlay->internalWinId() || !video->internalWinId())
        return;
    const auto layer = reinterpret_cast<HWND>(overlay->internalWinId());
    const auto nativeVideo = reinterpret_cast<HWND>(video->internalWinId());
    const auto owner = GetWindow(nativeVideo, GW_OWNER);
    if (!owner || GetWindow(layer, GW_OWNER) != owner)
        return;
    for (auto previous = GetWindow(layer, GW_HWNDPREV); previous; previous = GetWindow(previous, GW_HWNDPREV)) {
        if (previous == nativeVideo) {
            // Move the video behind its overlay without raising the player over other apps.
            SetWindowPos(nativeVideo, layer, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
            return;
        }
    }
#else
    Q_UNUSED(overlay);
    Q_UNUSED(video);
#endif
}

bool movePlaybackWindows(QWidget *host, const QByteArray &eventType, void *message,
                         qintptr *result, std::initializer_list<QWidget *> layers) {
#ifdef Q_OS_WIN
    if (eventType != "windows_generic_MSG" || playbackWindowsMoving(host))
        return false;

    const auto &event = *static_cast<const MSG *>(message);
    if (event.message != WM_MOVING || !event.lParam || host->isMaximized())
        return false;

    BOOL fullDrag = FALSE;
    if (!SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS, 0, &fullDrag, 0) || !fullDrag)
        return false;

    const auto &target = *reinterpret_cast<const RECT *>(event.lParam);
    RECT current{};
    if (!GetWindowRect(event.hwnd, &current) ||
        target.right - target.left != current.right - current.left ||
        target.bottom - target.top != current.bottom - current.top)
        return false;

    const int dx = target.left - current.left;
    const int dy = target.top - current.top;
    if (!dx && !dy)
        return false;

    struct Placement { HWND handle; int x, y; };
    QVarLengthArray<Placement,4> placements;
    for (auto *layer : layers) {
        if (!layer || !layer->isVisible() || !layer->internalWinId())
            continue;
        const auto handle = reinterpret_cast<HWND>(layer->internalWinId());
        RECT rect{};
        if (GetWindow(handle, GW_OWNER) == event.hwnd && GetWindowRect(handle, &rect))
            placements.append({handle, rect.left + dx, rect.top + dy});
    }
    if (placements.isEmpty())
        return false;
    auto positions = BeginDeferWindowPos(int(placements.size()));
    constexpr UINT flags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                           SWP_NOOWNERZORDER | SWP_NOSENDCHANGING;
    for (const auto &placement : placements) {
        if (!positions)
            break;
        positions = DeferWindowPos(positions, placement.handle, nullptr, placement.x, placement.y, 0, 0, flags);
    }
    // Owned playback windows share an owner and can be moved in one batch.
    // Their owner belongs to a different group, so move it immediately after.
    const QScopedValueRollback guard(movingHost, static_cast<const QWidget *>(host));
    if (!positions || !EndDeferWindowPos(positions)) {
        for (const auto &placement : placements)
            SetWindowPos(placement.handle, nullptr, placement.x, placement.y, 0, 0, flags);
    }

    if (!SetWindowPos(event.hwnd, nullptr, target.left, target.top, 0, 0, flags))
        return false;
    *result = TRUE;
    return true;
#else
    Q_UNUSED(host);
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    Q_UNUSED(layers);
    return false;
#endif
}
}
