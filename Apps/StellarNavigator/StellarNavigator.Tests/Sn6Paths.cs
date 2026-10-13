namespace StellarNavigator.Tests;

/// <summary>Shared SN6 test asset locations (orientation asset + CSPICE fixture).</summary>
internal static class Sn6Paths
{
    internal static string OrientationAssetPath()
    {
        var src = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..",
            "Astronomy.Core", "Data", "earth_orientation.sn6json");
        if (File.Exists(src))
            return Path.GetFullPath(src);
        var local = Path.Combine(AppContext.BaseDirectory, "Data", "earth_orientation.sn6json");
        if (File.Exists(local))
            return local;
        throw new FileNotFoundException("SN6 earth_orientation.sn6json not found.");
    }

    internal static string Sn6ReferencePath()
    {
        var local = Path.Combine(AppContext.BaseDirectory, "sn6_reference.json");
        if (File.Exists(local))
            return local;
        throw new FileNotFoundException("sn6_reference.json not found.");
    }
}
