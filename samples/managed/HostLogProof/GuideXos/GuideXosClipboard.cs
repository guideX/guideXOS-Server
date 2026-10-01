using System;

namespace HostLogProof;

/// <summary>
/// One transient managed-runtime text slot. Storage is fixed at 256 bytes and
/// accepts the same printable ASCII plus LF encoding as GuideXosTextArea.
/// </summary>
public sealed class GuideXosClipboard
{
    public const int Capacity = 256;

    private static readonly GuideXosClipboard s_shared = new();
    private readonly byte[] _storage = new byte[Capacity];
    private int _length;

    public static GuideXosClipboard Shared => s_shared;
    public bool HasText => _length != 0;
    public int Length => _length;
    public ReadOnlySpan<byte> TextSpan => _storage.AsSpan(0, _length);

    /// <summary>Clears the transient text without allocating.</summary>
    public void Clear()
    {
        Array.Clear(_storage);
        _length = 0;
    }

    /// <summary>
    /// Replaces the slot atomically when the text is valid and bounded.
    /// Invalid input leaves the previous clipboard contents intact.
    /// </summary>
    public bool TrySetText(ReadOnlySpan<byte> text)
    {
        if (text.Length > Capacity || !IsSupportedText(text)) return false;
        text.CopyTo(_storage);
        if (text.Length < _length)
            Array.Clear(_storage, text.Length, _length - text.Length);
        _length = text.Length;
        return true;
    }

    /// <summary>Copies exact contents into caller-owned bounded storage.</summary>
    public bool TryCopyText(Span<byte> destination, out int written)
    {
        written = _length;
        if (destination.Length < _length) return false;
        TextSpan.CopyTo(destination);
        return true;
    }

    internal bool TryCopySelectionFrom(GuideXosTextArea textArea)
    {
        if (textArea == null || !textArea.HasSelection) return false;
        Span<byte> selected = stackalloc byte[Capacity];
        return textArea.TryCopySelectionTo(selected, out int written) &&
            TrySetText(selected[..written]);
    }

    private static bool IsSupportedText(ReadOnlySpan<byte> text)
    {
        for (int index = 0; index < text.Length; index++)
        {
            byte value = text[index];
            if (value != (byte)'\n' && (value < 0x20 || value > 0x7E))
                return false;
        }
        return true;
    }
}
