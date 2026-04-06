#include "dinput8_proxy.h"
#include <Windows.h>

static HMODULE g_realDInput8 = nullptr;

// The only export most games actually call is DirectInput8Create.
// We forward it (and the COM factory export) to the real DLL.
static FARPROC g_fnDirectInput8Create = nullptr;
static FARPROC g_fnDllCanUnloadNow    = nullptr;
static FARPROC g_fnDllGetClassObject  = nullptr;
static FARPROC g_fnDllRegisterServer  = nullptr;
static FARPROC g_fnDllUnregisterServer= nullptr;

void InitProxy()
{
    wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    wcscat_s(path, L"\\dinput8.dll");
    g_realDInput8 = LoadLibraryW(path);
    if (!g_realDInput8) return;

    g_fnDirectInput8Create  = GetProcAddress(g_realDInput8, "DirectInput8Create");
    g_fnDllCanUnloadNow     = GetProcAddress(g_realDInput8, "DllCanUnloadNow");
    g_fnDllGetClassObject   = GetProcAddress(g_realDInput8, "DllGetClassObject");
    g_fnDllRegisterServer   = GetProcAddress(g_realDInput8, "DllRegisterServer");
    g_fnDllUnregisterServer = GetProcAddress(g_realDInput8, "DllUnregisterServer");
}

void FreeProxy()
{
    if (g_realDInput8) { FreeLibrary(g_realDInput8); g_realDInput8 = nullptr; }
}

// ---------------------------------------------------------------------------
// Exported forwarding stubs
// ---------------------------------------------------------------------------
extern "C" {

HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID* p, LPUNKNOWN u)
{
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    return g_fnDirectInput8Create
        ? reinterpret_cast<Fn>(g_fnDirectInput8Create)(h, v, r, p, u)
        : E_FAIL;
}

HRESULT WINAPI DllCanUnloadNow()
{
    using Fn = HRESULT(WINAPI*)();
    return g_fnDllCanUnloadNow
        ? reinterpret_cast<Fn>(g_fnDllCanUnloadNow)() : S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID r, REFIID i, LPVOID* p)
{
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    return g_fnDllGetClassObject
        ? reinterpret_cast<Fn>(g_fnDllGetClassObject)(r, i, p) : E_FAIL;
}

HRESULT WINAPI DllRegisterServer()
{
    using Fn = HRESULT(WINAPI*)();
    return g_fnDllRegisterServer
        ? reinterpret_cast<Fn>(g_fnDllRegisterServer)() : E_FAIL;
}

HRESULT WINAPI DllUnregisterServer()
{
    using Fn = HRESULT(WINAPI*)();
    return g_fnDllUnregisterServer
        ? reinterpret_cast<Fn>(g_fnDllUnregisterServer)() : E_FAIL;
}

} // extern "C"
