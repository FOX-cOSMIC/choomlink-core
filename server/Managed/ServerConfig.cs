using System.Text.Json;
using NLog;

namespace Cyberverse.Server;

public class InterestConfig
{
    public float EnterRadius { get; set; } = 425.0f;
    public float ExitRadius { get; set; } = 470.0f;
    public float CellSize { get; set; } = 470.0f;
}

public class ServerConfig
{
    private static readonly Logger Logger = LogManager.GetCurrentClassLogger();

    public InterestConfig Interest { get; set; } = new();

    public static ServerConfig Load(string path)
    {
        ServerConfig? config = null;
        try
        {
            if (File.Exists(path))
            {
                config = JsonSerializer.Deserialize<ServerConfig>(File.ReadAllText(path),
                    new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
            }
            else
            {
                Logger.Info("No config.json found at {0}, using defaults", path);
            }
        }
        catch (Exception e)
        {
            Logger.Warn(e, "config.json could not be read, using defaults");
        }

        return Validate(config ?? new ServerConfig());
    }

    private static ServerConfig Validate(ServerConfig config)
    {
        var i = config.Interest;
        if (i.EnterRadius <= 0 || i.ExitRadius <= i.EnterRadius || i.CellSize <= 0)
        {
            Logger.Warn("Invalid interest config (need exitRadius > enterRadius > 0, cellSize > 0) — using defaults");
            config.Interest = new InterestConfig();
        }
        return config;
    }
}
