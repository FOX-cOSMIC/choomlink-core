#include "BotClient.h"

#include "serverbound/AuthPacketsServerBound.h"
#include "serverbound/WorldPacketsServerBound.h"
#include "clientbound/AuthPacketsClientBound.h"
#include "clientbound/WorldPacketsClientBound.h"

std::unordered_map<HSteamNetConnection, BotClient*> BotClient::s_byConnection;

BotClient::BotClient(const int index, ISteamNetworkingSockets* iface, Behavior behavior)
    : m_index(index), m_iface(iface), m_behavior(behavior)
{
    char name[16];
    std::snprintf(name, sizeof(name), "Bot%02d", index + 1);
    m_name = name;
}

BotClient::~BotClient()
{
    Close("destroyed");
}

bool BotClient::Connect(const SteamNetworkingIPAddr& address)
{
    SteamNetworkingConfigValue_t opt = {};
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
               reinterpret_cast<void*>(OnConnectionStatusChanged));

    m_conn = m_iface->ConnectByIPAddress(address, 1, &opt);
    if (m_conn == k_HSteamNetConnection_Invalid)
    {
        std::fprintf(stderr, "[%s] could not create connection\n", m_name.c_str());
        m_state = State::Dead;
        return false;
    }

    s_byConnection[m_conn] = this;
    return true;
}

void BotClient::OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
{
    const auto it = s_byConnection.find(info->m_hConn);
    if (it == s_byConnection.end())
    {
        return;
    }
    BotClient* bot = it->second;

    switch (info->m_info.m_eState)
    {
    case k_ESteamNetworkingConnectionState_Connected:
        bot->OnConnected();
        break;

    case k_ESteamNetworkingConnectionState_ClosedByPeer:
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        std::fprintf(stderr, "[%s] connection lost (%d): %s\n", bot->m_name.c_str(), info->m_info.m_eState,
                     info->m_info.m_szEndDebug);
        bot->Close("connection lost");
        break;

    default:
        break; // Connecting/FindingRoute etc. — nothing to do
    }
}

void BotClient::OnConnected()
{
    std::printf("[%s] socket connected, authenticating\n", m_name.c_str());

    InitAuthServerBound auth = {};
    auth.username = m_name;
    auth.protocol_version = PROTOCOL_VERSION_CURRENT;
    if (Send(auth))
    {
        m_state = State::Authenticating;
    }
}

void BotClient::PollIncomingMessages()
{
    if (m_state == State::Dead || m_conn == k_HSteamNetConnection_Invalid)
    {
        return;
    }

    while (true)
    {
        ISteamNetworkingMessage* msg = nullptr;
        const auto numMsgs = m_iface->ReceiveMessagesOnConnection(m_conn, &msg, 1);
        if (numMsgs == 0)
        {
            break;
        }
        if (numMsgs < 0)
        {
            std::fprintf(stderr, "[%s] error polling messages: %d\n", m_name.c_str(), numMsgs);
            return;
        }

        HandleMessage(msg->GetData(), msg->GetSize());
        msg->Release();
    }
}

void BotClient::HandleMessage(const void* data, const size_t size)
{
    auto [buffer, in] = zpp::bits::data_in();
    const auto begin = static_cast<const std::byte*>(data);
    buffer.assign(begin, begin + size);

    MessageFrame frame = {};
    if (zpp::bits::failure(in(frame)))
    {
        std::fprintf(stderr, "[%s] faulty packet\n", m_name.c_str());
        return;
    }

    switch (frame.message_type)
    {
    case EINIT_AUTH_RESULT:
    {
        AuthResultClientBound result = {};
        if (zpp::bits::failure(in(result)))
        {
            std::fprintf(stderr, "[%s] faulty packet: AuthResultClientBound\n", m_name.c_str());
            return;
        }

        if (result.auth_result == EAuthResult_Ok)
        {
            const auto [position, yaw] = m_behavior.Spawn();
            std::printf("[%s] login accepted, joining world at (%.1f, %.1f, %.1f)\n", m_name.c_str(), position.x,
                        position.y, position.z);

            PlayerJoinWorld join = {};
            join.position = position;
            if (Send(join))
            {
                m_state = State::Running;
            }
        }
        else
        {
            std::fprintf(stderr, "[%s] login rejected: %d (protocol %u vs server %u)\n", m_name.c_str(),
                         result.auth_result, PROTOCOL_VERSION_CURRENT, result.protocol_version);
            Close("auth rejected");
        }
    }
    break;

    case eSpawnEntity:
    {
        SpawnEntity spawn = {};
        if (zpp::bits::failure(in(spawn)))
        {
            std::fprintf(stderr, "[%s] faulty packet: SpawnEntity\n", m_name.c_str());
            return;
        }
        std::printf("[%s] peer entity spawned: networkId %llu (record %llu) at (%.1f, %.1f, %.1f)\n", m_name.c_str(),
                    static_cast<unsigned long long>(spawn.networkedEntityId),
                    static_cast<unsigned long long>(spawn.recordId), spawn.spawnPosition.x, spawn.spawnPosition.y,
                    spawn.spawnPosition.z);
    }
    break;

    case eTeleportEntity:
    {
        TeleportEntity teleport = {};
        if (zpp::bits::failure(in(teleport)))
        {
            std::fprintf(stderr, "[%s] faulty packet: TeleportEntity\n", m_name.c_str());
            return;
        }
        // counted, not logged — 8 bots x 7 peers x 10 Hz would flood the console
        m_teleportsReceived++;
    }
    break;

    case eDestroyEntity:
    {
        DestroyEntity destroy = {};
        if (zpp::bits::failure(in(destroy)))
        {
            std::fprintf(stderr, "[%s] faulty packet: DestroyEntity\n", m_name.c_str());
            return;
        }
        std::printf("[%s] peer entity destroyed: networkId %llu\n", m_name.c_str(),
                    static_cast<unsigned long long>(destroy.networkedEntityId));
    }
    break;

    default:
        // e.g. eEquipItemEntity — irrelevant for v1
        break;
    }
}

void BotClient::SendTick(const float dt)
{
    if (m_state != State::Running)
    {
        return;
    }

    const auto [position, yaw] = m_behavior.Tick(dt);
    const PlayerPositionUpdate update = { position, yaw };
    Send(update);
}

void BotClient::Close(const char* reason)
{
    if (m_conn != k_HSteamNetConnection_Invalid)
    {
        m_iface->CloseConnection(m_conn, 0, reason, false);
        s_byConnection.erase(m_conn);
        m_conn = k_HSteamNetConnection_Invalid;
    }
    m_state = State::Dead;
}
