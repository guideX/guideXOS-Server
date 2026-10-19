using System.Diagnostics;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
using Navigation.Core;

namespace StellarNavigator.Gui;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        var startup = Stopwatch.StartNew();
        ApplicationConfiguration.Initialize();
        IndexedChebyshevEphemerisProvider? ephemeris = null;
        TimeConverter? timeConverter = null;
        IauEarthOrientation? orientation = null;
        SunGravitationalParameter? solarGm = null;
        string trajectoryStatus;
        string? ephemerisError = null;
        string timeStatus;
        string orientationStatus;

        try
        {
            string dataDirectory = Path.Combine(AppContext.BaseDirectory, "Data");
            ephemeris = new IndexedChebyshevEphemerisProvider(
                Path.Combine(dataDirectory, "ephemeris_de440s_chebyshev.sn4bin"));
            try
            {
                var time = LeapSecondProvider.Load(Path.Combine(dataDirectory, "leap_seconds.sn5json"));
                timeConverter = new TimeConverter(time.Table, time.Identity);
                timeStatus = $"SN5 time table authenticated ({time.LastAnnouncedLeapSecondUtc})";
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException)
            {
                timeStatus = $"UTC disabled: {ex.Message}";
            }
            try
            {
                orientation = IauEarthOrientation.Load(Path.Combine(dataDirectory, "earth_orientation.sn6json"));
                orientationStatus = $"IAU_EARTH enabled: {orientation.Provenance} (low-precision cartographic model)";
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException)
            {
                orientationStatus = $"IAU_EARTH disabled: {ex.Message}";
            }
            try
            {
                solarGm = SunGravitationalParameter.Load(Path.Combine(dataDirectory, SunGravitationalParameter.RuntimeAssetName));
                trajectoryStatus = $"SN8 idealized trajectory enabled: {solarGm.Identity}";
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or ArgumentException)
            {
                trajectoryStatus = $"SN8 trajectory disabled: authenticated solar GM data unavailable ({ex.Message})";
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or ArgumentException)
        {
            ephemerisError = $"The DE440s ephemeris could not be loaded: {ex.Message}";
            timeStatus = "UTC status unavailable until the ephemeris data loads.";
            orientationStatus = "IAU_EARTH status unavailable until the ephemeris data loads.";
            trajectoryStatus = "SN8 trajectory unavailable until the ephemeris data loads.";
        }

        Scene.SceneBuilder? sceneBuilder = ephemeris is null ? null : new Scene.SceneBuilder(ephemeris, orientation, solarGm);
        try
        {
            Application.Run(new MainForm(sceneBuilder, ephemeris, timeConverter,
                ephemerisError, timeStatus, orientationStatus, trajectoryStatus, startup));
        }
        finally
        {
            ephemeris?.Dispose();
        }
    }
}
