#include "platform_features.h"
#include <QEvent>
#include <QTimer>
#include <QSet>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif
namespace luma {
QJsonArray videoAdapters(){
    QJsonArray result;
#ifdef Q_OS_WIN
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return result;
    QSet<QString> selectableNames;
    for(UINT i=0;;++i){
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND)break;
        if(!adapter)continue;
        DXGI_ADAPTER_DESC1 desc{};if(FAILED(adapter->GetDesc1(&desc)) || desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
        const auto name=QString::fromWCharArray(desc.Description);
        // mpv selects D3D11 adapters by name. Repeated DXGI entries with that
        // same name cannot be selected independently.
        if(name.isEmpty() || selectableNames.contains(name))continue;
        selectableNames.insert(name);
        result.append(QJsonObject{{"id",name},{"name",name},{"backend","D3D11"},{"dedicatedMemoryMiB",double(desc.DedicatedVideoMemory)/(1024*1024)}});
    }
#endif
    return result;
}
QJsonObject displayCapabilities(WId windowId){
    QJsonObject result{{"hdrActive",false},{"verified",false},{"dolbyVisionCertified",false}};
#ifdef Q_OS_WIN
    const auto monitor=MonitorFromWindow(reinterpret_cast<HWND>(windowId),MONITOR_DEFAULTTONEAREST);
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return result;
    for(UINT a=0;;++a){
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;if(factory->EnumAdapters1(a,&adapter)==DXGI_ERROR_NOT_FOUND)break;
        if(!adapter)continue;
        for(UINT o=0;;++o){
            Microsoft::WRL::ComPtr<IDXGIOutput> output;if(adapter->EnumOutputs(o,&output)==DXGI_ERROR_NOT_FOUND)break;
            Microsoft::WRL::ComPtr<IDXGIOutput6> output6;if(!output || FAILED(output.As(&output6)))continue;
            DXGI_OUTPUT_DESC1 desc{};if(FAILED(output6->GetDesc1(&desc)) || desc.Monitor!=monitor)continue;
            result.insert("verified",true);result.insert("display",QString::fromWCharArray(desc.DeviceName));
            DEVMODEW mode{};mode.dmSize=sizeof(mode);if(EnumDisplaySettingsExW(desc.DeviceName,ENUM_CURRENT_SETTINGS,&mode,0))result.insert("refreshRate",int(mode.dmDisplayFrequency));
            result.insert("hdrActive",desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
            result.insert("bitsPerColor",int(desc.BitsPerColor));result.insert("colorSpace",int(desc.ColorSpace));
            result.insert("maxLuminance",desc.MaxLuminance);result.insert("minLuminance",desc.MinLuminance);
            result.insert("maxFullFrameLuminance",desc.MaxFullFrameLuminance);return result;
        }
    }
#else
    Q_UNUSED(windowId);
#endif
    return result;
}
CaptureGuard::CaptureGuard(QObject *parent):QObject(parent){}
void CaptureGuard::watch(QWidget *window){
    if(!window)return;
    window=window->window();for(const auto &item:windows_)if(item==window)return;
    windows_.append(window);window->installEventFilter(this);apply();
}
void CaptureGuard::setEnabled(bool enabled){enabled_=enabled;apply();}
void CaptureGuard::apply(){
#ifdef Q_OS_WIN
    for(const auto &window:windows_)if(window && window->testAttribute(Qt::WA_WState_Created))
        SetWindowDisplayAffinity(reinterpret_cast<HWND>(window->winId()),enabled_?0x11:WDA_NONE);
#endif
}
bool CaptureGuard::applied()const{
#ifdef Q_OS_WIN
    BOOL composed=FALSE;if(!enabled_ || FAILED(DwmIsCompositionEnabled(&composed)) || !composed)return false;
    bool checked=false;
    for(const auto &window:windows_)if(window && window->isVisible()){
        DWORD affinity=0;checked=true;
        if(!GetWindowDisplayAffinity(reinterpret_cast<HWND>(window->winId()),&affinity) || affinity!=0x11)return false;
    }
    return checked;
#else
    return false;
#endif
}
QString CaptureGuard::status()const{
    if(!enabled_)return QStringLiteral("캡처 차단 꺼짐");
#ifdef Q_OS_WIN
    return applied()?QStringLiteral("Windows 캡처 제외 적용 · 인증 DRM/HDCP와 별개"):QStringLiteral("캡처 제외 요청됨 · 적용 확인 실패");
#else
    return QStringLiteral("현재 플랫폼에서 캡처 제외 미지원");
#endif
}
bool CaptureGuard::eventFilter(QObject *object,QEvent *event){
    if(event->type()==QEvent::Show || event->type()==QEvent::WinIdChange || event->type()==QEvent::WindowStateChange)
        QTimer::singleShot(0,this,[this]{apply();});
    return QObject::eventFilter(object,event);
}
}
