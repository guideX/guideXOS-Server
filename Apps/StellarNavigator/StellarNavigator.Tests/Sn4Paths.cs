namespace StellarNavigator.Tests;

/// <summary>Shared SN4 test asset locations (binary dataset + reference fixture).</summary>
internal static class Sn4Paths
{
    internal static string DatasetPath()
    {
        var src = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..",
            "Astronomy.Ephemeris", "Data", "ephemeris_de440s_chebyshev.sn4bin");
        if (File.Exists(src))
            return Path.GetFullPath(src);
        var local = Path.Combine(AppContext.BaseDirectory, "Data", "ephemeris_de440s_chebyshev.sn4bin");
        if (File.Exists(local))
            return local;
        throw new FileNotFoundException("SN4 SNEPH dataset not found.");
    }

    internal static string Sn4ReferencePath()
    {
        var local = Path.Combine(AppContext.BaseDirectory, "sn4_reference.json");
        if (File.Exists(local))
            return local;
        throw new FileNotFoundException("sn4_reference.json not found.");
    }
}
