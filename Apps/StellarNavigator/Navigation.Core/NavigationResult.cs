using Astronomy.Core;

namespace Navigation.Core;

public sealed class NavigationResult<T>
{
    public required bool IsSuccess { get; init; }
    public NavigationResultStatus Status { get; init; }
    public T? Value { get; init; }
    public string? ErrorMessage { get; init; }
    public AstroTime? Epoch { get; init; }
    public ReferenceFrame? Frame { get; init; }
    public string? Provenance { get; init; }

    public static NavigationResult<T> Success(T value, NavigationResultStatus status,
        AstroTime? epoch = null, ReferenceFrame? frame = null, string? provenance = null) =>
        new() { IsSuccess = true, Value = value, Status = status, Epoch = epoch, Frame = frame, Provenance = provenance };

    public static NavigationResult<T> Failure(NavigationResultStatus status, string message) =>
        new() { IsSuccess = false, Status = status, ErrorMessage = message };
}
