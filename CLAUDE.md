# ChoomLink Core — Claude Code instructions

Fork of [TDUniverse/Cyberverse](https://github.com/TDUniverse/Cyberverse) (MIT). Project home / planning docs: `..\choomlink` (see its `START_HERE.md` and `RESEARCH.md`).

## What this is

GTA-Online-style shared-world multiplayer for Cyberpunk 2077. Near-term target: **8 concurrent players**, first target experience: **PvP combat that works well** (locomotion → weapon → damage/health sync, upstream issues #6/#4/#5).

## Ground rules

- **Never read tiltedphoques/CyberpunkMP core source** — its license forbids studying it for a competing multiplayer product. Only its 3 MIT dirs are safe (details in `..\choomlink\RESEARCH.md`).
- **No game binaries/assets in the repo.** No paths into the game install committed.
- Generic fixes (build breaks, sync primitives) should be PR'd upstream to TDUniverse/Cyberverse where practical; divergent GTA-freeroam direction stays here.
- Verify community claims empirically before building on them.
- Issue tracking: **beans** (`beans list` / `beans create`), data lives in `..\choomlink`. Backlog follows the combat dependency chain.

## Architecture (upstream's 4 projects)

| Part | Tech | Path |
|---|---|---|
| Server.Native | C++ (GameNetworkingSockets) | `server\Native` |
| Server.Managed | C# (net9.0) — game logic, plugins | `server\Managed` |
| Client.RED4extModule | C++ RED4ext plugin | `client\red4ext` |
| Client.Redscript | redscript game-logic layer | `client\redscript` |
| Shared protocol | C++ headers (zpp_bits serialization) | `shared\protocol` |

New packets currently require touching: protocol headers + Server.Native switch-cases + Server.Managed enums/structs + client module.

## Building (Windows)

- **Server.Managed:** `dotnet build server\Managed` (needs .NET SDK 9+; SDK 10.0.301 installed).
- **C++ (Server.Native, client module):** CMake + Ninja + MSVC (VS 2022 Build Tools) + **standalone vcpkg** at `C:\Users\G4M3R\Programming\vcpkg` (`VCPKG_ROOT`). GNS comes from vcpkg (manifest mode; `builtin-baseline` added in our fork — candidate upstream PR). Configure with the vcpkg toolchain file; run inside a `vcvars64.bat` environment.
- Docker server image is reported broken upstream (#14/#22) — run the server bare via `dotnet run`.

## Game / client environment

- Steam install: `C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077` — **live patch, never downgrade** (decision §7.9 in START_HERE).
- Upstream README claims game v2.1; actual compatibility with the live patch is an open question — porting to current is milestone `choomlink-esjd` if needed.
- Client deps: RED4ext + redscript + Codeware (staged zips: `C:\Users\G4M3R\Programming\cp2077-mod-staging`).
- Toolchain/version record: `..\choomlink\docs\ENVIRONMENT.md`.
