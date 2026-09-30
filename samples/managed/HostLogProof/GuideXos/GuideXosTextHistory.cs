using System;

namespace HostLogProof;

/// <summary>A bounded identity for one state in a text history.</summary>
public readonly struct GuideXosTextRevision : IEquatable<GuideXosTextRevision>
{
    public GuideXosTextRevision(ulong generation, ulong sequence)
    {
        Generation = generation;
        Sequence = sequence;
    }

    public ulong Generation { get; }
    public ulong Sequence { get; }
    public bool IsValid => Generation != 0 && Sequence != 0;

    public bool Equals(GuideXosTextRevision other) =>
        Generation == other.Generation && Sequence == other.Sequence;

    public override bool Equals(object obj) =>
        obj is GuideXosTextRevision other && Equals(other);

    public override int GetHashCode() => HashCode.Combine(Generation, Sequence);

    public static bool operator ==(GuideXosTextRevision left,
        GuideXosTextRevision right) => left.Equals(right);

    public static bool operator !=(GuideXosTextRevision left,
        GuideXosTextRevision right) => !left.Equals(right);
}

/// <summary>
/// Fixed-capacity snapshots for a bounded text document. All snapshot storage
/// is allocated by the constructor; recording, undo, and redo allocate nothing.
/// </summary>
public sealed class GuideXosTextHistory
{
    public const int DefaultCapacity = 16;
    public const int MaximumSupportedCapacity = 32;

    private struct Snapshot
    {
        public int Length;
        public int Caret;
        public int Anchor;
        public int Reserved;
        public GuideXosTextRevision Revision;
    }

    private readonly int _maximumCharacters;
    private readonly int _capacity;
    private readonly char[] _textStorage;
    private readonly Snapshot[] _snapshots;
    private int _oldest;
    private int _count;
    private int _cursor;
    private ulong _revisionGeneration = 1;
    private ulong _revisionSequence;
    private bool _identityExhausted;

    public GuideXosTextHistory(int maximumCharacters,
        int capacity = DefaultCapacity)
    {
        if (maximumCharacters < 1 ||
            maximumCharacters > GuideXosTextArea.MaximumSupportedCharacters)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumCharacters));
        }
        if (capacity < 1 || capacity > MaximumSupportedCapacity)
        {
            throw new ArgumentOutOfRangeException(nameof(capacity));
        }

        _maximumCharacters = maximumCharacters;
        _capacity = capacity;
        _textStorage = new char[maximumCharacters * capacity];
        _snapshots = new Snapshot[capacity];
    }

    public int Capacity => _capacity;
    public int Count => _count;
    public int CurrentIndex => _cursor;
    public bool CanUndo => _count > 1 && _cursor > 0;
    public bool CanRedo => _count > 0 && _cursor + 1 < _count;
    public bool RevisionIdentityExhausted => _identityExhausted;
    public GuideXosTextRevision CurrentRevision => _count == 0
        ? default : _snapshots[SlotFor(_cursor)].Revision;

    /// <summary>Replaces all retained states with one authoritative baseline.</summary>
    public bool Reset(ReadOnlySpan<char> text, int caret, int anchor)
    {
        if (text.Length > _maximumCharacters || caret < 0 ||
            caret > text.Length || anchor < 0 || anchor > text.Length)
        {
            return false;
        }

        _oldest = 0;
        _count = 0;
        _cursor = 0;
        WriteSnapshot(0, text, caret, anchor, NextRevision());
        _count = 1;
        return true;
    }

    /// <summary>
    /// Appends a content state. Identical text updates caret metadata only and
    /// does not create a revision or discard a redo branch.
    /// </summary>
    public bool Record(ReadOnlySpan<char> text, int caret, int anchor)
    {
        if (_count == 0 || text.Length > _maximumCharacters || caret < 0 ||
            caret > text.Length || anchor < 0 || anchor > text.Length)
        {
            return false;
        }

        int currentSlot = SlotFor(_cursor);
        Snapshot current = _snapshots[currentSlot];
        ReadOnlySpan<char> previous = _textStorage.AsSpan(
            currentSlot * _maximumCharacters, current.Length);
        if (text.SequenceEqual(previous))
        {
            current.Caret = caret;
            current.Anchor = anchor;
            _snapshots[currentSlot] = current;
            return false;
        }

        _count = _cursor + 1;
        if (_count == _capacity)
        {
            if (_capacity == 1)
            {
                _oldest = 0;
                _count = 0;
                _cursor = 0;
            }
            else
            {
                _oldest = (_oldest + 1) % _capacity;
                --_count;
                --_cursor;
            }
        }

        int slot = (_oldest + _count) % _capacity;
        WriteSnapshot(slot, text, caret, anchor, NextRevision());
        ++_count;
        _cursor = _count - 1;
        return true;
    }

    /// <summary>Updates caret/selection for the displayed revision only.</summary>
    public bool UpdateCurrentCaret(int caret, int anchor)
    {
        if (_count == 0) return false;
        int slot = SlotFor(_cursor);
        Snapshot snapshot = _snapshots[slot];
        if (caret < 0 || caret > snapshot.Length || anchor < 0 ||
            anchor > snapshot.Length)
        {
            return false;
        }
        snapshot.Caret = caret;
        snapshot.Anchor = anchor;
        _snapshots[slot] = snapshot;
        return true;
    }

    public bool ContainsRevision(GuideXosTextRevision revision)
    {
        if (!revision.IsValid) return false;
        for (int offset = 0; offset < _count; offset++)
        {
            if (_snapshots[SlotFor(offset)].Revision == revision) return true;
        }
        return false;
    }

    public bool TryUndo(Span<char> destination, out int length,
        out int caret, out int anchor, out GuideXosTextRevision revision)
    {
        length = 0;
        caret = 0;
        anchor = 0;
        revision = default;
        if (!CanUndo) return false;

        Snapshot target = _snapshots[SlotFor(_cursor - 1)];
        if (destination.Length < target.Length) return false;
        --_cursor;
        return CopySnapshot(SlotFor(_cursor), destination, out length,
            out caret, out anchor, out revision);
    }

    public bool TryRedo(Span<char> destination, out int length,
        out int caret, out int anchor, out GuideXosTextRevision revision)
    {
        length = 0;
        caret = 0;
        anchor = 0;
        revision = default;
        if (!CanRedo) return false;

        Snapshot target = _snapshots[SlotFor(_cursor + 1)];
        if (destination.Length < target.Length) return false;
        ++_cursor;
        return CopySnapshot(SlotFor(_cursor), destination, out length,
            out caret, out anchor, out revision);
    }

    private void WriteSnapshot(int slot, ReadOnlySpan<char> text,
        int caret, int anchor, GuideXosTextRevision revision)
    {
        text.CopyTo(_textStorage.AsSpan(slot * _maximumCharacters,
            _maximumCharacters));
        _snapshots[slot] = new Snapshot
        {
            Length = text.Length,
            Caret = caret,
            Anchor = anchor,
            Reserved = 0,
            Revision = revision,
        };
    }

    private bool CopySnapshot(int slot, Span<char> destination,
        out int length, out int caret, out int anchor,
        out GuideXosTextRevision revision)
    {
        Snapshot snapshot = _snapshots[slot];
        if (destination.Length < snapshot.Length)
        {
            length = 0;
            caret = 0;
            anchor = 0;
            revision = default;
            return false;
        }
        _textStorage.AsSpan(slot * _maximumCharacters,
            snapshot.Length).CopyTo(destination);
        length = snapshot.Length;
        caret = snapshot.Caret;
        anchor = snapshot.Anchor;
        revision = snapshot.Revision;
        return true;
    }

    private int SlotFor(int offset) => (_oldest + offset) % _capacity;

    private GuideXosTextRevision NextRevision()
    {
        if (_identityExhausted) return default;
        if (_revisionSequence == ulong.MaxValue)
        {
            if (_revisionGeneration == ulong.MaxValue)
            {
                _identityExhausted = true;
                return default;
            }
            ++_revisionGeneration;
            _revisionSequence = 1;
        }
        else
        {
            ++_revisionSequence;
        }
        return new GuideXosTextRevision(_revisionGeneration,
            _revisionSequence);
    }
}
