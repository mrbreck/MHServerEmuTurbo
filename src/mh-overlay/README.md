# mh-overlay

General-purpose in-game D3D9 overlay for MHServerEmu. Renders an ImGui UI
inside the running game client and communicates with the server's HTTP API.

Currently includes:
- **Vaporizer settings** — configure per-slot loot vaporizer rarity thresholds

Designed to be extended with additional settings panels, admin tools, or other
server-facing functionality without being tied to any single feature.

## How it works

The DLL is loaded via the **dinput8.dll proxy trick**: drop `dinput8.dll`
(our DLL) next to `MarvelHeroesOmega.exe`. Windows finds it before the
system copy. Our DLL forwards all real DirectInput8 calls to the system DLL,
then injects the overlay.

**Auth** is automatic. The overlay hooks WinINet calls during the game's login
sequence to capture the player's `email` and `PlatformTicket`. No manual
credential entry or config needed.

**In-game controls**
- `Insert` — toggle overlay window visibility
- Combo boxes select vaporizer rarity threshold per equipment slot
- **Save** button POSTs settings to MHServerEmu (`/api/gameoptions`)

## Dependencies (populate manually — no network access)

```
deps/
  imgui/     ← https://github.com/ocornut/imgui   (clone into here)
  minhook/   ← https://github.com/TsudaKageyu/minhook  (clone into here)
```

```powershell
cd <repo-root>\src\mh-overlay\deps
git clone --depth 1 https://github.com/ocornut/imgui.git imgui
git clone --depth 1 https://github.com/TsudaKageyu/minhook.git minhook
```

## Build

Open `mh-overlay.vcxproj` in Visual Studio 2022, set to **Release | x64**, build.
Output: `build\dinput8.dll`.

Or use the build script:

```powershell
.\build.ps1
```

## Install

Copy `build\dinput8.dll` to:
```
C:\Games\Marvel Heroes Omega 2.16a Steam\UnrealEngine3\Binaries\Win64\
```

## Optional config

Create `gameoptions.ini` next to `MarvelHeroesOmega.exe`:
```ini
[Overlay]
ServerBase=http://<linux-server-ip>:8080
```

Default `ServerBase` is `http://127.0.0.1:8080`. If the server and client are
on different machines (the normal setup), this **must** be updated.
