using System;

namespace HostLogProof;

public enum GuideXosInputKind
{
    None = 0,
    PointerDown = 1,
    KeyDown = 2,
    KeyChar = 3,
}

/// <summary>
/// Platform-neutral input event produced by the shared managed input bridge.
/// Applications do not consume compositor or keyboard scan codes directly.
/// </summary>
public readonly struct GuideXosInputEvent
{
    private GuideXosInputEvent(
        GuideXosInputKind kind, int x, int y, uint keyCode, char character,
        bool shift)
    {
        Kind = kind;
        X = x;
        Y = y;
        KeyCode = keyCode;
        Character = character;
        Shift = shift;
    }

    public GuideXosInputKind Kind { get; }
    public int X { get; }
    public int Y { get; }
    public uint KeyCode { get; }
    public char Character { get; }
    public bool Shift { get; }

    internal static GuideXosInputEvent From(GuideXosLaunchContext context)
    {
        return new GuideXosInputEvent(
            context.InputKind, context.InputX, context.InputY,
            context.InputKeyCode, context.InputCharacter, context.InputShift);
    }
}

public enum GuideXosTextInputKey : uint
{
    Backspace = 8u,
    Enter = 10u,
    Tab = 9u,
    Escape = 27u,
    Up = 0x100u,
    Down = 0x101u,
    Left = 0x102u,
    Right = 0x103u,
    Home = 0x104u,
    End = 0x105u,
    Delete = 0x106u,
}

public enum GuideXosTextInputEditResult
{
    Ignored = 0,
    Changed = 1,
    Focused = 2,
    Submitted = 3,
    Cancelled = 4,
    Rejected = 5,
}

/// <summary>
/// Small bounded ASCII text field for filenames, forms, and dialog controls.
/// It owns text storage, focus, caret, editing, and terminal input state.
/// </summary>
public sealed class GuideXosTextInput
{
    public const int DefaultMaximumLength = 64;
    public const int MaximumSupportedLength = 127;

    private readonly char[] _buffer;
    private readonly string _placeholder;
    private int _length;
    private int _caretIndex;
    private bool _isFocused;
    private bool _isSubmitted;
    private bool _isCancelled;
    private bool _hasChanged;
    private uint _rejectedInputCount;

    public GuideXosTextInput(
        int maximumLength = DefaultMaximumLength, string placeholder = "")
    {
        if (maximumLength < 1 || maximumLength > MaximumSupportedLength)
        {
            maximumLength = DefaultMaximumLength;
        }
        _buffer = new char[maximumLength];
        _placeholder = placeholder ?? string.Empty;
    }

    public int MaximumLength => _buffer.Length;
    public int Length => _length;
    public int CaretIndex => _caretIndex;
    public bool IsFocused => _isFocused;
    public bool IsSubmitted => _isSubmitted;
    public bool IsCancelled => _isCancelled;
    public bool HasChanged => _hasChanged;
    public uint RejectedInputCount => _rejectedInputCount;
    public string Value => new string(_buffer, 0, _length);

    public void Focus()
    {
        _isFocused = true;
        _isSubmitted = false;
        _isCancelled = false;
    }

    public void Blur()
    {
        _isFocused = false;
    }

    public void ResetTransientState()
    {
        _isFocused = false;
        _isSubmitted = false;
        _isCancelled = false;
        _hasChanged = false;
        _rejectedInputCount = 0u;
    }

    /// <summary>Direct initialization/helper API; keyboard proof uses HandleKey.</summary>
    public bool SetValue(string value)
    {
        if (value == null || value.Length > MaximumLength) return false;
        for (int index = 0; index < value.Length; index++)
        {
            if (!IsSupportedCharacter(value[index])) return false;
        }
        for (int index = 0; index < value.Length; index++) _buffer[index] = value[index];
        _length = value.Length;
        _caretIndex = _length;
        _hasChanged = false;
        _isSubmitted = false;
        _isCancelled = false;
        return true;
    }

    public GuideXosTextInputEditResult HandlePointerDown(int x, int y)
    {
        // The picker performs the field-region hit test. Once routed here,
        // pointer coordinates are intentionally no longer part of control state.
        Focus();
        return GuideXosTextInputEditResult.Focused;
    }

    public GuideXosTextInputEditResult HandleCharacter(char value)
    {
        if (!_isFocused) return GuideXosTextInputEditResult.Ignored;
        if (!IsSupportedCharacter(value))
        {
            ++_rejectedInputCount;
            return GuideXosTextInputEditResult.Rejected;
        }
        if (_length >= MaximumLength)
        {
            ++_rejectedInputCount;
            return GuideXosTextInputEditResult.Rejected;
        }
        for (int index = _length; index > _caretIndex; index--)
        {
            _buffer[index] = _buffer[index - 1];
        }
        _buffer[_caretIndex++] = value;
        ++_length;
        _hasChanged = true;
        return GuideXosTextInputEditResult.Changed;
    }

    public GuideXosTextInputEditResult HandleKey(GuideXosTextInputKey key)
    {
        if (!_isFocused) return GuideXosTextInputEditResult.Ignored;
        switch (key)
        {
            case GuideXosTextInputKey.Backspace:
                if (_caretIndex == 0) return GuideXosTextInputEditResult.Ignored;
                for (int index = _caretIndex - 1; index < _length - 1; index++)
                {
                    _buffer[index] = _buffer[index + 1];
                }
                --_caretIndex;
                --_length;
                _hasChanged = true;
                return GuideXosTextInputEditResult.Changed;
            case GuideXosTextInputKey.Delete:
                if (_caretIndex >= _length) return GuideXosTextInputEditResult.Ignored;
                for (int index = _caretIndex; index < _length - 1; index++)
                {
                    _buffer[index] = _buffer[index + 1];
                }
                --_length;
                _hasChanged = true;
                return GuideXosTextInputEditResult.Changed;
            case GuideXosTextInputKey.Left:
                if (_caretIndex == 0) return GuideXosTextInputEditResult.Ignored;
                --_caretIndex;
                return GuideXosTextInputEditResult.Changed;
            case GuideXosTextInputKey.Right:
                if (_caretIndex >= _length) return GuideXosTextInputEditResult.Ignored;
                ++_caretIndex;
                return GuideXosTextInputEditResult.Changed;
            case GuideXosTextInputKey.Enter:
                _isSubmitted = true;
                _isFocused = false;
                return GuideXosTextInputEditResult.Submitted;
            case GuideXosTextInputKey.Escape:
                _isCancelled = true;
                _isFocused = false;
                return GuideXosTextInputEditResult.Cancelled;
            default:
                ++_rejectedInputCount;
                return GuideXosTextInputEditResult.Rejected;
        }
    }

    /// <summary>
    /// Renders a bounded single-line field through the existing surface text API.
    /// The caret is represented by a visible vertical bar while focused.
    /// </summary>
    public GuideXosResult Render(
        GuideXosSurface surface, int x, int y, ReadOnlySpan<byte> label)
    {
        if (surface == null || x < 0 || y < 0 || label.Length > 24)
        {
            return GuideXosResult.InvalidArgument;
        }
        Span<byte> line = stackalloc byte[64];
        int position = 0;
        if (!GuideXosText.Append(line, ref position, label) ||
            !GuideXosText.Append(line, ref position, "[ "u8))
        {
            return GuideXosResult.InvalidArgument;
        }
        int visibleLimit = line.Length - position - 2;
        if (_length == 0 && !_isFocused && _placeholder.Length != 0)
        {
            for (int index = 0; index < _placeholder.Length && index < visibleLimit; index++)
            {
                line[position++] = (byte)_placeholder[index];
            }
        }
        else
        {
            int shown = Math.Min(_length, visibleLimit);
            for (int index = 0; index < shown; index++)
            {
                line[position++] = (byte)_buffer[index];
            }
            if (_isFocused && position < line.Length - 1)
            {
                line[position++] = (byte)'|';
            }
        }
        if (!GuideXosText.Append(line, ref position, " ]"u8))
        {
            return GuideXosResult.InvalidArgument;
        }
        return surface.TrySetText(x, y, line[..position]);
    }

    private static bool IsSupportedCharacter(char value)
    {
        return value >= 0x20 && value <= 0x7E;
    }
}
