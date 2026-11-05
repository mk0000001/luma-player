#pragma once
#include <QtGlobal>

namespace luma {
struct NativeInputSnapshot {
    quint32 serial=0;
    bool foregroundOwned=false;
    bool pointerOwned=false;
    bool blocked=false;
};
class InputActivityTracker {
public:
    bool observe(const NativeInputSnapshot &snapshot){
        // Consume every snapshot, including foreign input, so returning to the
        // player never replays an old capture transition. Serial wrap is valid.
        const bool changed=known_ && snapshot.serial!=previous_;
        known_=true;previous_=snapshot.serial;
        return changed && snapshot.foregroundOwned && snapshot.pointerOwned && !snapshot.blocked;
    }
private:
    quint32 previous_=0;
    bool known_=false;
};
}
