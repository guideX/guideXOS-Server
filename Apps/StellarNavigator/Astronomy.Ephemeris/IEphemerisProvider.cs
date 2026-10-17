using Astronomy.Core;
using Astronomy.Time;
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

/// <summary>Optional provider path that evaluates directly at ET without conversion through a Julian date.</summary>
public interface IEtEphemerisProvider
{
    NavigationResult<(Vector3D Position, Vector3D Velocity)> GetStateAtEt(
        string bodyId, EtSeconds epoch, ReferenceFrame targetFrame);
}
