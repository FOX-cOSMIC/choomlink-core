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

    private static string WriteTemp(string content)
    {
        var path = Path.Combine(Path.GetTempPath(), $"choomlink-test-{Guid.NewGuid()}.json");
        File.WriteAllText(path, content);
        return path;
    }
}
