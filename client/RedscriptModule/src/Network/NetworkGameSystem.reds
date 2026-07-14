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

    // Applies an AnimFeature through the game's own helper (AnimationControllerComponent.
    // ApplyFeature) — the exact code path the base game uses for hit reactions, cover
    // stances and equips. The feature instance itself is built natively in C++ (the
    // locomotion feature classes are not script-exposed).
    public func ApplyAnimFeature(entity: ref<Entity>, inputName: CName, feature: ref<AnimFeature>) {
        let go = entity as GameObject;
        if !IsDefined(go) {
            return;
        }
        AnimationControllerComponent.ApplyFeature(go, inputName, feature);
    }

    // Fires a named external anim event on the puppet's graph (the trigger half of the
    // game's feature+event pattern: ApplyFeature carries the data, PushEvent fires the
    // state transition — the graph's transitions require both, composed simultaneously).
    public func PushPuppetEvent(entity: ref<Entity>, eventName: CName) {
        let go = entity as GameObject;
        if !IsDefined(go) {
            return;
        }
        AnimationControllerComponent.PushEvent(go, eventName);
    }

    // Kinematic per-frame move for remote puppets (AI-free locomotion, phase 4b): the C++
    // interpolation calls this every frame with the next position on the network path.
    public func KinematicMove(entity: ref<Entity>, position: Vector4, yaw: Float) {
        let go = entity as GameObject;
        if !IsDefined(go) {
            return;
        }
        let angles: EulerAngles;
        angles.Yaw = yaw;
        GameInstance.GetTeleportationFacility(go.GetGame()).Teleport(go, position, angles);
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
