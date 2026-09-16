using System;

namespace HostLogProof;

public enum GuideXosTextAreaEditResult
{
    Ignored = 0,
    Changed = 1,
    Focused = 2,
    Moved = 3,
    Submitted = 4,
    Cancelled = 5,
    Rejected = 6,
}

/// <summary>
/// A bounded, platform-neutral multiline editor. The document is stored as
/// one fixed UTF-16 buffer with LF line separators. No application-specific
/// keyboard or compositor state is kept by the control.
/// </summary>
public sealed class GuideXosTextArea
{
    public const int DefaultMaximumCharacters = 256;
    public const int MaximumSupportedCharacters = 1024;
    public const int DefaultMaximumLines = 32;
    public const int MaximumSupportedLines = 128;
    public const int DefaultVisibleLineCount = 4;
    public const int MaximumVisibleLineCount = 16;
    public const int DefaultMaximumRenderableColumns = 48;
    public const int MaximumSupportedRenderableColumns = 56;

    private readonly char[] _buffer;
    private readonly int _maximumLines;
    private readonly int _maximumRenderableColumns;
    private readonly int _visibleLineCount;
    private int _length;
    private int _lineCount = 1;
    private int _caretIndex;
    private int _anchorIndex;
    private int _firstVisibleLine;
    private int _preferredColumn = -1;
    private bool _isFocused;
    private bool _isSubmitted;
    private bool _isCancelled;
    private uint _rejectedInputCount;

    public GuideXosTextArea(
        int maximumCharacters = DefaultMaximumCharacters,
        int maximumLines = DefaultMaximumLines,
        int visibleLineCount = DefaultVisibleLineCount,
        int maximumRenderableColumns = DefaultMaximumRenderableColumns)
    {
        if (maximumCharacters < 1 || maximumCharacters > MaximumSupportedCharacters)
        {
            maximumCharacters = DefaultMaximumCharacters;
        }
        if (maximumLines < 1 || maximumLines > MaximumSupportedLines)
        {
            maximumLines = DefaultMaximumLines;
        }
        if (visibleLineCount < 1 || visibleLineCount > MaximumVisibleLineCount)
        {
            visibleLineCount = DefaultVisibleLineCount;
        }
        if (maximumRenderableColumns < 1 ||
            maximumRenderableColumns > MaximumSupportedRenderableColumns)
        {
            maximumRenderableColumns = DefaultMaximumRenderableColumns;
        }

        _buffer = new char[maximumCharacters];
        _maximumLines = maximumLines;
        _visibleLineCount = visibleLineCount;
        _maximumRenderableColumns = maximumRenderableColumns;
    }

    public int MaximumCharacters => _buffer.Length;
    public int MaximumLines => _maximumLines;
    public int MaximumRenderableColumns => _maximumRenderableColumns;
    public int VisibleLineCount => _visibleLineCount;
    public int Length => _length;
    public int LineCount => _lineCount;
    public int CaretIndex => _caretIndex;
    public int AnchorIndex => _anchorIndex;
    public int SelectionStart => Math.Min(_anchorIndex, _caretIndex);
    public int SelectionEnd => Math.Max(_anchorIndex, _caretIndex);
    public bool HasSelection => _anchorIndex != _caretIndex;
    public bool IsFocused => _isFocused;
    public bool IsSubmitted => _isSubmitted;
    public bool IsCancelled => _isCancelled;
    public int FirstVisibleLine => _firstVisibleLine;
    public int CaretLine => GetLineAndColumn(_caretIndex, out _);
    public int CaretColumn
    {
        get
        {
            GetLineAndColumn(_caretIndex, out int column);
            return column;
        }
    }
    public int PreferredColumn => _preferredColumn;
    public uint RejectedInputCount => _rejectedInputCount;
    public string Text => new string(_buffer, 0, _length);
    public string SelectedText => HasSelection
        ? new string(_buffer, SelectionStart, SelectionEnd - SelectionStart)
        : string.Empty;

    public void Focus()
    {
        _isFocused = true;
        _isSubmitted = false;
        _isCancelled = false;
        EnsureCaretVisible();
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
        _rejectedInputCount = 0u;
    }

    /// <summary>
    /// Replaces the whole document atomically. Supported content is bounded
    /// printable ASCII plus LF; CR and other control characters are rejected.
    /// </summary>
    public bool SetText(string value)
    {
        if (value == null || value.Length > MaximumCharacters)
        {
            return false;
        }
        if (!ValidateText(value.AsSpan(), out int lineCount) ||
            lineCount > _maximumLines)
        {
            return false;
        }
        value.AsSpan().CopyTo(_buffer);
        _length = value.Length;
        _lineCount = lineCount;
        ResetCaretToEnd();
        return true;
    }

    /// <summary>Copies bounded ASCII/UTF-8 bytes without using a decoder.</summary>
    public bool SetUtf8(ReadOnlySpan<byte> value)
    {
        if (value.Length > MaximumCharacters)
        {
            return false;
        }
        if (!ValidateUtf8(value, out int lineCount) ||
            lineCount > _maximumLines)
        {
            return false;
        }
        for (int index = 0; index < value.Length; index++)
        {
            _buffer[index] = (char)value[index];
        }
        _length = value.Length;
        _lineCount = lineCount;
        ResetCaretToEnd();
        return true;
    }

    /// <summary>Returns an exact bounded UTF-8/ASCII copy for a file service.</summary>
    public byte[] ToUtf8()
    {
        byte[] result = new byte[_length];
        for (int index = 0; index < _length; index++)
        {
            result[index] = (byte)_buffer[index];
        }
        return result;
    }

    public bool TryCopyUtf8To(Span<byte> destination, out int written)
    {
        written = _length;
        if (destination.Length < _length) return false;
        for (int index = 0; index < _length; index++)
        {
            destination[index] = (byte)_buffer[index];
        }
        return true;
    }

    /// <summary>Places a newly loaded document at its deterministic start.</summary>
    public void SetCaretToStart()
    {
        _caretIndex = 0;
        _anchorIndex = 0;
        _preferredColumn = -1;
        EnsureCaretVisible();
    }

    /// <summary>
    /// Maps a local surface coordinate to the nearest character position in
    /// the visible document and gives the control focus.
    /// </summary>
    public GuideXosTextAreaEditResult HandlePointerDown(
        int x,
        int y,
        int originX,
        int originY,
        int characterWidth = 8,
        int lineHeight = 18)
    {
        if (characterWidth < 1 || lineHeight < 1 || x < originX || y < originY ||
            x >= originX + _maximumRenderableColumns * characterWidth ||
            y >= originY + _visibleLineCount * lineHeight)
        {
            return GuideXosTextAreaEditResult.Ignored;
        }

        int row = (y - originY) / lineHeight;
        int line = _firstVisibleLine + row;
        if (line >= _lineCount) line = _lineCount - 1;
        int column = (x - originX) / characterWidth;
        int lineLength = GetLineLength(line);
        if (column > lineLength) column = lineLength;
        int target = GetLineStart(line) + column;
        _caretIndex = target;
        _anchorIndex = target;
        _preferredColumn = -1;
        Focus();
        return GuideXosTextAreaEditResult.Focused;
    }

    public GuideXosTextAreaEditResult HandleCharacter(char value)
    {
        if (!_isFocused) return GuideXosTextAreaEditResult.Ignored;
        if (!IsSupportedCharacter(value)) return RejectInput();
        return ReplaceSelection(value);
    }

    public GuideXosTextAreaEditResult HandleKey(
        GuideXosTextInputKey key, bool shift = false)
    {
        if (!_isFocused) return GuideXosTextAreaEditResult.Ignored;

        switch (key)
        {
            case GuideXosTextInputKey.Backspace:
                return DeleteBackward();
            case GuideXosTextInputKey.Delete:
                return DeleteForward();
            case GuideXosTextInputKey.Enter:
                return ReplaceSelection('\n');
            case GuideXosTextInputKey.Left:
                return MoveHorizontal(-1, shift);
            case GuideXosTextInputKey.Right:
                return MoveHorizontal(1, shift);
            case GuideXosTextInputKey.Up:
                return MoveVertical(-1, shift);
            case GuideXosTextInputKey.Down:
                return MoveVertical(1, shift);
            case GuideXosTextInputKey.Home:
                return MoveToLineEdge(false, shift);
            case GuideXosTextInputKey.End:
                return MoveToLineEdge(true, shift);
            case GuideXosTextInputKey.Escape:
                _isCancelled = true;
                _isFocused = false;
                return GuideXosTextAreaEditResult.Cancelled;
            default:
                return RejectInput();
        }
    }

    /// <summary>
    /// Renders visible lines using the existing managed surface text path.
    /// Brackets show selected ranges and a vertical bar shows the caret.
    /// Long logical lines are clipped deterministically; no horizontal scroll
    /// state is hidden in the control.
    /// </summary>
    public GuideXosResult Render(
        GuideXosSurface surface,
        int x,
        int y,
        int lineHeight = 18)
    {
        if (surface == null || x < 0 || y < 0 || lineHeight < 1)
        {
            return GuideXosResult.InvalidArgument;
        }

        int selectionStart = SelectionStart;
        int selectionEnd = SelectionEnd;
        Span<byte> rendered = stackalloc byte[64];
        for (int row = 0; row < _visibleLineCount; row++)
        {
            int line = _firstVisibleLine + row;
            rendered.Clear();
            int position = 0;
            if (!AppendByte(rendered, ref position,
                    line == CaretLine ? (byte)'>' : (byte)' ' ) ||
                !AppendByte(rendered, ref position, (byte)' '))
            {
                return GuideXosResult.InvalidArgument;
            }

            if (line < _lineCount)
            {
                int lineStart = GetLineStart(line);
                int lineLength = GetLineLength(line);
                int visibleLength = Math.Min(lineLength, _maximumRenderableColumns);
                for (int column = 0; column <= visibleLength; column++)
                {
                    int index = lineStart + column;
                    if (_isFocused && index == _caretIndex &&
                        !AppendByte(rendered, ref position, (byte)'|'))
                    {
                        return GuideXosResult.InvalidArgument;
                    }
                    if (column == visibleLength) break;

                    bool selected = HasSelection &&
                        index >= selectionStart && index < selectionEnd;
                    bool selectedBefore = column > 0 &&
                        index - 1 >= selectionStart && index - 1 < selectionEnd;
                    if (selected && !selectedBefore)
                    {
                        if (!AppendByte(rendered, ref position, (byte)'['))
                        {
                            return GuideXosResult.InvalidArgument;
                        }
                    }
                    if (!AppendByte(rendered, ref position, (byte)_buffer[index]))
                    {
                        return GuideXosResult.InvalidArgument;
                    }
                    bool selectedAfter = column + 1 < visibleLength &&
                        index + 1 >= selectionStart && index + 1 < selectionEnd;
                    if (selected && !selectedAfter)
                    {
                        if (!AppendByte(rendered, ref position, (byte)']'))
                        {
                            return GuideXosResult.InvalidArgument;
                        }
                    }
                }
            }

            if (surface.TrySetText(x, y + row * lineHeight, rendered[..position]) !=
                GuideXosResult.Success)
            {
                return GuideXosResult.InvalidArgument;
            }
        }
        return GuideXosResult.Success;
    }

    private GuideXosTextAreaEditResult DeleteBackward()
    {
        if (HasSelection)
        {
            int start = SelectionStart;
            RemoveRange(SelectionStart, SelectionEnd);
            _caretIndex = start;
            _anchorIndex = start;
            return GuideXosTextAreaEditResult.Changed;
        }
        if (_caretIndex == 0) return GuideXosTextAreaEditResult.Ignored;
        int target = _caretIndex - 1;
        RemoveRange(target, _caretIndex);
        _caretIndex = target;
        _anchorIndex = target;
        return GuideXosTextAreaEditResult.Changed;
    }

    private GuideXosTextAreaEditResult DeleteForward()
    {
        if (HasSelection)
        {
            int start = SelectionStart;
            RemoveRange(SelectionStart, SelectionEnd);
            _caretIndex = start;
            _anchorIndex = start;
            return GuideXosTextAreaEditResult.Changed;
        }
        if (_caretIndex >= _length) return GuideXosTextAreaEditResult.Ignored;
        RemoveRange(_caretIndex, _caretIndex + 1);
        return GuideXosTextAreaEditResult.Changed;
    }

    private GuideXosTextAreaEditResult ReplaceSelection(char value)
    {
        int start = SelectionStart;
        int end = SelectionEnd;
        int removedNewlines = CountNewlines(start, end);
        int insertedNewlines = value == '\n' ? 1 : 0;
        int newLength = _length - (end - start) + 1;
        int newLineCount = _lineCount - removedNewlines + insertedNewlines;
        if (newLength > MaximumCharacters || newLineCount > _maximumLines)
        {
            return RejectInput();
        }

        if (end != start)
        {
            RemoveRange(start, end);
            _caretIndex = start;
        }
        for (int index = _length; index > _caretIndex; index--)
        {
            _buffer[index] = _buffer[index - 1];
        }
        _buffer[_caretIndex++] = value;
        _length++;
        _lineCount = newLineCount;
        _anchorIndex = _caretIndex;
        _preferredColumn = -1;
        EnsureCaretVisible();
        return value == '\n'
            ? GuideXosTextAreaEditResult.Changed
            : GuideXosTextAreaEditResult.Changed;
    }

    private GuideXosTextAreaEditResult MoveHorizontal(int direction, bool shift)
    {
        int target;
        if (!shift && HasSelection)
        {
            target = direction < 0 ? SelectionStart : SelectionEnd;
        }
        else
        {
            target = _caretIndex + direction;
            if (target < 0 || target > _length) return GuideXosTextAreaEditResult.Ignored;
        }
        _caretIndex = target;
        if (!shift) _anchorIndex = target;
        _preferredColumn = -1;
        EnsureCaretVisible();
        return GuideXosTextAreaEditResult.Moved;
    }

    private GuideXosTextAreaEditResult MoveVertical(int direction, bool shift)
    {
        if (!shift && HasSelection)
        {
            _caretIndex = direction < 0 ? SelectionStart : SelectionEnd;
            _anchorIndex = _caretIndex;
            _preferredColumn = -1;
            EnsureCaretVisible();
            return GuideXosTextAreaEditResult.Moved;
        }
        int line = GetLineAndColumn(_caretIndex, out int column);
        if (_preferredColumn < 0) _preferredColumn = column;
        int targetLine = line + direction;
        if (targetLine < 0 || targetLine >= _lineCount)
        {
            return GuideXosTextAreaEditResult.Ignored;
        }
        int targetColumn = Math.Min(_preferredColumn, GetLineLength(targetLine));
        _caretIndex = GetLineStart(targetLine) + targetColumn;
        if (!shift) _anchorIndex = _caretIndex;
        EnsureCaretVisible();
        return GuideXosTextAreaEditResult.Moved;
    }

    private GuideXosTextAreaEditResult MoveToLineEdge(bool end, bool shift)
    {
        if (!shift && HasSelection)
        {
            _caretIndex = end ? SelectionEnd : SelectionStart;
            _anchorIndex = _caretIndex;
            _preferredColumn = -1;
            EnsureCaretVisible();
            return GuideXosTextAreaEditResult.Moved;
        }
        int line = GetLineAndColumn(_caretIndex, out _);
        int target = GetLineStart(line) + (end ? GetLineLength(line) : 0);
        _caretIndex = target;
        if (!shift) _anchorIndex = target;
        _preferredColumn = -1;
        EnsureCaretVisible();
        return GuideXosTextAreaEditResult.Moved;
    }

    private void RemoveRange(int start, int end)
    {
        int removedNewlines = CountNewlines(start, end);
        int count = end - start;
        for (int index = end; index < _length; index++)
        {
            _buffer[index - count] = _buffer[index];
        }
        _length -= count;
        _lineCount -= removedNewlines;
        _anchorIndex = Math.Min(_anchorIndex, _length);
        _caretIndex = Math.Min(_caretIndex, _length);
        _preferredColumn = -1;
        EnsureCaretVisible();
    }

    private GuideXosTextAreaEditResult RejectInput()
    {
        ++_rejectedInputCount;
        return GuideXosTextAreaEditResult.Rejected;
    }

    private void ResetCaretToEnd()
    {
        _caretIndex = _length;
        _anchorIndex = _length;
        _firstVisibleLine = 0;
        _preferredColumn = -1;
        _isSubmitted = false;
        _isCancelled = false;
        EnsureCaretVisible();
    }

    private void EnsureCaretVisible()
    {
        int line = CaretLine;
        if (line < _firstVisibleLine) _firstVisibleLine = line;
        if (line >= _firstVisibleLine + _visibleLineCount)
        {
            _firstVisibleLine = line - _visibleLineCount + 1;
        }
        int maximumFirst = Math.Max(0, _lineCount - _visibleLineCount);
        if (_firstVisibleLine > maximumFirst) _firstVisibleLine = maximumFirst;
        if (_firstVisibleLine < 0) _firstVisibleLine = 0;
    }

    private int GetLineAndColumn(int index, out int column)
    {
        if (index < 0) index = 0;
        if (index > _length) index = _length;
        int line = 0;
        int lastStart = 0;
        for (int cursor = 0; cursor < index; cursor++)
        {
            if (_buffer[cursor] == '\n')
            {
                ++line;
                lastStart = cursor + 1;
            }
        }
        column = index - lastStart;
        return line;
    }

    private int GetLineStart(int line)
    {
        if (line <= 0) return 0;
        int current = 0;
        for (int index = 0; index < _length; index++)
        {
            if (_buffer[index] != '\n') continue;
            ++current;
            if (current == line) return index + 1;
        }
        return _length;
    }

    private int GetLineLength(int line)
    {
        int start = GetLineStart(line);
        int index = start;
        while (index < _length && _buffer[index] != '\n') ++index;
        return index - start;
    }

    private int CountNewlines(int start, int end)
    {
        int count = 0;
        for (int index = start; index < end; index++)
        {
            if (_buffer[index] == '\n') ++count;
        }
        return count;
    }

    private static bool ValidateText(ReadOnlySpan<char> value, out int lineCount)
    {
        lineCount = 1;
        for (int index = 0; index < value.Length; index++)
        {
            char character = value[index];
            if (character == '\n') ++lineCount;
            else if (!IsSupportedCharacter(character)) return false;
        }
        return true;
    }

    private static bool ValidateUtf8(ReadOnlySpan<byte> value, out int lineCount)
    {
        lineCount = 1;
        for (int index = 0; index < value.Length; index++)
        {
            byte character = value[index];
            if (character == (byte)'\n') ++lineCount;
            else if (character < 0x20 || character > 0x7E) return false;
        }
        return true;
    }

    private static bool IsSupportedCharacter(char value)
    {
        return value >= 0x20 && value <= 0x7E;
    }

    private static bool AppendByte(Span<byte> destination, ref int position, byte value)
    {
        if (position >= destination.Length - 1) return false;
        destination[position++] = value;
        return true;
    }
}
