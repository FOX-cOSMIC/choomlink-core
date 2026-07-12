# Locomotion Sync (upstream #6) — Design

Approved 2026-07-12. Make remote players' movement visible and believable. Milestone DoD
is FULL movement coverage, delivered in phases; each phase is independently testable and
committable.

## Phases

| Phase | Scope | Status |
|---|---|---|
| 1 | Ground locomotion (walk/jog/sprint) + jump | this spec, build now |
| 2 | Swimming | research spike + `PlayerLocomotionState` packet |
| 3 | Climbing / ladders / slides | research spike + same packet family |

Phases 2/3 are explicitly research: they need engine work to trigger those states on NPC
puppets at all. The protocol shape is sketched here so phase 1 doesn't paint us into a
corner; implementation details are decided per-phase.

## Core mechanism (decided): AI-MoveTo puppetry

Today every position update teleports the puppet each frame (AITeleportCommand +
interpolation) — visible sliding, no animations. Instead: give the puppet the engine's
own movement order (`AIMoveToCommand`) toward the latest target; the engine plays real
locomotion animations. Speed class is derived from consecutive position updates
(distance / update interval → Walk / Jog / Sprint). Teleport remains as fallback:
spawn placement, distance > 8 m, or MoveTo failure (stuck puppet). Known unknown:
MoveTo behavior under continuously re-issued targets (every ~100 ms) — first thing to
verify empirically; the fallback path is the safety net.

## Phase-1 changes by component

1. **shared/protocol** — new clientbound packet:
   `EntityAction { uint64 networkedEntityId; uint8 action; Vector3 worldTransform; }`
   (message type appended to EMessageTypeClientbound; `action` mirrors serverbound
   `PlayerAction` enum: jump now, swim/climb/... later). Serverbound side reuses the
   existing `PlayerActionTracked` (type 2).
2. **server/Native** — deserialize `PlayerActionTracked` (if not already) and marshal to
   C#; serialize the new `EntityAction` clientbound.
3. **server/Managed** — handler: on PlayerActionTracked from connection X, look up X's
   networkedEntityId, broadcast `EntityAction` to everyone else (same pattern as the
   position broadcast).
4. **client/red4ext** — replace per-frame teleport interpolation with a per-entity
   movement state: track last target + implied speed; call redscript `MovePuppet(entity,
   target, speedClass)`; re-issue/update commands as new targets arrive; arrival + no
   new update → let AI idle. Teleport fallback per the rule above. New switch case for
   `EntityAction` → redscript `PuppetAction(entity, action)`.
5. **client/RedscriptModule** —
   - `MovePuppet`: AIMoveToCommand, movementType by speed class, `doNavTest = false`,
     update-not-stack command management (cancel/replace like today's StopAICommand).
   - `PuppetAction(jump)`: trigger jump on the puppet (research: PushAnimationEvent vs
     AI jump command — try PushAnimationEvent("Jump") first, it's stubbed in comments).
   - Fix jump SEND: wrap the locomotion state machine's jump event (`JumpEvents.OnEnter`
     stub already in comments) instead of the Jump button release — also fixes the
     upstream-noted bug (keyboard-triggered false positives).
6. **tools/bot-harness** — `--jump-every <seconds>`: bots emit PlayerActionTracked(jump)
   periodically so jump relay is testable without a second human. (Bots ignore incoming
   EntityAction — just count it in stats like teleports.)

## New-packet checklist (first use, keep for the repo)

A new packet touches 5 places: shared header + enum → server/Native switch/marshal →
server/Managed enum/struct/handler → client/red4ext switch → tools/bot-harness (send or
count). Missing one = silent deserialization failure.

## Error handling

- Unknown `action` values: ignore (forward compatibility for phases 2/3).
- MoveTo failure / stuck puppet: teleport fallback after short grace; a visible beam
  beats a frozen enemy.
- No regression rule: if AI-MoveTo proves unusable under 100 ms re-targeting, fall back
  to today's interpolation while keeping the EntityAction/jump work.

## Testing / success criteria (phase 1)

Drivers: bot harness (8 bots circle = walk; faster patterns = jog/sprint; --jump-every =
jumps) + real client observing. PASS when the user, standing in-game next to the bots,
sees: leg animation instead of sliding, plausible speed classes, recognizable jumps.
Server-side packet rates unchanged (560/s at 8 bots). The user's eye is the instrument —
this is a perception feature.
