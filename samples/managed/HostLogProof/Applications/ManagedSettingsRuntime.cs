using System;

namespace HostLogProof.Applications;

/// <summary>The small immutable runtime view used by shared managed controls.</summary>
internal readonly struct GuideXosRuntimeSettingsSnapshot
{
    public GuideXosRuntimeSettingsSnapshot(bool naturalScroll) =>
        NaturalScroll = naturalScroll;

    public bool NaturalScroll { get; }
    public static GuideXosRuntimeSettingsSnapshot Defaults => new(false);
}

/// <summary>
/// Owns the currently active, typed settings that have a shared runtime
/// effect. A candidate is published as one value after semantic validation.
/// </summary>
internal sealed class GuideXosRuntimeSettingsState
{
    private GuideXosRuntimeSettingsSnapshot _current =
        GuideXosRuntimeSettingsSnapshot.Defaults;

    public bool IsReady { get; private set; }
    public GuideXosRuntimeSettingsSnapshot Current => _current;

    public bool TryCommit(ManagedSettingsSnapshot candidate)
    {
        if (!ManagedSettingsStore.IsValid(candidate)) return false;

        GuideXosRuntimeSettingsSnapshot next =
            new(candidate.NaturalScroll);
        _current = next;
        IsReady = true;
        return true;
    }

    internal RuntimeSettingsTestCapture CaptureForTests() =>
        new(_current, IsReady);

    internal void RestoreForTests(RuntimeSettingsTestCapture capture)
    {
        _current = capture.Current;
        IsReady = capture.IsReady;
    }
}

internal readonly struct RuntimeSettingsTestCapture
{
    public RuntimeSettingsTestCapture(
        GuideXosRuntimeSettingsSnapshot current, bool isReady)
    {
        Current = current;
        IsReady = isReady;
    }

    public GuideXosRuntimeSettingsSnapshot Current { get; }
    public bool IsReady { get; }
}

/// <summary>
/// Loads the C146 semantic snapshot once and publishes its supported runtime
/// fields before the first managed application is dispatched.
/// </summary>
internal sealed class ManagedSettingsRuntimeStartup
{
    private readonly GuideXosRuntimeSettingsState _runtime;
    private ManagedSettingsStore _store;
    private ManagedSettingsLoadResult _loadResult;
    private ManagedSettingsSnapshot _snapshot = ManagedSettingsSnapshot.Defaults;

    public ManagedSettingsRuntimeStartup(GuideXosRuntimeSettingsState runtime)
    {
        _runtime = runtime;
    }

    public bool IsInitialized { get; private set; }
    public ManagedSettingsStore Store => _store;
    public ManagedSettingsLoadResult LoadResult => _loadResult;
    public ManagedSettingsSnapshot Snapshot => _snapshot;

    public bool Initialize(ManagedSettingsStore store)
    {
        if (IsInitialized) return true;
        if (store == null || _runtime == null) return false;

        ManagedSettingsLoadResult load = store.Load();
        ManagedSettingsSnapshot snapshot =
            load.Status == ManagedSettingsLoadStatus.Loaded
                ? load.Snapshot
                : ManagedSettingsSnapshot.Defaults;
        if (!ManagedSettingsStore.IsValid(snapshot))
        {
            snapshot = ManagedSettingsSnapshot.Defaults;
        }
        if (!_runtime.TryCommit(snapshot)) return false;

        _store = store;
        _loadResult = load;
        _snapshot = snapshot;
        IsInitialized = true;
        return true;
    }

    public bool CanCommit(ManagedSettingsSnapshot candidate) =>
        IsInitialized && ManagedSettingsStore.IsValid(candidate);

    /// <summary>Called only after a verified save, so commit cannot fail.</summary>
    public void CommitPersisted(ManagedSettingsSnapshot candidate)
    {
        if (!CanCommit(candidate))
        {
            throw new InvalidOperationException(
                "Runtime settings must be validated before persistence.");
        }

        bool committed = _runtime.TryCommit(candidate);
        if (!committed)
        {
            throw new InvalidOperationException(
                "A validated runtime settings snapshot was rejected.");
        }
        _snapshot = candidate;
    }

    internal static ManagedSettingsSnapshot ResolveSnapshot(
        ManagedSettingsLoadResult load) =>
        load.Status == ManagedSettingsLoadStatus.Loaded &&
            ManagedSettingsStore.IsValid(load.Snapshot)
            ? load.Snapshot
            : ManagedSettingsSnapshot.Defaults;
}

/// <summary>Single applied runtime state shared by production controls.</summary>
internal static class GuideXosRuntimeSettings
{
    private static readonly GuideXosRuntimeSettingsState s_active = new();
    private static readonly ManagedSettingsRuntimeStartup s_startup =
        new(s_active);

    public static GuideXosRuntimeSettingsState Active => s_active;
    public static ManagedSettingsRuntimeStartup Startup => s_startup;
    public static GuideXosRuntimeSettingsSnapshot Current => s_active.Current;

    public static bool EnsureInitialized(GuideXosHost host)
    {
        if (host == null || host.IsCapabilityProbe) return true;
        if (s_startup.IsInitialized) return true;

        var store = new ManagedSettingsStore(new ManagedSettingsVfsAccess(host));
        if (!s_startup.Initialize(store))
        {
            // Canonical defaults are valid, so this is only a defensive
            // startup fallback for an unexpected runtime-state defect.
            var fallback = new ManagedSettingsStore(new MissingSettingsFiles());
            if (!s_startup.Initialize(fallback)) return false;
        }

        LogStartup(host, s_startup.LoadResult, s_active.Current);
        return s_active.IsReady;
    }

    public static bool CanCommit(ManagedSettingsSnapshot candidate) =>
        s_startup.CanCommit(candidate);

    public static void CommitPersisted(GuideXosHost host,
        ManagedSettingsSnapshot candidate)
    {
        bool oldNatural = s_active.Current.NaturalScroll;
        s_startup.CommitPersisted(candidate);
        Span<byte> line = stackalloc byte[127];
        int position = 0;
        if (GuideXosText.Append(line, ref position,
                "C147-RUNTIME-APPLY source=SettingsCenter oldNatural="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, oldNatural ? 1u : 0u) &&
            GuideXosText.Append(line, ref position, " newNatural="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                candidate.NaturalScroll ? 1u : 0u) &&
            GuideXosText.Append(line, ref position,
                " persistence=verified runtime=committed result=PASS"u8))
        {
            host?.TryLog(line[..position]);
        }
    }

    public static int TransformWheelDelta(int wheelDelta) =>
        s_active.Current.NaturalScroll ? -wheelDelta : wheelDelta;

    private static void LogStartup(GuideXosHost host,
        ManagedSettingsLoadResult load,
        GuideXosRuntimeSettingsSnapshot runtime)
    {
        ReadOnlySpan<byte> source = load.Status switch
        {
            ManagedSettingsLoadStatus.Loaded => "file"u8,
            ManagedSettingsLoadStatus.Missing => "missing"u8,
            ManagedSettingsLoadStatus.Invalid => "invalid"u8,
            _ => "io-failure"u8,
        };
        Span<byte> line = stackalloc byte[127];
        int position = 0;
        if (GuideXosText.Append(line, ref position,
                "C147-RUNTIME-SETTING source="u8) &&
            GuideXosText.Append(line, ref position, source) &&
            GuideXosText.Append(line, ref position, " naturalScroll="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                runtime.NaturalScroll ? 1u : 0u) &&
            GuideXosText.Append(line, ref position,
                " ready=true before-application=true result=PASS"u8))
        {
            host?.TryLog(line[..position]);
        }
    }

    private sealed class MissingSettingsFiles : ManagedSettingsFileAccessBase
    {
        public override GuideXosFileResult TryGetInfo(out GuideXosFileInfo info)
        {
            info = null;
            return GuideXosFileResult.NotFound;
        }

        public override GuideXosFileResult ReadAllBytes(out byte[] data)
        {
            data = null;
            return GuideXosFileResult.NotFound;
        }

        public override GuideXosFileResult WriteAllBytes(ReadOnlySpan<byte> data) =>
            GuideXosFileResult.IoFailure;
    }
}
