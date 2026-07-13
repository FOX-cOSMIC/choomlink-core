using Cyberverse.Server.NativeLayer.Protocol.Clientbound;
using Cyberverse.Server.NativeLayer.Protocol.Common;
using Cyberverse.Server.Services;
using Cyberverse.Server.Types;
using Xunit;

namespace Cyberverse.Server.Tests;

internal sealed class RecordingSink : IPacketSink
{
    public readonly List<(EMessageTypeClientbound type, uint connectionId, object content)> Sent = new();

    public void EnqueueMessage<T>(EMessageTypeClientbound messageType, uint connectionId, byte channelId, T content)
        where T : struct
    {
        Sent.Add((messageType, connectionId, content));
    }

    public int CountFor(uint connectionId, EMessageTypeClientbound type)
        => Sent.Count(s => s.connectionId == connectionId && s.type == type);

    public void Clear() => Sent.Clear();
}

public class EntityTrackerTests
{
    private readonly RecordingSink _sink = new();
    private readonly PlayerService _players = new();
    private readonly SpatialGrid _grid = new(470f);
    private readonly EntityTracker _tracker;

    public EntityTrackerTests()
    {
        _tracker = new EntityTracker(_sink, _players, _grid,
            new InterestConfig { EnterRadius = 425f, ExitRadius = 470f, CellSize = 470f });
    }

    private (Player player, Entity puppet) AddPlayer(uint connectionId, float x, float y)
    {
        var puppet = new Entity(connectionId * 1000, recordId: 1)
        {
            NetworkIdOwner = connectionId,
            WorldTransform = new Vector3 { x = x, y = y }
        };
        var player = new Player { ConnectionId = connectionId, Name = $"p{connectionId}", PuppetEntity = puppet };
        _players.ConnectedPlayers.Add(connectionId, player);
        _grid.Move(puppet);
        return (player, puppet);
    }

    [Fact]
    public void EnterRadius_SpawnsExactlyOnce()
    {
        var (_, mover) = AddPlayer(1, 0f, 0f);
        AddPlayer(2, 400f, 0f); // within 425
        _tracker.UpdateTrackingOf(mover);
        Assert.Equal(1, _sink.CountFor(2, EMessageTypeClientbound.SpawnEntity));
        _tracker.UpdateTrackingOf(mover); // same position again
        Assert.Equal(1, _sink.CountFor(2, EMessageTypeClientbound.SpawnEntity));
    }

    [Fact]
    public void OutsideEnterRadius_NoSpawn_NoTeleport()
    {
        var (_, mover) = AddPlayer(1, 0f, 0f);
        AddPlayer(2, 450f, 0f); // between enter and exit, never tracked before
        _tracker.UpdateTrackingOf(mover);
        Assert.Equal(0, _sink.CountFor(2, EMessageTypeClientbound.SpawnEntity));
        Assert.Equal(0, _sink.CountFor(2, EMessageTypeClientbound.TeleportEntity));
    }

    [Fact]
    public void Hysteresis_OscillatingInsideBand_NoRepeatedSpawnDespawn()
    {
        var (_, mover) = AddPlayer(1, 0f, 0f);
        AddPlayer(2, 400f, 0f);
        _tracker.UpdateTrackingOf(mover); // tracked now (400 <= 425)
        _sink.Clear();

        foreach (var x in new[] { 440f, 465f, 430f, 468f, 426f })
        {
            // p2 sits at x=400, so mover at x keeps distance |x - 400| + base 400 relative to p2?
            // No: distance from p2 (400,0) to mover (x,0) is |x-400|. To oscillate INSIDE the
            // 425/470 band relative to p2, positions must be 825..870 or -25..-70 away from origin.
            // Use offsets beyond p2: 400 + band values.
            mover.WorldTransform = new Vector3 { x = 400f + x, y = 0f };
            _tracker.UpdateTrackingOf(mover);
        }

        Assert.Equal(0, _sink.CountFor(2, EMessageTypeClientbound.SpawnEntity));
        Assert.Equal(0, _sink.CountFor(2, EMessageTypeClientbound.DestroyEntity));
        Assert.True(_sink.CountFor(2, EMessageTypeClientbound.TeleportEntity) >= 5); // still synced
    }

    [Fact]
    public void BeyondExitRadius_DespawnsExactlyOnce()
    {
        var (_, mover) = AddPlayer(1, 0f, 0f);
        AddPlayer(2, 400f, 0f);
        _tracker.UpdateTrackingOf(mover);
        _sink.Clear();

        mover.WorldTransform = new Vector3 { x = 5000f, y = 0f };
        _tracker.UpdateTrackingOf(mover);
        Assert.Equal(1, _sink.CountFor(2, EMessageTypeClientbound.DestroyEntity));
        _tracker.UpdateTrackingOf(mover);
        Assert.Equal(1, _sink.CountFor(2, EMessageTypeClientbound.DestroyEntity));
    }

    [Fact]
    public void FarJumpBeyondQueryRadius_TrackersStillGetDespawn_ReverseIndexConsistent()
    {
        var (_, mover) = AddPlayer(1, 0f, 0f);
        AddPlayer(2, 100f, 0f);
        _tracker.UpdateTrackingOf(mover);
        Assert.Contains(2u, _tracker.GetPlayersTracking(mover.NetworkedEntityId));
        _sink.Clear();

        mover.WorldTransform = new Vector3 { x = 100000f, y = 100000f }; // many cells away
        _tracker.UpdateTrackingOf(mover);

        Assert.Equal(1, _sink.CountFor(2, EMessageTypeClientbound.DestroyEntity));
        Assert.Empty(_tracker.GetPlayersTracking(mover.NetworkedEntityId));
    }

    [Fact]
    public void MoverApproachesRestingEntity_MoverStartsTrackingIt()
    {
        // Weakness 6 from the spec: viewer moves, target rests.
        var (_, mover) = AddPlayer(1, 5000f, 0f);
        AddPlayer(2, 0f, 0f);
        _tracker.UpdateTrackingOf(mover);
        Assert.Equal(0, _sink.CountFor(1, EMessageTypeClientbound.SpawnEntity));

        mover.WorldTransform = new Vector3 { x = 100f, y = 0f };
        _tracker.UpdateTrackingOf(mover); // ONLY the mover updates — resting entity never does
        Assert.Equal(1, _sink.CountFor(1, EMessageTypeClientbound.SpawnEntity)); // mover now sees resting puppet
    }

    [Fact]
    public void MoverLeavesRestingEntity_MoverGetsDespawn()
    {
        // Symmetric to the reverse-index case: the RESTING entity never updates, so the
        // mover's own tracked set must be exit-checked when the mover moves away.
        var (_, mover) = AddPlayer(1, 0f, 0f);
        AddPlayer(2, 100f, 0f);
        _tracker.UpdateTrackingOf(mover); // mover now tracks p2's resting puppet
        Assert.Equal(1, _sink.CountFor(1, EMessageTypeClientbound.SpawnEntity));
        _sink.Clear();

        mover.WorldTransform = new Vector3 { x = 100000f, y = 0f };
        _tracker.UpdateTrackingOf(mover); // only the mover updates

        Assert.Equal(1, _sink.CountFor(1, EMessageTypeClientbound.DestroyEntity));
    }

    [Fact]
    public void ActionRouting_GetPlayersTracking_OnlyNearbyPlayers()
    {
        var (_, mover) = AddPlayer(1, 0f, 0f);
        AddPlayer(2, 100f, 0f);
        AddPlayer(3, 10000f, 0f);
        _tracker.UpdateTrackingOf(mover);

        var trackers = _tracker.GetPlayersTracking(mover.NetworkedEntityId);
        Assert.Contains(2u, trackers);
        Assert.DoesNotContain(3u, trackers);
    }

    [Fact]
    public void Disconnect_CleansBothIndexDirections()
    {
        var (_, mover) = AddPlayer(1, 0f, 0f);
        var (_, puppet2) = AddPlayer(2, 100f, 0f);
        _tracker.UpdateTrackingOf(mover);
        _tracker.UpdateTrackingOf(puppet2);

        _tracker.OnPlayerDisconnected(2);
        _tracker.StopTrackingOf(puppet2);
        _players.ConnectedPlayers.Remove(2);

        Assert.DoesNotContain(2u, _tracker.GetPlayersTracking(mover.NetworkedEntityId));
        Assert.Empty(_tracker.GetPlayersTracking(puppet2.NetworkedEntityId));
    }

    [Fact]
    public void StopTrackingOf_SendsDespawnToAllTrackersAndRemovesFromGrid()
    {
        var (_, mover) = AddPlayer(1, 0f, 0f);
        AddPlayer(2, 100f, 0f);
        _tracker.UpdateTrackingOf(mover);
        _sink.Clear();

        _tracker.StopTrackingOf(mover);
        Assert.Equal(1, _sink.CountFor(2, EMessageTypeClientbound.DestroyEntity));
        Assert.DoesNotContain(mover, _grid.QueryCandidates(new Vector3 { x = 0f, y = 0f }, 470f));
    }

    [Fact]
    public void VisibilityFilter_StillRespected()
    {
        var (_, mover) = AddPlayer(1, 0f, 0f);
        AddPlayer(2, 100f, 0f);
        _tracker.SetEntityVisibilityFilter((_, _) => false);
        _tracker.UpdateTrackingOf(mover);
        Assert.Equal(0, _sink.CountFor(2, EMessageTypeClientbound.SpawnEntity));
    }

    [Fact]
    public void InitialSpawnForPlayer_SeesNearbyRestingEntities()
    {
        AddPlayer(1, 50f, 0f);
        var (joiner, _) = AddPlayer(2, 0f, 0f);
        _tracker.InitialSpawnForPlayer(joiner);
        Assert.Equal(1, _sink.CountFor(2, EMessageTypeClientbound.SpawnEntity));
    }
}
