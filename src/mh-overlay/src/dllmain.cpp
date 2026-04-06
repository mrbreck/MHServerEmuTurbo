#include <Windows.h>
#include <MinHook.h>
#include "dinput8_proxy.h"
#include "auth.h"
#include "overlay.h"
#include "api_client.h"

static void LoadConfig()
{
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
    wchar_t* slash = wcsrchr(dllPath, L'\\');
    if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash - dllPath) - 1, L"gameoptions.ini");

    wchar_t server[256] = {};
    GetPrivateProfileStringW(L"Overlay", L"ServerBase", L"http://127.0.0.1:8080",
        server, 256, dllPath);
    std::string s; for (wchar_t c : server) if (c) s += (char)c;
    if (!s.empty()) g_serverBase = s;
}

// Worker thread: only D3D overlay needs to wait for the game window
static DWORD WINAPI OverlayThread(LPVOID)
{
    Sleep(3000);
    InstallOverlay();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(nullptr);
        InitProxy();
        LoadConfig();

        // Auth hooks must be installed immediately — the game's login HTTP
        // request fires within the first second of startup, before any
        // delayed worker thread would run.
        MH_Initialize();
        InstallAuthHook();

        // D3D overlay can wait for the game window to be created
        CloseHandle(CreateThread(nullptr, 0, OverlayThread, nullptr, 0, nullptr));
        break;

    case DLL_PROCESS_DETACH:
        RemoveOverlay();
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        FreeProxy();
        break;
    }
    return TRUE;
}
