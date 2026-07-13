using Cyberverse.Server.NativeLayer.Protocol.Common;
using Cyberverse.Server.Types;

namespace Cyberverse.Server.Services;

/// <summary>
/// 2D cell grid over the world (x/y; height stays out of the cell structure — Night City is
/// horizontally huge but only ~200m tall). Pure candidate pre-filter: correctness always
/// comes from the exact 3D distance check done by the caller, never from cell geometry.
/// Ring-generic query so cellSize is a free tuning parameter (see spec).
/// </summary>
public class SpatialGrid
{
    private readonly float _cellSize;
    private readonly Dictionary<(int x, int y), HashSet<Entity>> _cells = new();
    private readonly Dictionary<ulong, (int x, int y)> _entityCells = new();

    public SpatialGrid(float cellSize)
    {
        _cellSize = cellSize;
    }

    private (int x, int y) CellOf(Vector3 position)
    {
        return ((int)MathF.Floor(position.x / _cellSize), (int)MathF.Floor(position.y / _cellSize));
    }

    /// Upsert: first call inserts, later calls migrate on cell change (cheap no-op otherwise).
    public void Move(Entity entity)
    {
        var cell = CellOf(entity.WorldTransform);
        if (_entityCells.TryGetValue(entity.NetworkedEntityId, out var oldCell))
        {
            if (oldCell == cell)
            {
                return;
            }

            RemoveFromCell(entity, oldCell);
        }

        if (!_cells.TryGetValue(cell, out var set))
        {
            set = new HashSet<Entity>();
            _cells.Add(cell, set);
        }

        set.Add(entity);
        _entityCells[entity.NetworkedEntityId] = cell;
    }

    public void Remove(Entity entity)
    {
        if (_entityCells.Remove(entity.NetworkedEntityId, out var cell))
        {
            RemoveFromCell(entity, cell);
        }
    }

    private void RemoveFromCell(Entity entity, (int x, int y) cell)
    {
        if (_cells.TryGetValue(cell, out var set))
        {
            set.Remove(entity);
            if (set.Count == 0)
            {
                _cells.Remove(cell);
            }
        }
    }

    public IEnumerable<Entity> QueryCandidates(Vector3 position, float radius)
    {
        var center = CellOf(position);
        var rings = (int)MathF.Ceiling(radius / _cellSize);
        for (var dx = -rings; dx <= rings; dx++)
        {
            for (var dy = -rings; dy <= rings; dy++)
            {
                if (!_cells.TryGetValue((center.x + dx, center.y + dy), out var set))
                {
                    continue;
                }

                foreach (var entity in set)
                {
                    yield return entity;
                }
            }
        }
    }
}
