using Cyberverse.Server.NativeLayer.Protocol.Clientbound;
using Cyberverse.Server.Types;

namespace Cyberverse.Server.Services;

/// <summary>
/// Distance-based interest management with hysteresis (spec:
/// choomlink/docs/superpowers/specs/2026-07-13-interest-management-design.md).
/// Enter checks run over spatial-grid candidates; exit checks run over the CURRENT tracking
/// relations (reverse index for "who sees this entity", the player's own tracked set for
/// "what does this player see") — a party that left the query radius never shows up among
/// grid candidates again, so exit can never be grid-driven.
/// </summary>
public class EntityTracker
{
    public delegate bool EntityVisibilityFilter(Player player, Entity entity);

    private readonly IPacketSink _sink;
    private readonly PlayerService _players;
    private readonly SpatialGrid _grid;
    private readonly InterestConfig _config;

    // player -> entities they track, and entity -> players tracking it. Kept in sync exclusively
    // by OnStartTrackingEntity/OnStopTrackingEntity.
    private readonly Dictionary<uint, HashSet<Entity>> _trackedEntities = new();
    private readonly Dictionary<ulong, HashSet<uint>> _trackers = new();

    private EntityVisibilityFilter? _visibilityFilter;

    public EntityTracker(IPacketSink sink, PlayerService players, SpatialGrid grid, InterestConfig config)
    {
        _sink = sink;
        _players = players;
        _grid = grid;
        _config = config;
    }

    public void SetEntityVisibilityFilter(EntityVisibilityFilter? filter)
    {
        _visibilityFilter = filter;
    }

    public IReadOnlyCollection<uint> GetPlayersTracking(ulong entityId)
    {
        return _trackers.TryGetValue(entityId, out var set) ? set : [];
    }

    public void UpdateTrackingOf(Entity entity)
    {
        _grid.Move(entity);

        // Enter side: players whose puppet is a grid candidate around the moved entity.
        foreach (var candidate in _grid.QueryCandidates(entity.WorldTransform, _config.EnterRadius))
        {
            var viewer = ViewerOf(candidate);
            if (viewer == null || viewer.ConnectionId == entity.NetworkIdOwner)
            {
                continue;
            }

            ConsiderPair(viewer, entity);
        }

        // Exit side MUST use the reverse index (see class doc).
        if (_trackers.TryGetValue(entity.NetworkedEntityId, out var trackers))
        {
            foreach (var connectionId in trackers.ToArray())
            {
                if (_players.ConnectedPlayers.TryGetValue(connectionId, out var player))
                {
                    ConsiderPair(player, entity);
                }
            }
        }

        // Position update to everyone still tracking.
        if (_trackers.TryGetValue(entity.NetworkedEntityId, out var remaining) && remaining.Count > 0)
        {
            var teleport = new TeleportEntity
            {
                networkedEntityId = entity.NetworkedEntityId,
                targetPosition = entity.WorldTransform,
                yaw = entity.Yaw
            };
            foreach (var connectionId in remaining)
            {
                _sink.EnqueueMessage(EMessageTypeClientbound.TeleportEntity, connectionId, 1, teleport);
            }
        }

        // Mover's own view (spec weakness 6): when a player's puppet moves, re-evaluate what
        // that player sees — a resting entity never triggers its own UpdateTrackingOf.
        if (entity.NetworkIdOwner != 0 && !entity.IsVehicle
            && _players.ConnectedPlayers.TryGetValue((uint)entity.NetworkIdOwner, out var owner)
            && ReferenceEquals(owner.PuppetEntity, entity))
        {
            // Enter side of the mover's view.
            foreach (var other in _grid.QueryCandidates(entity.WorldTransform, _config.EnterRadius))
            {
                if (other.NetworkIdOwner == entity.NetworkIdOwner)
                {
                    continue;
                }

                ConsiderPair(owner, other);
            }

            // Exit side of the mover's view: resting entities the mover walked away from are
            // no grid candidates anymore — check the mover's own tracked set.
            if (_trackedEntities.TryGetValue(owner.ConnectionId, out var tracked))
            {
                foreach (var trackedEntity in tracked.ToArray())
                {
                    ConsiderPair(owner, trackedEntity);
                }
            }
        }
    }

    /// Applies enter/exit hysteresis for one (player, entity) pair. Which radius applies is
    /// decided by the pair's current tracked state.
    private void ConsiderPair(Player player, Entity entity)
    {
        if (player.PuppetEntity == null)
        {
            return; // not in the world yet
        }

        if (_visibilityFilter != null && !_visibilityFilter.Invoke(player, entity))
        {
            OnStopTrackingEntity(player, entity);
            return;
        }

        var distSq = player.PuppetEntity.WorldTransform.DistanceSquared(entity.WorldTransform);
        var isTracked = _trackedEntities.TryGetValue(player.ConnectionId, out var set) && set.Contains(entity);

        if (!isTracked && distSq <= _config.EnterRadius * _config.EnterRadius)
        {
            OnStartTrackingEntity(player, entity);
        }
        else if (isTracked && distSq > _config.ExitRadius * _config.ExitRadius)
        {
            OnStopTrackingEntity(player, entity);
        }
    }

    private Player? ViewerOf(Entity candidate)
    {
        if (candidate.NetworkIdOwner == 0 || candidate.IsVehicle)
        {
            return null;
        }

        _players.ConnectedPlayers.TryGetValue((uint)candidate.NetworkIdOwner, out var player);
        // Only the puppet represents the viewer's position — ignore other owned entities.
        return player != null && ReferenceEquals(player.PuppetEntity, candidate) ? player : null;
    }

    public void OnStartTrackingEntity(Player player, Entity entity)
    {
        if (!_trackedEntities.TryGetValue(player.ConnectionId, out var tracked))
        {
            tracked = new HashSet<Entity>();
            _trackedEntities.Add(player.ConnectionId, tracked);
        }

        if (!tracked.Add(entity))
        {
            return; // already tracked
        }

        if (!_trackers.TryGetValue(entity.NetworkedEntityId, out var trackers))
        {
            trackers = new HashSet<uint>();
            _trackers.Add(entity.NetworkedEntityId, trackers);
        }

        trackers.Add(player.ConnectionId);

        var spawnEntity = new SpawnEntity
        {
            networkedEntityId = entity.NetworkedEntityId,
            recordId = entity.RecordId,
            spawnPosition = entity.WorldTransform
        };
        _sink.EnqueueMessage(EMessageTypeClientbound.SpawnEntity, player.ConnectionId, 1, spawnEntity);
    }

    public void OnStopTrackingEntity(Player player, Entity entity)
    {
        if (!_trackedEntities.TryGetValue(player.ConnectionId, out var tracked) || !tracked.Remove(entity))
        {
            return; // wasn't tracked
        }

        if (_trackers.TryGetValue(entity.NetworkedEntityId, out var trackers))
        {
            trackers.Remove(player.ConnectionId);
            if (trackers.Count == 0)
            {
                _trackers.Remove(entity.NetworkedEntityId);
            }
        }

        var destroyEntity = new DestroyEntity { networkedEntityId = entity.NetworkedEntityId };
        _sink.EnqueueMessage(EMessageTypeClientbound.DestroyEntity, player.ConnectionId, 1, destroyEntity);
    }

    /// Full removal: despawn at every tracker, drop from grid. Callers already use this before
    /// EntityService.RemoveEntity (vehicles, items, disconnects).
    public void StopTrackingOf(Entity entity)
    {
        foreach (var connectionId in GetPlayersTracking(entity.NetworkedEntityId).ToArray())
        {
            if (_players.ConnectedPlayers.TryGetValue(connectionId, out var player))
            {
                OnStopTrackingEntity(player, entity);
            }
        }

        _trackers.Remove(entity.NetworkedEntityId);
        _grid.Remove(entity);
    }

    /// Cleans the disconnected player's own tracked set + reverse-index entries. The player's
    /// entities themselves are cleaned up by the caller via StopTrackingOf.
    public void OnPlayerDisconnected(uint connectionId)
    {
        if (!_trackedEntities.Remove(connectionId, out var tracked))
        {
            return;
        }

        foreach (var entity in tracked)
        {
            if (_trackers.TryGetValue(entity.NetworkedEntityId, out var trackers))
            {
                trackers.Remove(connectionId);
                if (trackers.Count == 0)
                {
                    _trackers.Remove(entity.NetworkedEntityId);
                }
            }
        }
    }

    /// <summary>
    /// A joining player wouldn't see resting entities (tracking is move-driven), so evaluate
    /// everything near their spawn once. Uses the same grid+hysteresis path as movement.
    /// </summary>
    public void InitialSpawnForPlayer(Player player)
    {
        if (player.PuppetEntity == null)
        {
            return;
        }

        foreach (var entity in _grid.QueryCandidates(player.PuppetEntity.WorldTransform, _config.EnterRadius))
        {
            if (entity.NetworkIdOwner == player.ConnectionId)
            {
                continue;
            }

            ConsiderPair(player, entity);
        }
    }
}
