using System.IO;
using System.Text.Json;

namespace NetSpeedMonitor.Services;

/// <summary>
/// Preferences that survive a restart. Only the adapter choice for now; the
/// file lives under %LOCALAPPDATA% so an update of the exe keeps it.
/// </summary>
public sealed class Settings
{
    /// <summary>0 means "follow the default route"; anything else pins an interface.</summary>
    public uint AdapterIndex { get; set; }

    public static string DefaultPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "NetSpeedMonitor",
        "settings.json");

    /// <summary>
    /// Never throws: a missing, corrupt or unreadable file falls back to
    /// defaults, because losing a preference is not worth failing startup.
    /// </summary>
    public static Settings Load(string? path = null)
    {
        string file = path ?? DefaultPath;

        try
        {
            if (!File.Exists(file)) return new Settings();

            return JsonSerializer.Deserialize<Settings>(File.ReadAllText(file)) ?? new Settings();
        }
        catch
        {
            return new Settings();
        }
    }

    public void Save(string? path = null)
    {
        string file = path ?? DefaultPath;

        try
        {
            string? directory = Path.GetDirectoryName(file);
            if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);

            File.WriteAllText(file, JsonSerializer.Serialize(this));
        }
        catch
        {
            // Same reasoning as Load: a read-only profile should not kill the app.
        }
    }
}
