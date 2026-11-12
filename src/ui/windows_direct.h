#pragma once

#include <QList>
#include <QString>
#include <memory>
#include <optional>

namespace luma {

struct WindowsDirectBinding {
    quint64 adapter = 0;
    quint32 address = 0;
    int prefixLength = 0;
    quint32 interfaceIndex = 0;
    bool operator==(const WindowsDirectBinding &) const = default;
};

std::optional<WindowsDirectBinding> newWindowsDirectBinding(const QList<WindowsDirectBinding> &before,
                                                             const QList<WindowsDirectBinding> &current);

class WindowsDirectConnection final {
public:
    enum class Phase { Starting, Started, Failed };
    struct Status {
        Phase phase = Phase::Starting;
        std::optional<WindowsDirectBinding> binding;
        bool bluetooth = false;
    };

    WindowsDirectConnection();
    ~WindowsDirectConnection();
    static bool available();
    bool start();
    Status poll();
    const QString &ssid() const;
    const QString &passphrase() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
