#pragma once

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <unordered_map>

#include <steam/isteamnetworkingsockets.h>
#include <steam/steamnetworkingsockets.h>

#include <zpp_bits.h>

#include "MessageFrame.h"

#include "Behavior.h"

// One fake client: a GameNetworkingSockets connection plus the Cyberverse protocol
// state machine (Connecting -> Authenticating -> Joining -> Running; terminal: Dead).
class BotClient
{
public:
    enum class State
    {
        Connecting,
        Authenticating,
        Joining,
        Running,
        Dead,
    };

    BotClient(int index, ISteamNetworkingSockets* iface, Behavior behavior);
    ~BotClient();

    bool Connect(const SteamNetworkingIPAddr& address);
    void PollIncomingMessages();
    void SendTick(float dt); // behavior update + PlayerPositionUpdate while Running
    void Close(const char* reason);

    State GetState() const { return m_state; }
    const std::string& Name() const { return m_name; }
    uint64_t ConsumeTeleportCount()
    {
        const auto count = m_teleportsReceived;
        m_teleportsReceived = 0;
        return count;
    }

    // GNS delivers status changes through a single static callback; dispatch by handle.
    static void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);

private:
    void OnConnected();
    void HandleMessage(const void* data, size_t size);

    // Identical framing to the real client (NetworkGameSystem::EnqueueMessage):
    // zpp_bits-serialize the MessageFrame, then the content, send reliable.
    template <typename T>
    bool Send(T content)
    {
        auto frame = MessageFrame {};
        T::FillMessageFrame(frame);

        auto [data, out] = zpp::bits::data_out();
        if (zpp::bits::failure(out(frame)) || zpp::bits::failure(out(content)))
        {
            std::fprintf(stderr, "[%s] failed to serialize message type %u\n", m_name.c_str(), frame.message_type);
            return false;
        }

        const auto result = m_iface->SendMessageToConnection(
            m_conn, data.data(), static_cast<uint32_t>(data.size()), k_nSteamNetworkingSend_Reliable, nullptr);
        if (result != k_EResultOK)
        {
            std::fprintf(stderr, "[%s] send failed: %d\n", m_name.c_str(), result);
            return false;
        }
        return true;
    }

    int m_index;
    std::string m_name;
    ISteamNetworkingSockets* m_iface;
    HSteamNetConnection m_conn = k_HSteamNetConnection_Invalid;
    State m_state = State::Connecting;
    Behavior m_behavior;
    uint64_t m_teleportsReceived = 0;

    static std::unordered_map<HSteamNetConnection, BotClient*> s_byConnection;
};
