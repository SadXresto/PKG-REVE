// Windows-specific window and gamepad code for the NeonDrift demo. Never built
// on Linux; it exists so the portability scan has a concrete Win32/D3D signal.
#include <windows.h>
#include <d3d11.h>
#include <xinput.h>

namespace neondrift {

HWND createNativeWindow(HINSTANCE instance, const wchar_t* title)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = L"NeonDriftWindow";
    RegisterClassExW(&wc);

    return CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
                           nullptr, nullptr, instance, nullptr);
}

bool pollGamepad(DWORD index)
{
    XINPUT_STATE state = {};
    return XInputGetState(index, &state) == ERROR_SUCCESS;
}

ID3D11Device* createDevice()
{
    ID3D11Device* device = nullptr;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1,
                      D3D11_SDK_VERSION, &device, nullptr, nullptr);
    return device;
}

} // namespace neondrift
