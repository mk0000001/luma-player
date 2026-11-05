#pragma once
#include <QtGlobal>
#include <memory>

namespace luma {
// Observe capture transitions from this process, including VLC's input thread.
// Call and destroy on the thread that owns the event loop (the GUI thread).
class NativeCaptureObserver final {
public:
    explicit NativeCaptureObserver(bool enabled=true);
    ~NativeCaptureObserver();
    quint32 serial()const;
    quintptr window()const;
    bool available()const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
