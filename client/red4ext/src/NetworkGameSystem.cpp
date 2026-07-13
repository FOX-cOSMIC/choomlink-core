#include "NetworkGameSystem.h"

#include "CommandLine.h"
#include "Main.h"
#include "Utils.h"

#include "RED4ext/RTTISystem.hpp"
#include "RED4ext/Scripting/Natives/Generated/EulerAngles.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/TeleportationFacility.hpp"
#include "RED4ext/Scripting/Utils.hpp"
#include "RED4ext/SystemUpdate.hpp"

#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h> // Required, see https://github.com/ValveSoftware/GameNetworkingSockets/issues/171^
#include <steam/steamnetworkingsockets.h>

#include "serverbound/AuthPacketsServerBound.h"
#include "serverbound/WorldPacketsServerBound.h"
#include "clientbound/AuthPacketsClientBound.h"
#include "clientbound/WorldPacketsClientBound.h"

#include <zpp_bits.h>

#include <cmath>

bool NetworkGameSystem::Load()
{
    if (SteamDatagramErrMsg errMsg; !GameNetworkingSockets_Init(nullptr, errMsg))
    {
        SDK->logger->ErrorF(PLUGIN, "Could not initialize GameNetworkingSockets: %s", errMsg);
        return false;
    }

    return true;
}
void NetworkGameSystem::Unload()
{
    GameNetworkingSockets_Kill();
}

bool NetworkGameSystem::ConnectToServer(const std::string& host, uint16_t port)
{
    SDK->logger->InfoF(PLUGIN, "Trying to connect to server at %s:%d", host.c_str(), port);

    if (m_pInterface != nullptr)
    {
        SDK->logger->Warn(PLUGIN, "Trying to connect while already being connected. Aborting");
        return false;
    }

    m_pInterface = SteamNetworkingSockets();

    if (m_pInterface == nullptr)
    {
        SDK->logger->Error(PLUGIN, "Failed to initialize the networking library");
    }

    // Since I don't want to parse the ip manually and support both IP versions, I need to create a string first...
    const auto connection_string_size = host.length() + 2 + 5; // 2 (':' + \0) and 5 (65535)
    const auto connection_string = new char[connection_string_size];
    memset(connection_string, '\0', connection_string_size);
    sprintf_s(connection_string, connection_string_size, "%s:%d", host.c_str(), port);

    SteamNetworkingIPAddr address = {};
    if (!address.ParseString(connection_string))
    {
        SDK->logger->WarnF(PLUGIN, "Failed to parse connection string \"%s\"", connection_string);
        m_pInterface = nullptr; // prevent polling
        return false;
    }

    SteamNetworkingConfigValue_t opt = {};
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
               reinterpret_cast<void*>(ConnectionStatusChangedCallback));

    // m_hConnection = m_pInterface->ConnectToHostedDedicatedServer(identity, 0, 1, &opt);
    m_hConnection = m_pInterface->ConnectByIPAddress(address, 1, &opt);

    if (m_hConnection == k_HSteamNetConnection_Invalid)
    {
        SDK->logger->WarnF(PLUGIN, "Could not create connection for string \"%s\": invalid", connection_string);
        return false;
    }

    return true;
}

void NetworkGameSystem::OnNetworkUpdate(RED4ext::FrameInfo& frame_info, RED4ext::JobQueue& job_queue)
{
    // TODO: make this framerate indepedent, maybe also use multiple UpdateTickGroups.
    if (!m_hasTriedToConnect)
    {
        // We auto-connect on the first tick with the CLI address. We don't connect earlier because the message loop
        // isn't run there yet and we are prone to time out.
        const auto commandLine = GetCommandLineA();
        const auto host = ParseHostFromCommandLine(commandLine);
        const auto port = ParsePortFromCommandLine(commandLine);
        if (host.has_value() && port.has_value())
        {
            ConnectToServer(host.value(), port.value());
        }
        m_hasTriedToConnect = true; // We lie here, to prevent parsing the cli every time.
    }

    if (m_pInterface == nullptr)
    {
        return;
    }

    PollIncomingMessages();
    TrackPlayerPosition(frame_info.deltaTime);

    m_pInterface->RunCallbacks(); // This shall be called in a loop.
}

void NetworkGameSystem::OnRegisterUpdates(RED4ext::UpdateRegistrar* aRegistrar)
{
    // TODO: If we have no connection information passed on the command line, we have no reason to even register.
    IGameSystem::OnRegisterUpdates(aRegistrar);
    aRegistrar->RegisterUpdate(RED4ext::UpdateTickGroup::FrameBegin, this, "NetworkUpdate",
        [this](RED4ext::FrameInfo &frame_info, RED4ext::JobQueue &job_queue) {
            this->OnNetworkUpdate(frame_info, job_queue);
    });
}

void NetworkGameSystem::ConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo)
{
    SDK->logger->InfoF(PLUGIN, "Connection Status Changed (%d): %s", pInfo->m_info.m_eState,
                       pInfo->m_info.m_szEndDebug);

    if (pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected)
    {
        char buf[255];
        DWORD buf_len = 255;
        GetUserNameA(buf, &buf_len);

        SDK->logger->Info(PLUGIN, "Socket connected, authenticating");
        InitAuthServerBound auth_packet = {};
        auth_packet.protocol_version = PROTOCOL_VERSION_CURRENT;
        auth_packet.username = buf;

        // TODO: Maybe we could manage this singleton access better? But then, the Game's GameSystem Container is the owner of "this"
        Red::GetGameSystem<NetworkGameSystem>()->EnqueueMessage(0, auth_packet);
    } else {
        Red::GetGameSystem<NetworkGameSystem>()->FullyConnected = false;
    }
}

template<typename T>
bool NetworkGameSystem::EnqueueMessage(uint8_t channel_id, T content)
{
    auto frame = MessageFrame{};
    frame.channel_id = channel_id;
    content.FillMessageFrame(frame);

    auto [data, out] = zpp::bits::data_out();

    auto zpp_result = out(frame);
    if (zpp::bits::failure(zpp_result))
    {
        // Failed to serialize the frame(!)
        return false;
    }

    zpp_result = out(content);
    if (zpp::bits::failure(zpp_result))
    {
        // Failed to serialize the content
        return false;
    }

    // TODO: derive the send flags from the channel id, i.e. lookup registered channels.
    assert(data.size() < std::numeric_limits<uint32_t>::max());

    const auto result = m_pInterface->SendMessageToConnection(
        m_hConnection, data.data(), static_cast<uint32_t>(data.size()), k_nSteamNetworkingSend_Reliable, nullptr);

    if (result == k_EResultOK)
    {
        return true;
    }

    SDK->logger->ErrorF(PLUGIN, "NetworkGameSystem::EnqueueMessage(%d) => Error %d\n", channel_id, result);
    return false;
}

void NetworkGameSystem::SetEntityPosition(const RED4ext::ent::EntityID entityId, RED4ext::Vector4 worldPosition, float yaw)
{
    const auto entity = Cyberverse::Utils::GetDynamicEntity(entityId);
    // TODO: For most, this is actually a NPCPuppet, for those that aren't, this will crash.

    if (entity.has_value())
    {
        // TODO: Find out if this is an NPCPuppet...
        // AI
        RED4ext::Handle<RED4ext::AICommand> commandRef;
        Red::CallVirtual(this, "TeleportPuppet", commandRef, entity.value(), worldPosition, yaw);
        m_LastTeleportCommand[entityId] = commandRef;

        // random game objects
        RED4ext::EulerAngles angles = { 0.0f, 0.0f, yaw };
        const auto teleportFacility = Red::GetGameSystem<RED4ext::TeleportationFacility>();
        if (!Red::CallVirtual(teleportFacility, "Teleport", entity.value() /*"game object"*/, worldPosition, angles))
        {
            SDK->logger->Warn(PLUGIN, "Failed to teleport");
        }

        // Try: scriptInterface.PushAnimationEvent(n"Jump");
        // if (!Red::CallVirtual(entity.value(), "PushAnimationEvent", "Jump"))
        // {
        //     SDK->logger->Info(PLUGIN, "Could not push jump event.");
        // }
    } else
    {
        SDK->logger->Warn(PLUGIN, "Cannot SetEntityPosition, because the entity hasn't been found");
    }
}


void NetworkGameSystem::PollIncomingMessages()
{
    // TODO: More resonable quit condition.
    while (true)
    {
        ISteamNetworkingMessage* pIncomingMsg = nullptr;
        const auto numMsgs = m_pInterface->ReceiveMessagesOnConnection(m_hConnection, &pIncomingMsg, 1);
        if (numMsgs == 0)
        {
            break;
        }

        if (numMsgs < 0)
        {
            SDK->logger->ErrorF(PLUGIN, "Error polling messages: %d", numMsgs);
            return;
        }

        // TODO: Handle or enqueue the message
        //SDK->logger->InfoF(PLUGIN, "Received a packet with %d bytes", pIncomingMsg->m_cbSize);

        auto [data, in] = zpp::bits::data_in();
        const auto begin = (std::byte*)pIncomingMsg->GetData();
        data.assign(begin, begin + pIncomingMsg->GetSize());

        MessageFrame frame = {};
        if (zpp::bits::failure(in(frame)))
        {
            SDK->logger->Error(PLUGIN, "Faulty packet");
            pIncomingMsg->Release();
            continue;
        }

        // TODO: This should both be more generic probably _AND_ we need to consider how we want to hand this off to C#,
        // given that they may want to control _all_ packet logic. This however depends on how flexible and moddable we
        // want our, e.g. auth handling, to be.
        switch (frame.message_type)
        {
        case EINIT_AUTH_RESULT:
        {
            AuthResultClientBound auth_result_packet = {};
            if (zpp::bits::failure(in(auth_result_packet)))
            {
                SDK->logger->Error(PLUGIN, "Faulty packet: AuthResultClientBound");
                pIncomingMsg->Release();
                continue;
            }

            switch (auth_result_packet.auth_result)
            {
            case EAuthResult_Ok:
                SDK->logger->Info(PLUGIN, "Login accepted");
                FullyConnected = true;
                if (m_hasEnqueuedLoadLastCheckpoint && m_systemRequestsHandler)
                {
                    SDK->logger->Info(PLUGIN, "Loading the savegame");
                    Red::CallVirtual(m_systemRequestsHandler, "LoadLastCheckpoint", false);
                }
                // TODO: Follow-Up action
                break;
            case EAuthResult_ValidationFailed:
                SDK->logger->Warn(PLUGIN, "Login: Validation failed");
                break;
            case EAuthResult_VersionMismatch:
                SDK->logger->Warn(PLUGIN, "Login: Version mismatch");
                break;
            default:
                SDK->logger->ErrorF(PLUGIN, "Unknown auth result: %d", auth_result_packet.auth_result);
            }
        }
        break;

        case eSpawnEntity:
        {
            SpawnEntity spawn_entity = {};
            if (zpp::bits::failure(in(spawn_entity)))
            {
                SDK->logger->Error(PLUGIN, "Faulty packet: SpawnEntity");
                pIncomingMsg->Release();
                continue;
            }

            if (m_networkedEntitiesLookup.contains(spawn_entity.networkedEntityId))
            {
                SDK->logger->WarnF(PLUGIN, "Already have a spawned entity for %llu", spawn_entity.networkedEntityId);
                continue;
            }

            // TODO: separate spawning component
            // TODO: SpawnTransientEntity should return the EntityId for a Map<NetworkedEntityId, LocalEntityId>, especially for further updates.
            SDK->logger->InfoF(PLUGIN, "Spawning entity %llu", spawn_entity.recordId);
            RED4ext::TweakDBID entityName = { spawn_entity.recordId };
            RED4ext::Vector4 worldPosition = { spawn_entity.spawnPosition.x, spawn_entity.spawnPosition.y, spawn_entity.spawnPosition.z, 1.0 };
            RED4ext::Quaternion worldOrientation = { 0.0, 0.0, 0.0, 1.0 };

            RED4ext::ent::EntityID entityId;
            if (!Red::CallVirtual(this, "SpawnTransientEntity", entityId, entityName, worldPosition, worldOrientation))
            {
                SDK->logger->Warn(PLUGIN, "Failed to spawn entity!");
            }

            // TODO: Validation. already contained? Error!
            SDK->logger->InfoF(PLUGIN, "Got Entity Id %llu for networkId %llu", entityId.hash, spawn_entity.networkedEntityId);
            m_networkedEntitiesLookup.insert(std::make_pair(spawn_entity.networkedEntityId, entityId));
        }
        break;

        case eTeleportEntity:
        {
            TeleportEntity teleport = {};
            if (zpp::bits::failure(in(teleport)))
            {
                SDK->logger->Error(PLUGIN, "Faulty packet: TeleportEntity");
                pIncomingMsg->Release();
                continue;
            }

            if (!m_networkedEntitiesLookup.contains(teleport.networkedEntityId))
            {
                SDK->logger->WarnF(PLUGIN, "Entity Teleport packet for unknown networkedEntityId %llu. Map size %d",
                                   teleport.networkedEntityId, m_networkedEntitiesLookup.size());
                break;
            }

            const auto entityId = m_networkedEntitiesLookup[teleport.networkedEntityId];
            const RED4ext::Vector4 worldPosition = { teleport.targetPosition.x, teleport.targetPosition.y, teleport.targetPosition.z, 1.0 };

            // TODO: If teleport flag is set
            //SetEntityPosition(entityId, worldPosition, teleport.yaw);

            const auto entity = Cyberverse::Utils::GetDynamicEntity(entityId);
            if (!entity.has_value())
            {
                SDK->logger->Info(PLUGIN, "Skipping TeleportEntity");
                break;
            }

            const auto positionSource = Cyberverse::Utils::Entity_GetWorldPosition(entity.value());

            if (positionSource.X == 0.0 && positionSource.Y == 0.0 && positionSource.Z == 0.0)
            {
                // TODO: let's pray that this doesn't have a side effect when players DO reside at (0, 0, 0)
                SDK->logger->Warn(PLUGIN, "Server Bug: Teleport Entity without the teleport flag for a fresh spawned entity");
                SetEntityPosition(entityId, worldPosition, teleport.yaw);
                break;
            }

            // Non-puppets (vehicles, items) have no AI locomotion — plain teleport, done.
            bool handledAsNonPuppet = false;
            Red::CallVirtual(this, "TeleportIfNotPuppet", handledAsNonPuppet, entity.value(), worldPosition, teleport.yaw);
            if (handledAsNonPuppet)
            {
                break;
            }

            // Locomotion sync (engine-native): the puppet follows an invisible proxy entity via
            // ONE long-lived AIFollowTargetCommand (matchSpeed adapts walk/run/sprint natively);
            // per network update we only teleport the proxy to the sender's latest position.
            auto& moveState = m_movementState[entityId];

            const auto lagX = teleport.targetPosition.x - positionSource.X;
            const auto lagY = teleport.targetPosition.y - positionSource.Y;
            const auto lagZ = teleport.targetPosition.z - positionSource.Z;
            const auto puppetLag = std::sqrt(lagX * lagX + lagY * lagY + lagZ * lagZ);

            if (!moveState.proxyRequested)
            {
                RED4ext::ent::EntityID proxyId = {};
                Red::CallVirtual(this, "CreateMovementProxy", proxyId, worldPosition);
                moveState.proxyId = proxyId;
                moveState.proxyRequested = true;
                break; // spawning is async; the follow command starts once the proxy resolves
            }

            const auto proxy = Cyberverse::Utils::GetDynamicEntity(moveState.proxyId);
            if (!proxy.has_value())
            {
                break; // still spawning — retried on the next position update
            }

            Red::CallVirtual(this, "MoveProxy", proxy.value(), worldPosition, teleport.yaw);

            if (puppetLag > 8.0f)
            {
                // Desync / spawn placement / vehicle exit: hard-teleport the puppet to catch up;
                // the follow command keeps running and resumes from the new spot.
                SDK->logger->TraceF(PLUGIN, "Entity %llu is %f m off target, teleport catch-up", teleport.networkedEntityId, puppetLag);
                SetEntityPosition(entityId, worldPosition, teleport.yaw);
                break;
            }

            if (!moveState.followIssued)
            {
                RED4ext::Handle<RED4ext::AICommand> commandRef;
                Red::CallVirtual(this, "StartFollowingProxy", commandRef, entity.value(), proxy.value());
                if (commandRef)
                {
                    moveState.followCommand = commandRef;
                    moveState.followIssued = true;
                }
            }
        }
        break;

        case eEntityAction:
        {
            EntityAction entity_action = {};
            if (zpp::bits::failure(in(entity_action)))
            {
                SDK->logger->Error(PLUGIN, "Faulty packet: EntityAction");
                pIncomingMsg->Release();
                continue;
            }

            if (!m_networkedEntitiesLookup.contains(entity_action.networkedEntityId))
            {
                SDK->logger->WarnF(PLUGIN, "EntityAction for unknown networkedEntityId %llu", entity_action.networkedEntityId);
                break;
            }

            const auto entityId = m_networkedEntitiesLookup[entity_action.networkedEntityId];
            const auto entity = Cyberverse::Utils::GetDynamicEntity(entityId);
            if (!entity.has_value())
            {
                break;
            }

            // Unknown action values are ignored inside the redscript handler (forward compatibility).
            Red::CallVirtual(this, "PuppetAction", entity.value(), static_cast<uint32_t>(entity_action.action));
        }
        break;

        case eDestroyEntity:
        {
            DestroyEntity destroy_entity = {};
            if (zpp::bits::failure(in(destroy_entity)))
            {
                SDK->logger->Error(PLUGIN, "Faulty packet: DestroyEntity");
                pIncomingMsg->Release();
                continue;
            }

            if (!m_networkedEntitiesLookup.contains(destroy_entity.networkedEntityId))
            {
                SDK->logger->WarnF(PLUGIN, "Have no spawned entity for %llu", destroy_entity.networkedEntityId);
                continue;
            }

            const auto entityId = m_networkedEntitiesLookup[destroy_entity.networkedEntityId];
            if (!Red::CallVirtual(this, "DestroyTransientEntity", entityId))
            {
                SDK->logger->Warn(PLUGIN, "Failed to destroy entity!");
            }

            // The movement proxy is ours, not the server's — clean it up with its puppet.
            if (const auto it = m_movementState.find(entityId); it != m_movementState.end())
            {
                if (it->second.proxyRequested)
                {
                    Red::CallVirtual(this, "DestroyTransientEntity", it->second.proxyId);
                }
                m_movementState.erase(it);
            }
            m_networkedEntitiesLookup.erase(entityId);
        }
        break;

        default:
            printf("Message Type: %d\n", frame.message_type);
            break;
        }

        pIncomingMsg->Release();
    }
}

bool NetworkGameSystem::OnGameRestored()
{
    const auto res = IGameSystem::OnGameRestored();
    SDK->logger->Info(PLUGIN, "Game restored: We're in the world");
    const auto player = Cyberverse::Utils::GetPlayer();

    // Broken attempts:
    // const auto transform = Cyberverse::Utils::Entity_GetWorldTransform(player);
    // const auto position = Cyberverse::Utils::WorldPosition_ToVector4(transform.Position);
    // SDK->logger->InfoF(PLUGIN, "Player at (%f, %f, %f)", transform.Position.x.Bits, transform.Position.y.Bits,
    // transform.Position.z.Bits);

    const auto position = Cyberverse::Utils::Entity_GetWorldPosition(player);
    SDK->logger->InfoF(PLUGIN, "Player at (%f, %f, %f, %f)", position.X, position.Y, position.Z, position.W);

    PlayerJoinWorld join_packet = {};
    join_packet.position = { position.X, position.Y, position.Z };

    // TODO: Maybe we could manage this singleton access better? But then, the Game's GameSystem Container is the owner
    // of "this"
    Red::GetGameSystem<NetworkGameSystem>()->EnqueueMessage(0, join_packet);

    m_gameRestored = true;
    return res;
}

void NetworkGameSystem::TrackPlayerPosition(float deltaTime)
{
    m_TimeSinceLastPlayerPositionSync += deltaTime;
    if (m_TimeSinceLastPlayerPositionSync < 0.1f /* update rate*/)
    {
        return;
    }

    m_TimeSinceLastPlayerPositionSync = 0.0f;

    const auto player = Cyberverse::Utils::GetPlayer();
    const auto [X, Y, Z, W] = Cyberverse::Utils::Entity_GetWorldPosition(player);
    const auto orientation = Cyberverse::Utils::Entity_GetWorldOrientation(player);
    const auto [Roll, Pitch, Yaw] = Cyberverse::Utils::Quaternion_ToEulerAngles(orientation);

    const PlayerPositionUpdate position_update = { {  X, Y, Z }, Yaw};
    this->EnqueueMessage(1, position_update);
}

