using System;

namespace HostLogProof;

public enum GuideXosAssociationStatus
{
    Success = 0, NotSupported = 1, InvalidArgument = 2,
    UnknownExtension = 3, IneligibleHandler = 4,
    CapacityExceeded = 5, PersistenceFailed = 6,
    InvalidPersistedState = 7, CapabilityUnavailable = 8,
    NativeFailure = 9, InvalidResponse = 10,
}

public enum GuideXosAssociationOverrideState : uint
{
    NoOverride = 0, ApplicationOverride = 1, Disabled = 2,
}

public readonly struct GuideXosAssociationResult
{
    public readonly GuideXosAssociationStatus Status;
    public readonly NativeAssociationServiceResponse Response;
    internal GuideXosAssociationResult(GuideXosAssociationStatus status,
        NativeAssociationServiceResponse response)
    { Status = status; Response = response; }
}

/// <summary>Bounded access to the single native App Model association authority.</summary>
public static unsafe class GuideXosFileAssociations
{
    private const uint QueryOperation = 0u;
    private const uint SetOperation = 1u;
    private const uint DisableOperation = 2u;
    private const uint ResetOperation = 3u;
    private const uint DiagnosticsOperation = 4u;

    public static GuideXosAssociationResult Query(NativeGxAppContext* context,
        NativeHostCallTable* host, ReadOnlySpan<byte> extension) =>
        Invoke(context, host, QueryOperation, extension, default);

    public static GuideXosAssociationResult SetDefault(NativeGxAppContext* context,
        NativeHostCallTable* host, ReadOnlySpan<byte> extension,
        ReadOnlySpan<byte> applicationId) =>
        Invoke(context, host, SetOperation, extension, applicationId);

    public static GuideXosAssociationResult Disable(NativeGxAppContext* context,
        NativeHostCallTable* host, ReadOnlySpan<byte> extension) =>
        Invoke(context, host, DisableOperation, extension, default);

    public static GuideXosAssociationResult Reset(NativeGxAppContext* context,
        NativeHostCallTable* host, ReadOnlySpan<byte> extension) =>
        Invoke(context, host, ResetOperation, extension, default);

#if GUIDEXOS_PROOF_CONTROL
    public static GuideXosAssociationResult Diagnostics(NativeGxAppContext* context,
        NativeHostCallTable* host, ReadOnlySpan<byte> extension) =>
        Invoke(context, host, DiagnosticsOperation, extension, default);
#endif

    private static GuideXosAssociationResult Invoke(NativeGxAppContext* context,
        NativeHostCallTable* host, uint operation, ReadOnlySpan<byte> extension,
        ReadOnlySpan<byte> applicationId)
    {
        NativeAssociationServiceResponse empty = default;
        if (context == null || host == null || extension.Length < 2 ||
            extension.Length >= 16 || extension[0] != (byte)'.' ||
            (operation == SetOperation && (applicationId.Length == 0 || applicationId.Length >= 96)))
            return new(GuideXosAssociationStatus.InvalidArgument, empty);
        if (host->version < 4u || host->size < GxAbi.AssociationServiceOffset + (uint)sizeof(ulong))
            return new(GuideXosAssociationStatus.NotSupported, empty);
        if ((host->capabilities & GxAbi.CapabilityAssociationService) == 0u)
            return new(GuideXosAssociationStatus.CapabilityUnavailable, empty);
        if (host->associationService == null)
            return new(GuideXosAssociationStatus.NotSupported, empty);

        NativeAssociationServiceRequest request = default;
        request.operation = operation;
        byte* extensionTarget = request.extension;
        extension.CopyTo(new Span<byte>(extensionTarget, 16));
        if (operation == SetOperation)
        {
            byte* applicationTarget = request.applicationId;
            applicationId.CopyTo(new Span<byte>(applicationTarget, 96));
        }
        NativeAssociationServiceResponse response = default;
        int native = host->associationService(context, &request, &response);
        if (native == -5) return new(GuideXosAssociationStatus.NotSupported, empty);
        if (native == -3) return new(GuideXosAssociationStatus.CapabilityUnavailable, empty);
        if (native == -2) return new(GuideXosAssociationStatus.InvalidArgument, empty);
        if (native < 0 || native > 7 || response.status != (uint)native ||
            response.overrideState > (uint)GuideXosAssociationOverrideState.Disabled ||
            response.hasEffectiveAssociation > 1u)
            return new(GuideXosAssociationStatus.InvalidResponse, empty);
        if (native != 0) return new((GuideXosAssociationStatus)native, response);
        byte* compiled = response.compiledDefaultAppId;
        byte* overridden = response.overrideAppId;
        byte* effective = response.effectiveAppId;
        byte* normalized = response.normalizedExtension;
        if (operation == DiagnosticsOperation)
        {
            if (!Terminated(normalized, 16) || normalized[0] != (byte)'.' ||
                response.diagnosticSlot != 0u && response.diagnosticSlot != 1u &&
                    response.diagnosticSlot != 0xFFFFFFFFu)
                return new(GuideXosAssociationStatus.InvalidResponse, empty);
            for (int i = 1; i < extension.Length; ++i)
            {
                byte expected = extension[i];
                if (expected >= (byte)'A' && expected <= (byte)'Z')
                    expected = (byte)(expected + 32);
                if (normalized[i] != expected)
                    return new(GuideXosAssociationStatus.InvalidResponse, empty);
            }
            if (normalized[extension.Length] != 0)
                return new(GuideXosAssociationStatus.InvalidResponse, empty);
            return new(GuideXosAssociationStatus.Success, response);
        }
        if (!Terminated(compiled, 96) || !Terminated(overridden, 96) ||
            !Terminated(effective, 96) ||
            !Terminated(normalized, 16) || normalized[0] != (byte)'.' ||
            ((effective[0] != 0) != (response.hasEffectiveAssociation != 0)) ||
            (response.overrideState == (uint)GuideXosAssociationOverrideState.Disabled &&
                (effective[0] != 0 || overridden[0] != 0)))
            return new(GuideXosAssociationStatus.InvalidResponse, empty);
        for (int i = 1; i < extension.Length; ++i)
        {
            byte expected = extension[i];
            if (expected >= (byte)'A' && expected <= (byte)'Z')
                expected = (byte)(expected + 32);
            if (normalized[i] != expected)
                return new(GuideXosAssociationStatus.InvalidResponse, empty);
        }
        if (normalized[extension.Length] != 0)
            return new(GuideXosAssociationStatus.InvalidResponse, empty);
        return new(GuideXosAssociationStatus.Success, response);
    }

    private static bool Terminated(byte* bytes, int capacity)
    {
        for (int i = 0; i < capacity; ++i) if (bytes[i] == 0) return true;
        return false;
    }
}
