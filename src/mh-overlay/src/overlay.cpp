#include "overlay.h"
#include "auth.h"
#include "api_client.h"
#include <Windows.h>
#include <d3d9.h>
#include <MinHook.h>
#include <vector>
#include <string>
#include <atomic>
#include <algorithm>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx9.h"

#pragma comment(lib, "d3d9.lib")

using FnPresent9 = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using FnReset9   = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

static FnPresent9        g_origPresent9 = nullptr;
static FnReset9          g_origReset9   = nullptr;
static IDirect3DDevice9* g_device9      = nullptr;
static HWND              g_hwnd         = nullptr;
static bool              g_imguiOk      = false;

enum class FetchState : int { Idle = 0, InFlight, Done, Failed };

static std::atomic<FetchState> g_settingsState { FetchState::Idle };
static std::atomic<FetchState> g_saveState     { FetchState::Idle };
static bool                    g_visible = true;

// Rarities: index 0 = Off (disabled, protoId=0), indices 1-5 = Common..Cosmic
static std::vector<RarityEntry> g_rarities;

// Per-slot rarity index (0 = off)
static int g_slotIdx[kNumSlots] = {};

// Pending save payload
static GameOptions g_pendingSave;

static void LaunchThread(LPTHREAD_START_ROUTINE fn) {
    CloseHandle(CreateThread(nullptr, 0, fn, nullptr, 0, nullptr));
}

static DWORD WINAPI SettingsWorker(LPVOID)
{
    auto& creds = GetAuthCredentials();
    for (int attempt = 0; attempt < 10; attempt++) {
        if (attempt > 0) Sleep(1000);
        std::vector<RarityEntry> rarities;
        GameOptions opts;
        if (!ApiGetGameOptions(creds.email, creds.platformTicket, rarities, opts))
            continue;

        // Sort by tier, build list with Off at index 0
        std::sort(rarities.begin(), rarities.end(),
            [](const RarityEntry& a, const RarityEntry& b){ return a.tier < b.tier; });
        g_rarities.clear();
        g_rarities.push_back({0, "Off (disabled)", -1});
        for (auto& r : rarities) g_rarities.push_back(r);

        // Map slot protoIds -> rarity indices
        for (int si = 0; si < kNumSlots; si++) {
            int slotId = (int)kAllSlots[si];
            uint64_t proto = 0;
            auto it = opts.slotProtoId.find(slotId);
            if (it != opts.slotProtoId.end()) proto = it->second;
            g_slotIdx[si] = 0;
            if (proto != 0) {
                for (int ri = 1; ri < (int)g_rarities.size(); ri++) {
                    if (g_rarities[ri].protoId == proto) { g_slotIdx[si] = ri; break; }
                }
            }
        }
        g_settingsState.store(FetchState::Done, std::memory_order_release);
        return 0;
    }
    g_settingsState.store(FetchState::Failed, std::memory_order_release);
    return 0;
}

static DWORD WINAPI SaveWorker(LPVOID)
{
    auto& creds = GetAuthCredentials();
    bool ok = ApiSetGameOptions(creds.email, creds.platformTicket, g_pendingSave);
    g_saveState.store(ok ? FetchState::Done : FetchState::Failed, std::memory_order_release);
    return 0;
}

static void TickFetches()
{
    FetchState ss = g_settingsState.load(std::memory_order_acquire);
    if (ss == FetchState::Idle && GetAuthCredentials().ready) {
        g_settingsState.store(FetchState::InFlight, std::memory_order_release);
        LaunchThread(SettingsWorker);
    } else if (ss == FetchState::Failed) {
        static int cd = 180;
        if (--cd <= 0) { cd = 180; g_settingsState.store(FetchState::Idle, std::memory_order_release); }
    }
}

static bool SettingsReady() { return g_settingsState.load(std::memory_order_acquire) == FetchState::Done; }
static bool SaveInFlight()  { return g_saveState.load(std::memory_order_acquire) == FetchState::InFlight; }
static bool SaveJustDone()  {
    auto s = g_saveState.load(std::memory_order_acquire);
    return s == FetchState::Done || s == FetchState::Failed;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static WNDPROC g_origWndProc = nullptr;
static LRESULT CALLBACK HookedWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return TRUE;
    return CallWindowProcW(g_origWndProc, h, m, w, l);
}

static void DrawRarityCombo(const char* id, int& idx)
{
    const char* cur = (idx >= 0 && idx < (int)g_rarities.size())
        ? g_rarities[idx].name.c_str() : "?";
    ImGui::SetNextItemWidth(140.f);
    if (ImGui::BeginCombo(id, cur)) {
        for (int i = 0; i < (int)g_rarities.size(); i++)
            if (ImGui::Selectable(g_rarities[i].name.c_str(), idx == i))
                idx = i;
        ImGui::EndCombo();
    }
}

static void DrawOverlay()
{
    TickFetches();
    if (GetAsyncKeyState(VK_INSERT) & 1) g_visible = !g_visible;
    if (!g_visible) return;

    ImGui::GetIO().MouseDrawCursor = true;
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::Begin("Game Options", &g_visible,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

    if (!GetAuthCredentials().ready) {
        ImGui::TextColored(ImVec4(1,.6f,0,1), "Waiting for login...");
        std::string log = GetAuthLog().Dump();
        if (!log.empty()) ImGui::TextUnformatted(log.c_str());
        ImGui::End(); return;
    }
    if (!SettingsReady()) {
        ImGui::Text("Loading settings...");
        std::string log = GetAuthLog().Dump();
        if (!log.empty()) ImGui::TextUnformatted(log.c_str());
        ImGui::End(); return;
    }

    int prev[kNumSlots];
    memcpy(prev, g_slotIdx, sizeof(prev));

    for (int si = 0; si < kNumSlots; si++) {
        ImGui::Text("%-18s", SlotDisplayName(kAllSlots[si]));
        ImGui::SameLine();
        std::string id = "##slot" + std::to_string(si);
        DrawRarityCombo(id.c_str(), g_slotIdx[si]);
    }

    ImGui::Spacing();
    static bool s_dirty = false;
    for (int si = 0; si < kNumSlots; si++)
        if (g_slotIdx[si] != prev[si]) s_dirty = true;

    if (SaveInFlight()) {
        ImGui::TextColored(ImVec4(.5f,.5f,.5f,1), "Saving...");
    } else {
        if (SaveJustDone()) {
            bool ok = g_saveState.load(std::memory_order_acquire) == FetchState::Done;
            if (!ok) ImGui::TextColored(ImVec4(1,0,0,1), "Save failed");
            g_saveState.store(FetchState::Idle, std::memory_order_release);
        }
        if (s_dirty) {
            ImGui::TextColored(ImVec4(1,1,0,1), "Unsaved changes");
            if (ImGui::Button("Save")) {
                g_pendingSave.slotProtoId.clear();
                for (int si = 0; si < kNumSlots; si++) {
                    int slotId = (int)kAllSlots[si];
                    uint64_t proto = (g_slotIdx[si] > 0 && g_slotIdx[si] < (int)g_rarities.size())
                        ? g_rarities[g_slotIdx[si]].protoId : 0;
                    g_pendingSave.slotProtoId[slotId] = proto;
                }
                g_saveState.store(FetchState::InFlight, std::memory_order_release);
                LaunchThread(SaveWorker);
                s_dirty = false;
            }
        } else {
            ImGui::TextDisabled("All changes saved");
        }
    }
    ImGui::End();
}

static HRESULT WINAPI HookedPresent9(IDirect3DDevice9* dev, const RECT* src, const RECT* dst,
    HWND wnd, const RGNDATA* dirty)
{
    if (!g_imguiOk) {
        g_device9 = dev;
        D3DDEVICE_CREATION_PARAMETERS cp{};
        dev->GetCreationParameters(&cp);
        g_hwnd = cp.hFocusWindow;
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(g_hwnd);
        ImGui_ImplDX9_Init(dev);
        g_origWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        g_imguiOk = true;
    }
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawOverlay();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    return g_origPresent9(dev, src, dst, wnd, dirty);
}

static HRESULT WINAPI HookedReset9(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
{
    if (g_imguiOk) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_origReset9(dev, pp);
    if (g_imguiOk && SUCCEEDED(hr)) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

bool InstallOverlay()
{
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return false;
    WNDCLASSEXW wc{sizeof(wc), CS_HREDRAW|CS_VREDRAW, DefWindowProcW, 0, 0,
        GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"MHOverlayD9", nullptr};
    RegisterClassExW(&wc);
    HWND hw = CreateWindowExW(0, L"MHOverlayD9", L"", WS_OVERLAPPEDWINDOW,
        0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hw) { d3d->Release(); return false; }
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.BackBufferFormat = D3DFMT_UNKNOWN;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hw,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr) || !dev)
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, hw,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr) || !dev) {
        d3d->Release(); DestroyWindow(hw); UnregisterClassW(L"MHOverlayD9", wc.hInstance);
        return false;
    }
    void** vtable = *reinterpret_cast<void***>(dev);
    void* presentFn = vtable[17], *resetFn = vtable[16];
    dev->Release(); d3d->Release();
    DestroyWindow(hw); UnregisterClassW(L"MHOverlayD9", wc.hInstance);
    if (MH_CreateHook(presentFn, (void*)HookedPresent9, (void**)&g_origPresent9) != MH_OK) return false;
    if (MH_CreateHook(resetFn,   (void*)HookedReset9,   (void**)&g_origReset9)   != MH_OK) return false;
    MH_EnableHook(presentFn); MH_EnableHook(resetFn);
    return true;
}

void RemoveOverlay()
{
    if (!g_imguiOk) return;
    SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
    ImGui_ImplDX9_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    g_imguiOk = false;
}
