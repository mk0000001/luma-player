#include <cstdint>
#include <windows.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace {
constexpr std::uint64_t mib = 1024ull * 1024;
constexpr std::uint64_t gib = 1024 * mib;
constexpr unsigned nvidia = 0x10de;
constexpr unsigned amd = 0x1002;
constexpr unsigned intel = 0x8086;
constexpr int base = 0x10, turbo = 0x20, cuda = 0x01, translation = 0x02;

constexpr int recommendedComponents(std::uint64_t ram, std::uint64_t vram, bool cudaAvailable)
{
    // DXGI reports usable dedicated memory, which can be below a card's nominal capacity.
    const bool supportsTurbo = ram >= 15 * gib && vram >= 4 * gib - 256 * mib;
    const bool supportsTranslation = ram >= 30 * gib && vram >= 12 * gib - 512 * mib;
    return (supportsTurbo ? turbo : base) | (cudaAvailable ? cuda : 0) |
           (supportsTranslation ? translation : 0);
}

static_assert(recommendedComponents(8 * gib, 0, false) == base);
static_assert(recommendedComponents(16 * gib, 4 * gib, false) == turbo);
static_assert(recommendedComponents(16 * gib, 8 * gib, true) == (turbo | cuda));
static_assert(recommendedComponents(32 * gib, 12 * gib, true) == (turbo | cuda | translation));
static_assert(recommendedComponents(31 * gib, 11995 * mib, true) == (turbo | cuda | translation));
static_assert(recommendedComponents(32 * gib, 12 * gib, false) == (turbo | translation));

bool supportsCuda12()
{
    const auto driver = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!driver)
        return false;
    using DriverVersion = int (WINAPI *)(int *);
    const auto getVersion = reinterpret_cast<DriverVersion>(GetProcAddress(driver, "cuDriverGetVersion"));
    int version = 0;
    const bool supported = getVersion && getVersion(&version) == 0 && version >= 12000;
    FreeLibrary(driver);
    return supported;
}
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    MEMORYSTATUSEX memory{sizeof(MEMORYSTATUSEX)};
    if (!GlobalMemoryStatusEx(&memory))
        return 0;

    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))
        return 0;

    std::uint64_t maxDedicated = 0;
    std::uint64_t nvidiaDedicated = 0;
    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(index, adapter.GetAddressOf())))
            break;
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            continue;
        if (description.VendorId != nvidia && description.VendorId != amd && description.VendorId != intel)
            continue;
        const auto dedicated = std::uint64_t(description.DedicatedVideoMemory);
        if (dedicated > maxDedicated)
            maxDedicated = dedicated;
        if (description.VendorId == nvidia && dedicated > nvidiaDedicated)
            nvidiaDedicated = dedicated;
    }
    const bool useCuda = nvidiaDedicated >= 2 * gib && nvidiaDedicated == maxDedicated && supportsCuda12();
    return recommendedComponents(memory.ullTotalPhys, maxDedicated, useCuda);
}
