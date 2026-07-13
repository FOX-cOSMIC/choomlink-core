namespace Cyberverse.Server.Types;

/// <summary>
/// Don't overuse this class, it's just a simple dataclass, we want to handle complex data in a different way
/// </summary>
public class Player
{
    // TODO: Validate by JWT claim
    public string? Name;
    public uint ConnectionId;

    /// <summary>
    /// The player's own puppet entity (set on JoinWorld). Used as the viewer position for
    /// interest management. Null until the player has joined the world.
    /// </summary>
    public Entity? PuppetEntity;
}