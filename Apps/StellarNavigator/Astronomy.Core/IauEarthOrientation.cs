using System.Security.Cryptography;
using System.Text.Json;

namespace Astronomy.Core;

/// <summary>
/// SN6 managed IAU_EARTH orientation model.
///
/// Authenticated source: NAIF generic text PCK <c>pck00011.tpc</c>
/// (https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/pck00011.tpc,
/// retrieved 2026-10-13, SHA-256
/// 3dff7b1dbeceaa01f25467767d3fa25816051c85d162d1edf04acb310ee28bb1),
/// IAU WGCCRE 2015 constants for Earth (body 399):
/// POLE_RA = (0, -0.641, 0) deg/century-scale, POLE_DEC = (90, -0.557, 0),
/// PM = (190.147, 360.9856235, 0) deg/day-scale. Polynomial only; no
/// nutation, polar-motion, or UT1 terms.
///
/// Mathematics (per NAIF pck.req): with T = ET centuries past J2000 (TDB)
/// and d = ET days past J2000 (TDB),
/// RA(T) = RA0 + RA1*T + RA2*T^2, DEC(T) likewise, W(d) = W0 + W1*d + W2*d^2
/// (degrees), and the J2000-to-IAU_EARTH vector rotation is the passive
/// Euler product [W]3 [pi/2-Dec]1 [pi/2+RA]3, i.e.
/// R = Rz(-W) * Rx(-(pi/2-Dec)) * Rz(-(pi/2+RA)) in active-matrix terms.
/// The derivative dR/dt is analytical (chain rule over RA/Dec/W rates);
/// no finite differences are used on the validated path.
///
/// The deployed runtime uses this narrowly scoped managed model (plus the
/// compact JSON asset below for integrity checks). No general-purpose PCK
/// parser is included. Host-side CSPICE may load the source PCK for
/// generation/verification only.
///
/// This is a LOW-PRECISION cartographic model. IAU_EARTH is NOT equivalent
/// to high-precision terrestrial frames (ITRF93, EOP-based products).
/// </summary>
public sealed class IauEarthOrientation
{
    public const string SupportedSchema = "SN-FRAME-01";
    public const string SourceFilename = "pck00011.tpc";
    public const string SourceUrl = "https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/pck00011.tpc";
    public const string SourceRetrievalDate = "2026-10-13";
    public const string SourceKernelSha256 =
        "3dff7b1dbeceaa01f25467767d3fa25816051c85d162d1edf04acb310ee28bb1";

    /// <summary>Trusted SHA-256 of the distributed compact asset bytes.</summary>
    public const string ExpectedAssetSha256 =
        "d12c0cadc2937d37b7b0daf04a46233b39846f96773a4b5de2fdd9194b03b765";

    public const double SecondsPerDay = 86400.0;
    public const double SecondsPerJulianCentury = 86400.0 * 36525.0;

    // Degrees, verbatim from the authenticated PCK (BODY399_*).
    public const double PoleRa0Deg = 0.0;
    public const double PoleRa1DegPerCentury = -0.641;
    public const double PoleRa2DegPerCentury2 = 0.0;
    public const double PoleDec0Deg = 90.0;
    public const double PoleDec1DegPerCentury = -0.557;
    public const double PoleDec2DegPerCentury2 = 0.0;
    public const double Pm0Deg = 190.147;
    public const double Pm1DegPerDay = 360.9856235;
    public const double Pm2DegPerDay2 = 0.0;

    public string Provenance =>
        $"{SourceFilename} (SHA-256 {SourceKernelSha256[..16]}..., retrieved {SourceRetrievalDate}; IAU WGCCRE 2015)";

    private IauEarthOrientation() { }

    public static IauEarthOrientation Instance { get; } = new();

    /// <summary>
    /// Loads and authenticates the compact orientation asset. The
    /// numerical model above is authoritative; the asset carries the
    /// source-anchored metadata and is verified byte-for-byte so silent
    /// constant substitution is a load error, not a quiet drift.
    /// </summary>
    public static IauEarthOrientation Load(string assetPath)
    {
        byte[] bytes;
        try
        {
            bytes = File.ReadAllBytes(assetPath);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            throw new InvalidDataException($"Orientation asset not found or unreadable: {assetPath}", ex);
        }

        string actual = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
        if (!actual.Equals(ExpectedAssetSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException(
                $"Orientation asset integrity failure: SHA-256 {actual} != trusted {ExpectedAssetSha256}.");

        JsonDocument doc;
        try
        {
            doc = JsonDocument.Parse(bytes);
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException("Orientation asset is malformed JSON.", ex);
        }

        using (doc)
        {
            var root = doc.RootElement;
            if (!root.TryGetProperty("schema", out var schema) ||
                schema.GetString() != SupportedSchema)
                throw new InvalidDataException("Orientation asset schema mismatch (expected SN-FRAME-01).");
            if (!root.TryGetProperty("kernelSha256", out var ksha) ||
                !string.Equals(ksha.GetString(), SourceKernelSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Orientation asset kernel identity mismatch.");
            if (!CheckTriple(root, "poleRaDeg", PoleRa0Deg, PoleRa1DegPerCentury, PoleRa2DegPerCentury2) ||
                !CheckTriple(root, "poleDecDeg", PoleDec0Deg, PoleDec1DegPerCentury, PoleDec2DegPerCentury2) ||
                !CheckTriple(root, "primeMeridianDeg", Pm0Deg, Pm1DegPerDay, Pm2DegPerDay2))
                throw new InvalidDataException("Orientation asset constants do not match the managed model.");
        }

        return Instance;
    }

    private static bool CheckTriple(JsonElement root, string name, double a, double b, double c)
    {
        if (!root.TryGetProperty(name, out var el) || el.ValueKind != JsonValueKind.Array)
            return false;
        int i = 0;
        double[] expect = { a, b, c };
        foreach (var item in el.EnumerateArray())
        {
            if (i >= 3 || item.GetDouble() != expect[i])
                return false;
            i++;
        }
        return i == 3;
    }

    /// <summary>Euler angles (radians) and rates (rad/s) at ET seconds past J2000 TDB.</summary>
    public static void EvaluateAngles(double etSeconds,
        out double raRad, out double decRad, out double wRad,
        out double raRate, out double decRate, out double wRate)
    {
        if (!double.IsFinite(etSeconds))
            throw new ArgumentException("ET must be finite seconds past J2000 (TDB).", nameof(etSeconds));
        double t = etSeconds / SecondsPerJulianCentury;
        double d = etSeconds / SecondsPerDay;
        const double deg2rad = Math.PI / 180.0;
        raRad = (PoleRa0Deg + PoleRa1DegPerCentury * t + PoleRa2DegPerCentury2 * t * t) * deg2rad;
        decRad = (PoleDec0Deg + PoleDec1DegPerCentury * t + PoleDec2DegPerCentury2 * t * t) * deg2rad;
        wRad = (Pm0Deg + Pm1DegPerDay * d + Pm2DegPerDay2 * d * d) * deg2rad;
        raRate = (PoleRa1DegPerCentury + 2.0 * PoleRa2DegPerCentury2 * t) * deg2rad / SecondsPerJulianCentury;
        decRate = (PoleDec1DegPerCentury + 2.0 * PoleDec2DegPerCentury2 * t) * deg2rad / SecondsPerJulianCentury;
        wRate = (Pm1DegPerDay + 2.0 * Pm2DegPerDay2 * d) * deg2rad / SecondsPerDay;
    }

    /// <summary>
    /// J2000-to-IAU_EARTH rotation R(t) and analytical derivative dR/dt.
    /// Matches CSPICE pxform/sxform for the same PCK to ~1e-12 (angle
    /// growth with |ET| is binary64 quantization of W, not model error).
    /// </summary>
    public static void GetRotation(double etSeconds, out RotationMatrix r, out RotationMatrix.Derivative rDot)
    {
        EvaluateAngles(etSeconds, out double ra, out double dec, out double w,
            out double raDot, out double decDot, out double wDot);
        double u = Math.PI / 2.0 + ra;
        double v = Math.PI / 2.0 - dec;
        double cu = Math.Cos(u), su = Math.Sin(u);
        double cv = Math.Cos(v), sv = Math.Sin(v);
        double cw = Math.Cos(w), sw = Math.Sin(w);

        // R = A(W) * B(Dec) * C(RA), passive-Euler factors in active terms:
        // A = Rz(-W), B = Rx(-(pi/2-Dec)), C = Rz(-(pi/2+RA)).
        double r00 = cw * cu - sw * cv * su;
        double r01 = cw * su + sw * cv * cu;
        double r02 = sw * sv;
        double r10 = -sw * cu - cw * cv * su;
        double r11 = -sw * su + cw * cv * cu;
        double r12 = cw * sv;
        double r20 = sv * su;
        double r21 = -sv * cu;
        double r22 = cv;
        r = new RotationMatrix(r00, r01, r02, r10, r11, r12, r20, r21, r22);

        // Analytical derivative: dR = dA*B*C + A*dB*C + A*B*dC.
        // dA/dW for A=Rz(-W): [[-sw,cw,0],[-cw,-sw,0],[0,0,0]] * wDot.
        double a00 = -sw * wDot, a01 = cw * wDot;
        double a10 = -cw * wDot, a11 = -sw * wDot;
        // dC/du for C=Rz(-u): [[-su,cu,0],[-cu,-su,0],[0,0,0]] * raDot.
        double c00 = -su * raDot, c01 = cu * raDot;
        double c10 = -cu * raDot, c11 = -su * raDot;
        // B = Rx(-v): [[1,0,0],[0,cv,sv],[0,-sv,cv]]; dv/dt = -decDot.
        // dB/dt = dB/dv * (-decDot).
        double b11 = sv * decDot, b12 = -cv * decDot;
        double b21 = cv * decDot, b22 = sv * decDot;

        // Term1 = dA * B * C.
        // dA has only rows 0..1, cols 0..1 nonzero.
        double t1_00 = (a00 * 1.0 + a01 * 0.0) * cu + (a00 * 0.0 + a01 * cv) * (-su) + (a00 * 0.0 + a01 * sv) * 0.0;
        double t1_01 = (a00) * su + (a01 * cv) * cu;
        double t1_02 = a01 * sv;
        double t1_10 = (a10) * cu + (a11 * cv) * (-su);
        double t1_11 = (a10) * su + (a11 * cv) * cu;
        double t1_12 = a11 * sv;
        // Term1 row 2 is zero.

        // Term2 = A * dB * C. dB nonzero block rows/cols 1..2.
        // M = A*dB: M[i,j] = A[i,1]*dB[1,j] + A[i,2]*dB[2,j].
        // dB rows: row0 = 0; row1 = (0, b11, b12); row2 = (0, b21, b22).
        double A00 = cw, A01 = sw;
        double A10 = -sw, A11 = cw;
        const double A02 = 0.0, A12 = 0.0, A21 = 0.0, A22 = 1.0;
        double m01 = A01 * b11 + A02 * b21, m02 = A01 * b12 + A02 * b22;
        double m11 = A11 * b11 + A12 * b21, m12 = A11 * b12 + A12 * b22;
        double m21 = A21 * b11 + A22 * b21, m22 = A21 * b12 + A22 * b22;
        // (M*C): C rows: row0=(cu,su,0), row1=(-su,cu,0), row2=(0,0,1).
        double t2_00 = m01 * -su, t2_01 = m01 * cu, t2_02 = m02;
        double t2_10 = m11 * -su, t2_11 = m11 * cu, t2_12 = m12;
        double t2_20 = m21 * -su, t2_21 = m21 * cu, t2_22 = m22;

        // Term3 = A * B * dC. dC rows: row0=(c00,c01,0), row1=(c10,c11,0), row2=0.
        // N = A*B:
        double n00 = A00 * 1.0, n01 = A01 * cv, n02 = A01 * sv;
        double n10 = A10 * 1.0, n11 = A11 * cv, n12 = A11 * sv;
        double n20 = 0.0, n21 = -sv, n22 = cv;
        double t3_00 = n00 * c00 + n01 * c10, t3_01 = n00 * c01 + n01 * c11, t3_02 = 0.0;
        double t3_10 = n10 * c00 + n11 * c10, t3_11 = n10 * c01 + n11 * c11, t3_12 = 0.0;
        double t3_20 = n20 * c00 + n21 * c10, t3_21 = n20 * c01 + n21 * c11, t3_22 = 0.0;

        rDot = new RotationMatrix.Derivative(
            t1_00 + t2_00 + t3_00, t1_01 + t2_01 + t3_01, t1_02 + t2_02 + t3_02,
            t1_10 + t2_10 + t3_10, t1_11 + t2_11 + t3_11, t1_12 + t2_12 + t3_12,
            t2_20 + t3_20, t2_21 + t3_21, t2_22 + t3_22);
    }

    /// <summary>
    /// ET seconds past J2000 (TDB) for a TDB epoch. Only TDB epochs are
    /// accepted here: UTC labels must first pass through the validated SN5
    /// conversion (Astronomy.Time), never an implicit shift. Non-TDB
    /// epochs are rejected rather than silently reinterpreted.
    /// </summary>
    public static double ToEphemerisSeconds(AstroTime epoch)
    {
        if (!double.IsFinite(epoch.JdUtc))
            throw new ArgumentException("Epoch JD must be finite.", nameof(epoch));
        if (epoch.OriginalScale != TimeScale.Tdb)
            throw new ArgumentException(
                $"IauEarthOrientation requires a TDB epoch; got {epoch.OriginalScale}. " +
                "Convert UTC via the SN5 TimeConverter first.", nameof(epoch));
        return (epoch.JdUtc - 2451545.0) * SecondsPerDay;
    }
}
