# MHServerEmuTurbo

A fork of [MHServerEmu](https://github.com/Crypto137/MHServerEmu) — the server emulator for
**Marvel Heroes Omega 2.16a** — focused on quality-of-life improvements and grind reduction.

The goal is to make the private server experience more enjoyable by removing the tedious parts
of the original game while keeping the core gameplay intact.

## What's Different from Upstream

### Current Features

- **Loot Vaporizer** — automatically destroys loot below a configurable rarity threshold,
  per equipment slot. Configured per-player via the in-game overlay (`src/mh-overlay`).

### Planned / In Progress

- More QoL settings exposed through the overlay UI
- Additional grind-reduction options

## Components

| Component | Description |
|---|---|
| `src/` | Server source (C#, .NET) — runs on Linux |
| `src/mh-overlay/` | In-game D3D9 overlay DLL (C++) — runs on Windows, injected into the game client |

The overlay communicates with the server over HTTP. See [`src/mh-overlay/README.md`](src/mh-overlay/README.md)
for setup and build instructions specific to the overlay.

## Building

### Server

```powershell
dotnet build MHServerEmu.sln --nologo -v quiet
```

Or use the provided build scripts:

- **Windows:** `Build.bat`
- **Linux:** `BuildLinux.bat`

### Overlay

See [`src/mh-overlay/README.md`](src/mh-overlay/README.md) for full instructions. Short version:

```powershell
cd src\mh-overlay
.\build.ps1
```

Output is `src/mh-overlay/build/dinput8.dll`. Copy it to your game's `Win64\` directory.

## Setup

This fork requires the same base setup as upstream MHServerEmu. Refer to the
[upstream setup guide](https://github.com/Crypto137/MHServerEmu/blob/master/docs/Setup/InitialSetup.md)
to get the server running first, then apply the changes from this repo.

The server and game client run on **separate machines** in the expected configuration:

| Component | Machine | OS |
|---|---|---|
| Server | Remote | Linux |
| Game client + overlay | Local | Windows |

The overlay's `ServerBase` must be set to the Linux server's IP address in
`gameoptions.ini` (next to `MarvelHeroesOmega.exe`):

```ini
[Overlay]
ServerBase=http://<server-ip>:8080
```

## Upstream

This project is built on top of [MHServerEmu](https://github.com/Crypto137/MHServerEmu)
by Crypto137 and contributors. All credit for the base server emulation goes to them.
Changes in this fork are tracked in the commit history.
