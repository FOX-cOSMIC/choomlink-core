using Xunit;

namespace Cyberverse.Server.Tests;

public class ServerConfigTests
{
    [Fact]
    public void Load_MissingFile_ReturnsDefaults()
    {
        var config = ServerConfig.Load(Path.Combine(Path.GetTempPath(), $"does-not-exist-{Guid.NewGuid()}.json"));
        Assert.Equal(425.0f, config.Interest.EnterRadius);
        Assert.Equal(470.0f, config.Interest.ExitRadius);
        Assert.Equal(470.0f, config.Interest.CellSize);
    }

    [Fact]
    public void Load_ValidFile_ReadsValues()
    {
        var path = WriteTemp("""{ "interest": { "enterRadius": 200.0, "exitRadius": 240.0, "cellSize": 240.0 } }""");
        var config = ServerConfig.Load(path);
        Assert.Equal(200.0f, config.Interest.EnterRadius);
        Assert.Equal(240.0f, config.Interest.ExitRadius);
    }

    [Fact]
    public void Load_ExitNotGreaterThanEnter_FallsBackToDefaults()
    {
        var path = WriteTemp("""{ "interest": { "enterRadius": 400.0, "exitRadius": 400.0, "cellSize": 470.0 } }""");
        var config = ServerConfig.Load(path);
        Assert.Equal(425.0f, config.Interest.EnterRadius);
        Assert.Equal(470.0f, config.Interest.ExitRadius);
    }

    [Fact]
    public void Load_MalformedJson_FallsBackToDefaults()
    {
        var path = WriteTemp("{ not json ");
        var config = ServerConfig.Load(path);
        Assert.Equal(425.0f, config.Interest.EnterRadius);
    }

    [Fact]
    public void Load_MissingFalloff_UsesDefaultTiers()
    {
        var path = WriteTemp("""{ "interest": { "enterRadius": 425.0, "exitRadius": 470.0, "cellSize": 470.0 } }""");
        var config = ServerConfig.Load(path);
        Assert.Equal(3, config.Interest.Falloff.Count);
        Assert.Equal(1, config.Interest.Falloff[0].Divisor);
        Assert.Equal(100.0f, config.Interest.Falloff[0].MaxDistance);
        Assert.Equal(5, config.Interest.Falloff[2].Divisor);
    }

    [Fact]
    public void Load_CustomFalloff_IsRead()
    {
        var path = WriteTemp("""{ "interest": { "falloff": [ { "maxDistance": 50.0, "divisor": 1 }, { "maxDistance": 1e9, "divisor": 10 } ] } }""");
        var config = ServerConfig.Load(path);
        Assert.Equal(2, config.Interest.Falloff.Count);
        Assert.Equal(10, config.Interest.Falloff[1].Divisor);
    }

    [Fact]
    public void Load_UnsortedFalloff_FallsBackToDefaultTiers()
    {
        var path = WriteTemp("""{ "interest": { "falloff": [ { "maxDistance": 250.0, "divisor": 3 }, { "maxDistance": 100.0, "divisor": 1 } ] } }""");
        var config = ServerConfig.Load(path);
        Assert.Equal(3, config.Interest.Falloff.Count); // defaults
    }

    [Fact]
    public void Load_ZeroDivisor_FallsBackToDefaultTiers()
    {
        var path = WriteTemp("""{ "interest": { "falloff": [ { "maxDistance": 1e9, "divisor": 0 } ] } }""");
        var config = ServerConfig.Load(path);
        Assert.Equal(3, config.Interest.Falloff.Count);
        Assert.Equal(1, config.Interest.Falloff[0].Divisor);
    }

    private static string WriteTemp(string content)
    {
        var path = Path.Combine(Path.GetTempPath(), $"choomlink-test-{Guid.NewGuid()}.json");
        File.WriteAllText(path, content);
        return path;
    }
}
