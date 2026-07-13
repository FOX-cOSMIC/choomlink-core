using Cyberverse.Server.NativeLayer.Protocol.Common;
using Cyberverse.Server.Services;
using Cyberverse.Server.Types;
using Xunit;

namespace Cyberverse.Server.Tests;

public class SpatialGridTests
{
    private static Entity MakeEntity(ulong id, float x, float y, float z = 0f)
    {
        return new Entity(id, recordId: 1) { WorldTransform = new Vector3 { x = x, y = y, z = z } };
    }

    [Fact]
    public void Query_FindsEntityInSameCell()
    {
        var grid = new SpatialGrid(470f);
        var e = MakeEntity(1, 10f, 10f);
        grid.Move(e);
        Assert.Contains(e, grid.QueryCandidates(new Vector3 { x = 50f, y = 50f }, 470f));
    }

    [Fact]
    public void Query_FindsEntityInNeighborCell()
    {
        var grid = new SpatialGrid(470f);
        var e = MakeEntity(1, 460f, 0f);          // cell (0,0)
        grid.Move(e);
        var candidates = grid.QueryCandidates(new Vector3 { x = 480f, y = 0f }, 470f); // query from cell (1,0)
        Assert.Contains(e, candidates);
    }

    [Fact]
    public void Query_RadiusLargerThanCell_SearchesEnoughRings()
    {
        var grid = new SpatialGrid(100f);          // small cells, radius 470 -> 5 rings
        var e = MakeEntity(1, 450f, 0f);           // cell (4,0), 450m away
        grid.Move(e);
        Assert.Contains(e, grid.QueryCandidates(new Vector3 { x = 0f, y = 0f }, 470f));
    }

    [Fact]
    public void Query_CompletenessProperty_AllEntitiesWithinRadiusAreCandidates()
    {
        var grid = new SpatialGrid(470f);
        var rng = new Random(1337);
        var entities = new List<Entity>();
        for (ulong i = 0; i < 200; i++)
        {
            var e = MakeEntity(i, (float)(rng.NextDouble() * 4000 - 2000), (float)(rng.NextDouble() * 4000 - 2000));
            entities.Add(e);
            grid.Move(e);
        }

        var origin = new Vector3 { x = 123f, y = -456f };
        const float radius = 470f;
        var candidates = grid.QueryCandidates(origin, radius).ToHashSet();
        foreach (var e in entities.Where(e => e.WorldTransform.DistanceSquared(origin) <= radius * radius))
        {
            Assert.Contains(e, candidates);
        }
    }

    [Fact]
    public void Move_AcrossCellBoundary_LeavesOldCell()
    {
        var grid = new SpatialGrid(470f);
        var e = MakeEntity(1, 10f, 10f);
        grid.Move(e);
        e.WorldTransform = new Vector3 { x = 2000f, y = 2000f };
        grid.Move(e);
        Assert.DoesNotContain(e, grid.QueryCandidates(new Vector3 { x = 10f, y = 10f }, 470f));
        Assert.Contains(e, grid.QueryCandidates(new Vector3 { x = 2000f, y = 2000f }, 470f));
    }

    [Fact]
    public void Move_EntityExactlyOnCellEdge_IsDeterministicAndFound()
    {
        var grid = new SpatialGrid(470f);
        var e = MakeEntity(1, 470f, 0f);           // exactly on the boundary
        grid.Move(e);
        Assert.Contains(e, grid.QueryCandidates(new Vector3 { x = 470f, y = 0f }, 470f));
    }

    [Fact]
    public void Remove_EntityIsGone()
    {
        var grid = new SpatialGrid(470f);
        var e = MakeEntity(1, 10f, 10f);
        grid.Move(e);
        grid.Remove(e);
        Assert.DoesNotContain(e, grid.QueryCandidates(new Vector3 { x = 10f, y = 10f }, 470f));
    }

    [Fact]
    public void Remove_UnknownEntity_DoesNotThrow()
    {
        var grid = new SpatialGrid(470f);
        grid.Remove(MakeEntity(99, 0f, 0f));
    }
}
