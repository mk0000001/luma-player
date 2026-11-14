#include "disc_menu_preview.h"

#if defined(Q_OS_WIN) && defined(_MSC_VER) && __has_include(<winrt/Windows.Graphics.Capture.h>)
#define LUMA_WINDOW_CAPTURE
#include <windows.h>
#include <d3d11.h>
#include <roapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#endif

namespace luma {
#ifdef LUMA_WINDOW_CAPTURE
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

struct DiscMenuPreview::Capture {
    struct Apartment {
        HRESULT result = RoInitialize(RO_INIT_SINGLETHREADED);
        ~Apartment() { if (SUCCEEDED(result)) RoUninitialize(); }
    } apartment;
    WId window;
    winrt::Windows::Graphics::SizeInt32 size{};
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    IDirect3DDevice graphicsDevice{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    com_ptr<ID3D11Texture2D> staging;
    QImage latest;

    explicit Capture(WId target) : window(target) {
        if (apartment.result != RPC_E_CHANGED_MODE) check_hresult(apartment.result);
        check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            device.put(), nullptr, context.put()));
        const auto factory = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem item{nullptr};
        check_hresult(factory->CreateForWindow(reinterpret_cast<HWND>(target),
            guid_of<GraphicsCaptureItem>(), put_abi(item)));
        com_ptr<IInspectable> inspectable;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(device.as<IDXGIDevice>().get(), inspectable.put()));
        graphicsDevice = inspectable.as<IDirect3DDevice>();
        size = item.Size();
        if (size.Width < 1 || size.Height < 1 || size.Width > 8192 || size.Height > 8192)
            return;
        pool = Direct3D11CaptureFramePool::CreateFreeThreaded(graphicsDevice,
            DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        session = pool.CreateCaptureSession(item);
        session.IsCursorCaptureEnabled(false);
        session.StartCapture();
    }

    ~Capture() {
        try {
            if (session) session.Close();
            if (pool) pool.Close();
        } catch (const hresult_error &) {}
    }

    QImage read() {
        if (!pool) return {};
        auto frame = pool.TryGetNextFrame();
        if (!frame) return latest;
        if (auto newer = pool.TryGetNextFrame()) frame = std::move(newer);
        // Resizes can leave a frame whose content differs from the buffer.
        const auto content = frame.ContentSize();
        if (content.Width != size.Width || content.Height != size.Height) {
            frame.Close();
            if (content.Width < 1 || content.Height < 1 || content.Width > 8192 || content.Height > 8192)
                return {};
            latest = {};
            staging = nullptr;
            size = content;
            pool.Recreate(graphicsDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
            return {};
        }
        const auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        com_ptr<ID3D11Texture2D> texture;
        check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
        if (!staging) {
            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            description.Usage = D3D11_USAGE_STAGING;
            description.BindFlags = 0;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            description.MiscFlags = 0;
            check_hresult(device->CreateTexture2D(&description, nullptr, staging.put()));
        }
        context->CopyResource(staging.get(), texture.get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
        latest = QImage(static_cast<const uchar *>(mapped.pData), size.Width, size.Height,
            mapped.RowPitch, QImage::Format_RGB32).scaled(800, 450, Qt::KeepAspectRatio, Qt::SmoothTransformation).copy();
        context->Unmap(staging.get(), 0);
        return latest;
    }
};
#else
struct DiscMenuPreview::Capture {};
#endif

DiscMenuPreview::DiscMenuPreview() = default;
DiscMenuPreview::~DiscMenuPreview() = default;
void DiscMenuPreview::reset() { capture_.reset(); }

QImage DiscMenuPreview::latestFrame(WId window) {
#ifdef LUMA_WINDOW_CAPTURE
    // Respect the host's capture exclusion. The API is scoped to this HWND.
    DWORD affinity = WDA_NONE;
    const auto handle = reinterpret_cast<HWND>(window);
    if (!GetWindowDisplayAffinity(handle, &affinity) || affinity != WDA_NONE || !IsWindowVisible(handle)) {
        reset();
        return {};
    }
    try {
        if (!capture_ || capture_->window != window)
            capture_ = std::make_unique<Capture>(window);
        return capture_->read();
    } catch (const hresult_error &) {
        reset();
    }
#else
    Q_UNUSED(window);
#endif
    return {};
}
}
