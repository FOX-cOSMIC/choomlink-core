# Bot Harness (Cyberverse.BotHarness) — Design

Approved 2026-07-12 (session 1). Headless fake-client tool so a solo developer can test
multiplayer sync and 8-player capacity without human testers.

## V1 scope (decided)

- Connect + auth + world join + movement patterns. NO weapons/shooting (phase 2, needed
  for damage sync #5), no vehicles, no auto-reconnect, no scripting API (YAGNI).
- Success criteria: 8 bots connect to a local server, server logs 8 world joins, bots
  receive each other's position broadcasts (TeleportEntity), stable for 10 minutes.

## Technology (decided)

C++ console tool inside the fork, reusing the exact `shared/protocol` headers, zpp_bits
and GameNetworkingSockets (vcpkg) that the real client uses — serialization identical by
construction, zero protocol drift. Same CMake+Ninja+vcpkg build as `server/Native`.
Neutral name `Cyberverse.BotHarness` so it stays upstream-PR-able.

## Location

`tools/bot-harness/` — own CMake project: `vcpkg.json` (with builtin-baseline, like our
other manifests), `CMakeLists.txt`, `src/`.

## Components

1. **BotClient** (`BotClient.h/.cpp`) — one connection: GNS socket + protocol state
   machine `Connecting → Authenticating → Joining → Running` (terminal: `Dead`).
   - On connected: send `InitAuthServerBound{username, PROTOCOL_VERSION_CURRENT}`.
   - On `AuthResultClientBound{EAuthResult_Ok}`: send `PlayerJoinWorld{spawnPos}` → Running.
   - Running: each send-tick, ask Behavior for position/yaw → `PlayerPositionUpdate` (channel 1).
   - Incoming: `SpawnEntity`/`DestroyEntity` logged immediately; `TeleportEntity` counted
     (stats), not logged per-packet (8 bots × 7 peers × 10 Hz would spam).
   - Framing identical to real client (`NetworkGameSystem.cpp:EnqueueMessage`):
     zpp_bits `out(frame); out(content)`, sent `k_nSteamNetworkingSend_Reliable`.
2. **Behavior** (`Behavior.h`, header-only) — tick-driven movement. Patterns:
   - `circle`: orbit around center, radius 5 m + 1 m × botIndex, angular speed ~walking pace; yaw = tangent.
   - `random`: random-walk (new heading every few seconds), clamped to 30 m around center.
   - Bots spawn distributed around `--center`.
3. **main.cpp** — CLI parse, `GameNetworkingSockets_Init`, create N BotClients, fixed
   tick loop (poll + RunCallbacks every iteration ~15 ms; behavior/send at `--hz`),
   1/s aggregate stats line (per-bot state, teleports received/s), Ctrl+C → close all
   connections cleanly, exit.

Connection-status callback is global in GNS; dispatch to the right BotClient via a
static `HSteamNetConnection → BotClient*` map.

## CLI

```
bot-harness [--server 127.0.0.1] [--port 1337] [--count 8]
            [--pattern circle|random] [--center -2102,446,36] [--hz 10]
```

Defaults chosen so a bare `bot-harness` run reproduces the Stage-A scenario next to the
player's verified position. Usernames `Bot01..BotNN`.

## Error handling

- Failed connect / disconnect / auth failure → log with bot id, state = Dead; process
  continues with remaining bots; exit code 1 if all bots die before Ctrl+C.
- Faulty packet: log + skip (same as real client).

## Testing

No unit tests in v1 (the logic is protocol plumbing; the meaningful test is integration):
run against the real local server and check the success criteria above. This IS the
verification step of the implementation plan.
