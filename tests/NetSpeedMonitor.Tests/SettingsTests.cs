using System.IO;
using NetSpeedMonitor.Services;

namespace NetSpeedMonitor.Tests;

public class SettingsTests
{
    [Fact]
    public void RoundTripsTheAdapterChoice()
    {
        string path = TempPath();

        try
        {
            new Settings { AdapterIndex = 12 }.Save(path);

            Settings loaded = Settings.Load(path);

            Assert.Equal(12u, loaded.AdapterIndex);
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public void MissingFileMeansAutomatic()
    {
        string path = TempPath();

        Assert.Equal(0u, Settings.Load(path).AdapterIndex);
    }

    [Fact]
    public void CorruptFileFallsBackToDefaults()
    {
        string path = TempPath();

        try
        {
            File.WriteAllText(path, "{ this is not json");

            Assert.Equal(0u, Settings.Load(path).AdapterIndex);
        }
        finally
        {
            Delete(path);
        }
    }

    private static string TempPath()
    {
        string directory = Path.Combine(Path.GetTempPath(), $"nsm-settings-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        return Path.Combine(directory, "settings.json");
    }

    private static void Delete(string path)
    {
        try
        {
            Directory.Delete(Path.GetDirectoryName(path)!, recursive: true);
        }
        catch
        {
            // Temp cleanup is best effort.
        }
    }
}
