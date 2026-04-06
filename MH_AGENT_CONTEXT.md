# MH Vaporizer Mod — Project Documentation

## Overview

This project adds a configurable loot vaporizer to the **Marvel Heroes Omega 2.16a** private server
(MHServerEmu). It consists of three components:

1. **MHServerEmuTurbo** — the game server (C#, running on Linux); a fork of the open-source MHServerEmu project
2. **Marvel Heroes Omega 2.16a** — the game client (Windows)
3. **mh-overlay** — a D3D9 in-process overlay DLL injected into the game client (C++, Windows)

> **Critical:** The server and client run on **separate machines**. The server runs on **Linux**.
> The client and overlay run on **Windows**. The overlay communicates with the server over HTTP.

---

## Infrastructure

| Component | Machine | OS | Language |
|---|---|---|---|
| MHServerEmuTurbo server | Remote | Linux | C# (.NET) |
| Marvel Heroes Omega game client | Local | Windows | Unreal Engine 3 |
| mh-overlay DLL | Local (injected into game) | Windows | C++ |

### Overlay → Server Communication

- The overlay connects to the server via HTTP at `http://<ServerBase>/api/gameoptions`
- Default `ServerBase` is `http://127.0.0.1:8080` but **must be changed** since they are on different machines
- The real server address is configured in `gameoptions.ini` (next to the game's `.exe`):

```ini
[Overlay]
ServerBase=http://<server-ip>:8080
```

- Auth uses `email` + `PlatformTicket` (token). The overlay captures these automatically by
  hooking WinINet calls during the game's login sequence — no manual credential entry needed.

---

## Source Repositories

| Repo | Path (Windows dev machine) |
|---|---|
| Server + Overlay | `C:\Users\breck\source\repos\MHServerEmuTurbo\` |

This document lives in the root of that repo. The **mh-overlay** project is at `src\mh-overlay\`.

### Game Files

| Item | Path |
|---|---|
| Game binaries | `C:\Games\Marvel Heroes Omega 2.16a Steam\UnrealEngine3\Binaries\Win64\` |
| Overlay DLL (deployed) | `…\Win64\dinput8.dll` |
| Overlay config | `…\Win64\gameoptions.ini` |

---

## Build Commands

```powershell
# Build server (run on Windows dev machine, deploys to Linux separately)
cd "C:\Users\breck\source\repos\MHServerEmuTurbo"
dotnet build MHServerEmu.sln --nologo -v quiet

# Build overlay
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild "C:\Users\breck\source\repos\MHServerEmuTurbo\src\mh-overlay\mh-overlay.vcxproj" `
    /p:Configuration=Release /p:Platform=x64 /v:minimal

# Deploy overlay (game must not be running)
Copy-Item "C:\Users\breck\source\repos\MHServerEmuTurbo\src\mh-overlay\build\dinput8.dll" `
    "C:\Games\Marvel Heroes Omega 2.16a Steam\UnrealEngine3\Binaries\Win64\dinput8.dll" -Force
```

---

## Server: MHServerEmuTurbo

### Project Structure

```
src/
  MHServerEmu/                  — entry point, commands, server bootstrap
  MHServerEmu.Core/             — shared types, networking, serialization, logging
  MHServerEmu.Frontend/         — raw TCP frontend (game client connections)
  MHServerEmu.Games/            — game logic, entities, loot, powers, regions
  MHServerEmu.PlayerManagement/ — session management, auth, player routing
  MHServerEmu.WebFrontend/      — HTTP API (WebFrontend service)
  MHServerEmu.DatabaseAccess/   — SQLite player persistence
  MHServerEmu.Grouping/         — chat, grouping manager
  MHServerEmu.Leaderboards/     — leaderboard service
  Gazillion/                    — protobuf definitions (generated)
```

### Service Architecture

The server is composed of multiple `IGameService` instances that communicate via an internal
message-passing system (`ServiceMessage`). Messages are structs sent between services using
`ServerManager.Instance.SendMessageToService(GameServiceType.X, message)`.

Key service types:
- `GameServiceType.Frontend` — raw TCP, handles client connections
- `GameServiceType.PlayerManager` — auth, session, routing, player handles
- `GameServiceType.GameInstance` — game simulation (entities, loot, powers)
- `GameServiceType.WebFrontend` — HTTP API for the overlay

### WebFrontend API (overlay-facing)

Registered in `WebFrontendService.cs`. All routes use the same HTTP server on port 8080.

The game options API:

```
GET  /api/gameoptions?email=x&token=y
POST /api/gameoptions
```

Handler: `src/MHServerEmu.WebFrontend/Handlers/GameOptions/GameOptionsWebHandler.cs`
DTOs:    `src/MHServerEmu.WebFrontend/Models/GameOptionsDto.cs`

**GET response** (`GameOptionsResponse`):
```json
{
  "Rarities": [
    { "ProtoId": 12345678, "Name": "Common",   "Tier": 1 },
    { "ProtoId": 23456789, "Name": "Uncommon", "Tier": 2 },
    { "ProtoId": 34567890, "Name": "Rare",     "Tier": 3 },
    { "ProtoId": 45678901, "Name": "Epic",     "Tier": 4 },
    { "ProtoId": 56789012, "Name": "Cosmic",   "Tier": 5 }
  ],
  "VaporizerSlots": [
    { "SlotId": 1,  "RarityId": 45678901 },
    { "SlotId": 18, "RarityId": 0 }
  ]
}
```

**POST body** (`GameOptionsRequest`):
```json
{
  "Email": "user@example.com",
  "Token": "PlatformTicket...",
  "VaporizerSlots": [
    { "SlotId": 1,  "RarityId": 45678901 },
    { "SlotId": 18, "RarityId": 56789012 }
  ]
}
```

A `RarityId` of `0` means vaporization is disabled for that slot.

### Service Message Flow (GameOptions)

```
HTTP GET → GameOptionsWebHandler
         → GameServiceTaskManager.GetGameOptionsAsync()
         → ServiceMessage.GameOptionsGetRequest → PlayerManagerService
         → ServiceMessage.GameOptionsGetGameRequest → GameInstanceService
         → GameServiceMailbox.OnGameOptionsGetGameRequest()
             iterates GameplayOptions.VaporizableSlots
         → ServiceMessage.GameOptionsGetGameResponse → PlayerManagerService
         → ServiceMessage.GameOptionsGetResponse → WebFrontend
         → GameOptionsWebHandler → JSON response
```

SET follows the same path in reverse, calling `SetArmorRarityVaporizeThreshold()` on the player's
`GameplayOptions` and saving.


### Vaporizer Slot Assignments

Slot IDs are `EquipmentInvUISlot` enum values from `LootEnums.cs`.
The vaporizable slots are defined in `GameplayOptions.VaporizableSlots`.

| SlotId | Enum Name    | In-game Label   | Notes |
|--------|--------------|-----------------|-------|
| 1      | Gear01       | Gear 1          | Avatar gear |
| 2      | Gear02       | Gear 2          | Avatar gear |
| 3      | Gear03       | Gear 3          | Avatar gear |
| 4      | Gear04       | Gear 4          | Avatar gear |
| 5      | Gear05       | Gear 5          | Avatar gear |
| 12     | Ring         | Ring            | Avatar gear |
| 10     | Insignia     | Insignia        | Avatar gear |
| 18     | CostumeCore  | Catalyst        | NOT Relic(9) — confirmed via runtime logging |
| 6      | Artifact01   | Team-up Gear 1  | Team-up agent gear |
| 8      | Artifact02   | Team-up Gear 2  | Team-up agent gear |
| 11     | Artifact03   | Team-up Gear 3  | Team-up agent gear |
| 14     | Artifact04   | Team-up Gear 4  | Team-up agent gear |

> **Important gotcha:** The in-game item called "Catalyst" maps to `CostumeCore` (slot 18),
> **not** `Relic` (slot 9). This was determined via runtime diagnostic logging.
> Do not change this back to `Relic`.

### Vaporization Logic (`LootVaporizer.cs`)

`ShouldVaporizeLootResult()` is called from `ItemResolver.ProcessPending()` for every item drop
in `LootContext.Drop` or `LootContext.MissionReward`.

Slot resolution has three fallback levels:
1. `itemProto.GetInventorySlotForAgent(rollForProto)` — avatar equipment table lookup
2. `itemProto.DefaultEquipmentSlot` — baked into item prototype data
3. `GameDataTables.Instance.EquipmentSlotTable.EquipmentUISlotForTeamUp(itemProto, teamUpProto)`
   — player's current team-up agent's equipment inventories

Level 3 is needed because team-up gear items always have `RollFor = AvatarPrototype` (they are
droppable for avatars), so the avatar slot table returns Invalid; and their `DefaultEquipmentSlot`
is also unset in the game data. The team-up agent's equipment inventories correctly map these items
to their Artifact slots.

Key files:
- `src/MHServerEmu.Games/Loot/LootVaporizer.cs` — `ShouldVaporizeLootResult()`
- `src/MHServerEmu.Games/Loot/ItemResolver.cs` — `ProcessPending()` (calls vaporizer)
- `src/MHServerEmu.Games/Entities/Options/GameplayOptions.cs` — threshold storage/retrieval
- `src/MHServerEmu.Games/GameData/Tables/EquipmentSlotTable.cs` — slot lookup, incl. `EquipmentUISlotForTeamUp()`
- `src/MHServerEmu.Core/Network/GameServiceProtocol.cs` — `VaporizerSlot` class, all GameOptions messages

### Auth (PlatformTicket)

Auth for the WebFrontend API uses the player's `email` + `PlatformTicket`. The ticket is issued
during game login (via `SessionManager.TryCreateSession`) and validated by
`SessionManager.VerifyPlatformTicket(email, token, out playerDbId)` in `PlayerManagerServiceMailbox`.


---

## Overlay: mh-overlay

A Win32 DLL injected into the game process via the **dinput8.dll proxy trick** — the game loads
`dinput8.dll` from its own directory before the system path, so placing the DLL there causes it
to load automatically.

### Source Files

| File | Purpose |
|------|---------|
| `dllmain.cpp` | `DllMain` entry, config loading, thread startup |
| `auth.cpp/h` | WinINet hooks to capture login credentials |
| `overlay.cpp/h` | D3D9 Present hook, ImGui rendering, settings worker thread |
| `api_client.cpp/h` | HTTP client (WinHTTP), slot/rarity types, GET/POST logic |
| `dinput8_proxy.cpp/h` | Forwards all dinput8 exports to the real system DLL |

### Startup Sequence

1. **DllMain (DLL_PROCESS_ATTACH)**
   - `InitProxy()` — set up dinput8 forwarding
   - `LoadConfig()` — read `gameoptions.ini` for `ServerBase`
   - `MH_Initialize()` + `InstallAuthHook()` — hook WinINet **immediately** (login fires within 1s)
   - Spawn `OverlayThread` with 3-second delay (waits for D3D device creation)

2. **Auth hooks (auth.cpp)**
   - Hooks: `InternetSetStatusCallback`, `HttpOpenRequestA/W`, `HttpSendRequestA`,
     `InternetReadFileExA`, `InternetCloseHandle`
   - Tags handles whose URL path contains "Login" or "IndexPB"
   - Captures `email` from `LoginDataPB` protobuf body (field 1)
   - Captures `PlatformTicket` from `AuthTicket` response (field 7), skipping the
     `MessagePackageOut` envelope (varint msgId + varint msgSize)
   - Credentials stored in `g_credentials` (email + platformTicket)

3. **Overlay (overlay.cpp)**
   - Hooks D3D9 vtable: `Present` (index 17), `Reset` (index 16)
   - ImGui DX9 backend renders "Game Options" window
   - `SettingsWorker` thread: retries up to 10×1s waiting for auth credentials to be available,
     then calls `ApiGetGameOptions()` to fetch rarities and current thresholds
   - Displays 12 combo boxes (one per vaporizable slot) with rarity options
   - "Save" button calls `ApiSetGameOptions()` with the current UI state

### API Client (api_client.cpp)

Uses **WinHTTP** for all HTTP communication.

`ApiGetGameOptions()`:
- `GET /api/gameoptions?email=x&token=y`
- Parses `Rarities` JSON array → `std::vector<RarityEntry>` (sorted by tier)
- Parses `VaporizerSlots` JSON array → `GameOptions.slotProtoId` map

`ApiSetGameOptions()`:
- `POST /api/gameoptions`
- Body: `{"Email":"...","Token":"...","VaporizerSlots":[{"SlotId":N,"RarityId":N},...] }`

JSON parsing is hand-rolled (no external library). The `VaporizerSlots` array uses objects with
`SlotId` and `RarityId` integer fields (not a dictionary).

### VapSlot Enum (api_client.h)

Must stay in sync with the server's `EquipmentInvUISlot` enum values:

```cpp
enum class VapSlot : int {
    Gear01      = 1,
    Gear02      = 2,
    Gear03      = 3,
    Gear04      = 4,
    Gear05      = 5,
    Artifact01  = 6,   // Team-up Gear 1
    Artifact02  = 8,   // Team-up Gear 2
    Insignia    = 10,
    Artifact03  = 11,  // Team-up Gear 3
    Ring        = 12,
    Artifact04  = 14,  // Team-up Gear 4
    CostumeCore = 18,  // Catalyst (NOT Relic=9)
};
```

### Config File (gameoptions.ini)

Location: same directory as `MarvelHeroes.exe` / `Win64\`

```ini
[Overlay]
ServerBase=http://<linux-server-ip>:8080
```

The default (`http://127.0.0.1:8080`) only works if the server and client are on the same machine,
which they are **not** in this setup.

---

## Game Client Notes

- Marvel Heroes Omega 2.16a, running on Windows via Steam
- Connects to MHServerEmu (Linux) over the standard game protocol (TCP, not HTTP)
- Uses WinINet for its own HTTP-based login flow (not the game protocol)
- The overlay DLL is loaded via the dinput8 proxy trick — no game file modifications needed
- The overlay window is titled "Game Options" and appears as an ImGui window in-game

---

## Common Pitfalls

- **Don't confuse Catalyst with Relic.** The item called "Catalyst" in-game maps to
  `CostumeCore` (slot 18). `Relic` (slot 9) is a different, unused slot for vaporizer purposes.

- **Team-up gear RollFor is always AvatarPrototype.** `TeamUpGearPrototype.IsDroppableForAgent`
  returns `true` for avatars, so loot rolling always uses an avatar as the `rollFor` target.
  The avatar slot table returns `Invalid` for team-up gear. The vaporizer handles this by falling
  back to the player's current team-up agent's equipment inventories.

- **Server and client are on different machines.** The overlay's `ServerBase` in `gameoptions.ini`
  must point to the Linux server's IP address, not localhost.

- **Auth hooks must be installed in DllMain, not a delayed thread.** The game fires its login
  HTTP request within ~1 second of startup.

- **The overlay DLL cannot be replaced while the game is running** — Windows locks the file.
  Always close the game before deploying a new `dinput8.dll`.

- **Server-side slot extensions.** The client protobuf (`NetStructGameplayOptions`) only carries
  Gear01–05 vaporize thresholds positionally. Ring, Insignia, CostumeCore, and all Artifact slots
  are server-side extensions stored in the archive dict and initialized/migrated in `Serialize()`.
  Older saves are automatically migrated on load; `Medal` is removed if present.
