using Astronomy.Core;
using Navigation.Core;

namespace Astronomy.Ephemeris;

public interface IEphemerisProvider
{
    NavigationResult<(Vector3D Position, Vector3D Velocity)> GetState(
        string bodyId, AstroTime epoch, ReferenceFrame targetFrame);

    bool IsBodySupported(string bodyId);
    (AstroTime Start, AstroTime End)? GetCoverage(string bodyId);
    string DataSource { get; }
    ReferenceFrame NativeFrame { get; }
}
