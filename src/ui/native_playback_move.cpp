#include "native_playback_move.h"

#include <QByteArray>
#include <QScopedValueRollback>
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
