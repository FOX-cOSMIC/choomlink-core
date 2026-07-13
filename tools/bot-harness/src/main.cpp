#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include "Behavior.h"
#include "BotClient.h"

namespace
{
std::atomic<bool> g_stop = false;

void OnSignal(int)
{
    g_stop = true;
}

struct Options
{
    std::string server = "127.0.0.1";
    uint16_t port = 1337;
    int count = 8;
    Behavior::Pattern pattern = Behavior::Pattern::Circle;
    Vector3 center = { -2102.0f, 446.0f, 36.0f }; // verified Stage-A player position
    float hz = 10.0f;
    float jumpEvery = 0.0f; // seconds between jump actions per bot; 0 = never
    float boundaryDistance = 440.0f; // oscillation center distance for the boundary pattern
};

bool ParseArgs(const int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        const auto next = [&]() -> const char* {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "missing value for %s\n", arg.c_str());
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--server")
        {
            const auto value = next();
            if (!value) return false;
            options.server = value;
        }
        else if (arg == "--port")
        {
            const auto value = next();
            if (!value) return false;
            options.port = static_cast<uint16_t>(std::atoi(value));
        }
        else if (arg == "--count")
        {
            const auto value = next();
            if (!value) return false;
            options.count = std::atoi(value);
        }
        else if (arg == "--pattern")
        {
            const auto value = next();
            if (!value) return false;
            if (std::strcmp(value, "circle") == 0)
            {
                options.pattern = Behavior::Pattern::Circle;
            }
            else if (std::strcmp(value, "random") == 0)
            {
                options.pattern = Behavior::Pattern::Random;
            }
            else if (std::strcmp(value, "boundary") == 0)
            {
                options.pattern = Behavior::Pattern::Boundary;
            }
            else
            {
                std::fprintf(stderr, "unknown pattern \"%s\" (circle|random|boundary)\n", value);
                return false;
            }
        }
        else if (arg == "--center")
        {
            const auto value = next();
            if (!value) return false;
            if (sscanf_s(value, "%f,%f,%f", &options.center.x, &options.center.y, &options.center.z) != 3)
            {
                std::fprintf(stderr, "invalid --center \"%s\", expected x,y,z\n", value);
                return false;
            }
        }
        else if (arg == "--hz")
        {
            const auto value = next();
            if (!value) return false;
            options.hz = static_cast<float>(std::atof(value));
        }
        else if (arg == "--jump-every")
        {
            const auto value = next();
            if (!value) return false;
            options.jumpEvery = static_cast<float>(std::atof(value));
        }
        else if (arg == "--distance")
        {
            const auto value = next();
            if (!value) return false;
            options.boundaryDistance = static_cast<float>(std::atof(value));
        }
        else
        {
            std::fprintf(stderr,
                         "usage: bot-harness [--server 127.0.0.1] [--port 1337] [--count 8]\n"
                         "                   [--pattern circle|random|boundary] [--center x,y,z] [--hz 10]\n"
                         "                   [--jump-every <seconds>] [--distance <m, boundary pattern>]\n");
            return false;
        }
    }

    return options.count > 0 && options.hz > 0.0f;
}

const char* StateName(const BotClient::State state)
{
    switch (state)
    {
    case BotClient::State::Connecting: return "connecting";
    case BotClient::State::Authenticating: return "authenticating";
    case BotClient::State::Joining: return "joining";
    case BotClient::State::Running: return "running";
    case BotClient::State::Dead: return "DEAD";
    }
    return "?";
}
} // namespace

int main(const int argc, char** argv)
{
    Options options;
    if (!ParseArgs(argc, argv, options))
    {
        return 2;
    }

    std::signal(SIGINT, OnSignal);

    if (SteamDatagramErrMsg errMsg; !GameNetworkingSockets_Init(nullptr, errMsg))
    {
        std::fprintf(stderr, "could not initialize GameNetworkingSockets: %s\n", errMsg);
        return 1;
    }

    ISteamNetworkingSockets* iface = SteamNetworkingSockets();

    char connectionString[64];
    std::snprintf(connectionString, sizeof(connectionString), "%s:%u", options.server.c_str(), options.port);
    SteamNetworkingIPAddr address = {};
    if (!address.ParseString(connectionString))
    {
        std::fprintf(stderr, "failed to parse server address \"%s\"\n", connectionString);
        return 2;
    }

    std::printf("bot-harness: %d bot(s) -> %s, pattern %s, %.0f Hz, center (%.1f, %.1f, %.1f)\n", options.count,
                connectionString, options.pattern == Behavior::Pattern::Circle ? "circle" : "random", options.hz,
                options.center.x, options.center.y, options.center.z);

    std::vector<std::unique_ptr<BotClient>> bots;
    for (int i = 0; i < options.count; i++)
    {
        auto bot = std::make_unique<BotClient>(i, iface,
                                               Behavior(options.pattern, options.center, i, options.boundaryDistance));
        bot->Connect(address);
        bots.push_back(std::move(bot));
    }

    const auto sendInterval = 1.0f / options.hz;
    float sendAccumulator = 0.0f;
    float statsAccumulator = 0.0f;
    float jumpAccumulator = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();

    while (!g_stop)
    {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        iface->RunCallbacks();
        for (const auto& bot : bots)
        {
            bot->PollIncomingMessages();
        }

        sendAccumulator += dt;
        if (sendAccumulator >= sendInterval)
        {
            for (const auto& bot : bots)
            {
                bot->SendTick(sendAccumulator);
            }
            sendAccumulator = 0.0f;
        }

        if (options.jumpEvery > 0.0f)
        {
            jumpAccumulator += dt;
            if (jumpAccumulator >= options.jumpEvery)
            {
                jumpAccumulator = 0.0f;
                for (const auto& bot : bots)
                {
                    bot->SendJump();
                }
            }
        }

        statsAccumulator += dt;
        if (statsAccumulator >= 1.0f)
        {
            statsAccumulator = 0.0f;

            int running = 0, dead = 0;
            uint64_t teleports = 0, actions = 0, spawns = 0, despawns = 0;
            for (const auto& bot : bots)
            {
                if (bot->GetState() == BotClient::State::Running) running++;
                if (bot->GetState() == BotClient::State::Dead) dead++;
                teleports += bot->ConsumeTeleportCount();
                actions += bot->ConsumeActionCount();
                spawns += bot->ConsumeSpawnCount();
                despawns += bot->ConsumeDespawnCount();
            }
            std::printf("stats: %d/%zu running, %d dead, %llu teleports, %llu actions, %llu spawns, %llu despawns received/s\n",
                        running, bots.size(), dead, static_cast<unsigned long long>(teleports),
                        static_cast<unsigned long long>(actions), static_cast<unsigned long long>(spawns),
                        static_cast<unsigned long long>(despawns));

            if (dead == static_cast<int>(bots.size()))
            {
                std::fprintf(stderr, "all bots dead, exiting\n");
                GameNetworkingSockets_Kill();
                return 1;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    std::printf("shutting down, closing %zu connection(s)\n", bots.size());
    for (const auto& bot : bots)
    {
        bot->Close("harness shutdown");
    }
    iface->RunCallbacks();

    GameNetworkingSockets_Kill();
    return 0;
}
