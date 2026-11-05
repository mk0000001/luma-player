#include "native_capture_observer.h"
#ifdef Q_OS_WIN
#include <QHash>
#include <windows.h>
#endif

namespace luma {
struct NativeCaptureObserver::State {
    quint32 serial=0;
    quintptr window=0;
#ifdef Q_OS_WIN
    HWINEVENTHOOK hook=nullptr;
    static QHash<HWINEVENTHOOK,State*> &observers(){static QHash<HWINEVENTHOOK,State*> value;return value;}
    static void CALLBACK captureChanged(HWINEVENTHOOK hook,DWORD,HWND window,LONG,LONG,DWORD,DWORD){
        // OUTOFCONTEXT dispatches on the registering GUI thread. Only record;
        // never call Qt, activate a window, or reenter the VLC input thread here.
        if(auto *state=observers().value(hook,nullptr);state && window){++state->serial;state->window=reinterpret_cast<quintptr>(window);}
    }
    explicit State(bool enabled){
        if(enabled)hook=SetWinEventHook(EVENT_SYSTEM_CAPTURESTART,EVENT_SYSTEM_CAPTUREEND,nullptr,&captureChanged,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
        if(hook)observers().insert(hook,this);
    }
    ~State(){if(hook){observers().remove(hook);UnhookWinEvent(hook);}}
#else
    explicit State(bool enabled){Q_UNUSED(enabled);}
#endif
};
NativeCaptureObserver::NativeCaptureObserver(bool enabled):state_(std::make_unique<State>(enabled)){}
NativeCaptureObserver::~NativeCaptureObserver()=default;
quint32 NativeCaptureObserver::serial()const{return state_->serial;}
quintptr NativeCaptureObserver::window()const{return state_->window;}
bool NativeCaptureObserver::available()const{
#ifdef Q_OS_WIN
    return state_->hook!=nullptr;
#else
    return false;
#endif
}
}
