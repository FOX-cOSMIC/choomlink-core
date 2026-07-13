module Cyberverse.Network.Managers
import Codeware.*

public native class NetworkGameSystem extends IGameSystem {
    //public native func ConnectToServer(host: String, port: Uint16) -> Void;
    native let FullyConnected: Bool;
    native let playerActionTracker: ref<PlayerActionTracker>;
    public native func EnqueueLoadLastCheckpoint(handler: wref<inkISystemRequestsHandler>) -> Void;
    
    public func SpawnTransientEntity(entityName: TweakDBID, worldPosition: Vector4, worldOrientation: Quaternion) -> EntityID {
        let npcSpec = new DynamicEntitySpec();
        //npcSpec.recordID = t"Character.spr_animals_bouncer1_ranged1_omaha_mb";
        npcSpec.recordID = entityName; //t"Character.Panam";
        //npcSpec.appearanceName = n"random"; // TODO

        // Trust the server to properly track the entity state, because otherwise,
        // entities will just disappear for the client and never get back
        // (i.e. being invisible), as the server will only re-spawn them when
        // they have been properly despawned.
        npcSpec.alwaysSpawned = true;
        
        // base\characters\entities\main_npc\panam.ent
        //npcSpec.recordID = t"Vehicle.v_sport2_quadra_type66";
        //npcSpec.appearanceName = n"quadra_type66__basic_bulleat";

        npcSpec.position = worldPosition;
        npcSpec.orientation = worldOrientation;
        npcSpec.persistState = false;
        npcSpec.persistSpawn = false;
        npcSpec.tags = [n"RED4ext"];

        return GameInstance.GetDynamicEntitySystem().CreateEntity(npcSpec);
    }

    public func DestroyTransientEntity(entityId: EntityID) {
        GameInstance.GetDynamicEntitySystem().DeleteEntity(entityId);
    }

    public func TeleportEntity(game: GameInstance, entity: ref<Entity>, position: Vector4, worldOrientation: EulerAngles) {
        // TODO: there is no SetWorldPosition.
        //let entity = GameInstance.GetDynamicEntitySystem().GetEntity(id);
        //let transform = .GetWorldTransform();
        // let worldPosition = WorldTransform.GetWorldPosition(transform);
        // WorldPosition.SetVector4(worldPosition, position);
        // WorldTransform.SetPosition(transform, position);

        // We need GameObjects, not pure Entites.
        //GameInstance.GetTeleportationFacility(game).Teleport(entity as GameObject, position, worldOrientation);
    }

    public func TeleportPuppet(puppet: ref<ScriptedPuppet>, position: Vector4, rotation: Float) -> ref<AICommand> {
        let teleportCommand = new AITeleportCommand();
        teleportCommand.position = position;
        teleportCommand.rotation = rotation;
        teleportCommand.doNavTest = false;

        puppet.GetAIControllerComponent().SendCommand(teleportCommand);
        puppet.GetAIControllerComponent().DisableCollider(); // TODO: Temp - In the future this should be controlled by the server, but currently Judy's just annoying :D 
        puppet.GetAIControllerComponent().ForceTickNextFrame();

        // let attackCommand = new AIMeleeAttackCommand();
        // puppet.GetAIControllerComponent().SendCommand(attackCommand);

        // let weapon = ScriptedPuppet.GetActiveWeapon(puppet);
        // weapon.ShootStraight(true);

        return teleportCommand;
    }

    public func StopAICommand(puppet: ref<ScriptedPuppet>, command: ref<AICommand>) {
        let component = puppet.GetAIControllerComponent();
        if (EnumInt(component.GetCommandState(command)) != EnumInt(AICommandState.Success)) {
            component.CancelCommand(command);
        }
    }

    // Non-puppets (vehicles, items) have no AI locomotion; plain teleport. Returns true if handled.
    public func TeleportIfNotPuppet(entity: ref<Entity>, position: Vector4, yaw: Float) -> Bool {
        let puppet = entity as ScriptedPuppet;
        if IsDefined(puppet) {
            return false;
        }
        let go = entity as GameObject;
        if IsDefined(go) {
            let angles: EulerAngles;
            angles.Yaw = yaw;
            GameInstance.GetTeleportationFacility(go.GetGame()).Teleport(go, position, angles);
        }
        return true;
    }

    // Invisible proxy entity for locomotion sync: the remote puppet FOLLOWS this proxy via a
    // single long-lived AIFollowTargetCommand (the engine's native moving-target mechanism);
    // we only teleport the proxy to each incoming network position.
    // Invisibility trick: a nonexistent appearanceName renders the entity invisible (community-
    // documented). TODO(sustainable asset): replace with a minimal custom .ent proxy template
    // once we ship our own archive — same interface, lighter entity.
    public func CreateMovementProxy(position: Vector4) -> EntityID {
        let spec = new DynamicEntitySpec();
        spec.recordID = t"Character.Judy";
        spec.appearanceName = n"choomlink_invisible_proxy";
        spec.alwaysSpawned = true;
        spec.position = position;
        spec.persistState = false;
        spec.persistSpawn = false;
        spec.tags = [n"ChoomLinkProxy"];
        return GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
    }

    public func MoveProxy(proxy: ref<Entity>, position: Vector4, yaw: Float) {
        let go = proxy as GameObject;
        if !IsDefined(go) {
            return;
        }
        let angles: EulerAngles;
        angles.Yaw = yaw;
        GameInstance.GetTeleportationFacility(go.GetGame()).Teleport(go, position, angles);
    }

    // One command, indefinite engine-native follow (same recipe AMM's companion system uses):
    // matchSpeed adapts walk/run/sprint from the distance to the target, tolerance gives the
    // stop dead-zone, stopWhenDestinationReached=false keeps it running forever.
    public func StartFollowingProxy(entity: ref<Entity>, proxy: ref<Entity>) -> ref<AICommand> {
        let puppet = entity as ScriptedPuppet;
        let target = proxy as GameObject;
        if !IsDefined(puppet) || !IsDefined(target) {
            return null;
        }

        // The proxy must never collide with the world, traffic or players.
        let proxyPuppet = proxy as ScriptedPuppet;
        if IsDefined(proxyPuppet) {
            let proxyAi = proxyPuppet.GetAIControllerComponent();
            if IsDefined(proxyAi) {
                proxyAi.DisableCollider();
            }
        }

        let ai = puppet.GetAIControllerComponent();
        if !IsDefined(ai) {
            return null;
        }

        let command = new AIFollowTargetCommand();
        command.target = target;
        command.desiredDistance = 0.5;
        command.tolerance = 1.0;
        command.stopWhenDestinationReached = false;
        command.movementType = moveMovementType.Walk;
        command.matchSpeed = true;
        command.teleport = false; // desync catch-up is handled by our own 8 m teleport rule
        command.lookAtTarget = target;
        command.removeAfterCombat = false;
        command.ignoreInCombat = true;

        ai.SendCommand(command);
        return command;
    }

    // Relayed remote-player actions (EntityAction packet). Unknown actions are ignored on
    // purpose — forward compatibility for swim/climb/... in later phases.
    public func PuppetAction(entity: ref<Entity>, action: Uint32) {
        if !Equals(action, 0u) { // 0 = jump (EPlayerAction.ActionJump)
            return;
        }

        let puppet = entity as ScriptedPuppet;
        if !IsDefined(puppet) {
            return;
        }

        // Best effort: NPC anim graphs have no scriptable jump (no AI jump command exists in
        // the engine). Push the event anyway — harmless if the graph ignores it; the in-game
        // test decides whether phase 1 jump display needs a different approach.
        AnimationControllerComponent.PushEvent(puppet, n"Jump");
        FTLog(s"[ChoomLink] remote jump for puppet \(EntityID.GetHash(puppet.GetEntityID()))");
    }
}

@addMethod(GameInstance)
public static native func GetNetworkGameSystem() -> ref<NetworkGameSystem>
