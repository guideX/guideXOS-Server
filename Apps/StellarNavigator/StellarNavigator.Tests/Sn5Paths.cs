using Astronomy.Time;

namespace StellarNavigator.Tests;

/// <summary>Shared SN5 test asset locations (time-data asset + CSPICE fixture).</summary>
internal static class Sn5Paths
{
    internal static string TimeDataAssetPath()
    {
        var src = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..",
            "Astronomy.Time", "Data", "leap_seconds.sn5json");
        if (File.Exists(src))
            return Path.GetFullPath(src);
        var local = Path.Combine(AppContext.BaseDirectory, "Data", "leap_seconds.sn5json");
        if (File.Exists(local))
            return local;
        throw new FileNotFoundException("SN5 leap_seconds.sn5json not found.");
    }

    internal static string Sn5ReferencePath()
    {
        var local = Path.Combine(AppContext.BaseDirectory, "sn5_reference.json");
        if (File.Exists(local))
            return local;
        throw new FileNotFoundException("sn5_reference.json not found.");
    }

    internal static LeapSecondProvider LoadProvider() =>
        LeapSecondProvider.Load(TimeDataAssetPath());

    internal static TimeConverter LoadConverter()
    {
        var provider = LoadProvider();
        return new TimeConverter(provider.Table, provider.Identity);
    }
}
