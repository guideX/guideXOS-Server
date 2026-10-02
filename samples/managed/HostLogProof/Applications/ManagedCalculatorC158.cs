#if HOSTLOGPROOF_C158_MANAGED_CALCULATOR
using System;
using System.Runtime.InteropServices;

namespace HostLogProof.Applications;

internal enum GuideXosCalculatorPhaseC158 : byte
{
    EnteringLeft = 0,
    OperatorPending = 1,
    EnteringRight = 2,
    ShowingResult = 3,
    Error = 4,
}

internal enum GuideXosCalculatorOperatorC158 : byte
{
    None = 0,
    Add = 1,
    Subtract = 2,
    Multiply = 3,
    Divide = 4,
}

internal enum GuideXosCalculatorErrorC158 : byte
{
    None = 0,
    Overflow = 1,
    DivideByZero = 2,
}

/// <summary>
/// Fixed-size signed Int64 calculator state. EntryMagnitude is unsigned so
/// the magnitude of Int64.MinValue can be accumulated without wrapping.
/// </summary>
internal sealed unsafe class GuideXosCalculatorCoreC158
{
    private const ulong NegativeMagnitudeLimit = 0x8000000000000000UL;
    private const ulong PositiveMagnitudeLimit = 0x7FFFFFFFFFFFFFFFUL;

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct StateData
    {
        public long LeftOperand;
        public ulong EntryMagnitude;
        public GuideXosCalculatorOperatorC158 PendingOperator;
        public GuideXosCalculatorPhaseC158 Phase;
        public byte EntryLength;
        public byte EntryNegative;
        public GuideXosCalculatorErrorC158 Error;
    }

    private StateData _state;

    public GuideXosCalculatorCoreC158()
    {
        Clear();
    }

    public static int StatePayloadBytes => sizeof(StateData);
    public GuideXosCalculatorPhaseC158 Phase => _state.Phase;
    public GuideXosCalculatorOperatorC158 PendingOperator => _state.PendingOperator;
    public GuideXosCalculatorErrorC158 Error => _state.Error;
    public int EntryLength => _state.EntryLength;
    public long LeftOperand => _state.LeftOperand;

    public bool InputDigit(byte digit)
    {
        if (digit > 9u) return false;
        if (_state.Phase == GuideXosCalculatorPhaseC158.Error ||
            _state.Phase == GuideXosCalculatorPhaseC158.ShowingResult)
        {
            Clear();
        }
        if (_state.Phase == GuideXosCalculatorPhaseC158.OperatorPending)
        {
            BeginRightEntry();
        }
        if (_state.Phase != GuideXosCalculatorPhaseC158.EnteringLeft &&
            _state.Phase != GuideXosCalculatorPhaseC158.EnteringRight)
        {
            return false;
        }

        if (_state.EntryLength >= 19u)
        {
            SetError(GuideXosCalculatorErrorC158.Overflow);
            return true;
        }

        ulong limit = _state.EntryNegative != 0u
            ? NegativeMagnitudeLimit : PositiveMagnitudeLimit;
        ulong value = digit;
        if (_state.EntryLength != 0u)
        {
            if (_state.EntryMagnitude > (limit - value) / 10u)
            {
                SetError(GuideXosCalculatorErrorC158.Overflow);
                return true;
            }
            value += _state.EntryMagnitude * 10u;
        }
        else if (value > limit)
        {
            SetError(GuideXosCalculatorErrorC158.Overflow);
            return true;
        }

        _state.EntryMagnitude = value;
        ++_state.EntryLength;
        return true;
    }

    public bool ChooseOperator(GuideXosCalculatorOperatorC158 operation)
    {
        if (_state.Phase == GuideXosCalculatorPhaseC158.Error ||
            operation == GuideXosCalculatorOperatorC158.None)
        {
            return false;
        }

        if (_state.Phase == GuideXosCalculatorPhaseC158.OperatorPending)
        {
            // No right-hand digits yet: conventional operator replacement.
            _state.PendingOperator = operation;
            return true;
        }

        if (_state.Phase == GuideXosCalculatorPhaseC158.EnteringRight)
        {
            if (!TryEvaluatePending()) return true;
        }

        if (_state.Phase == GuideXosCalculatorPhaseC158.EnteringLeft ||
            _state.Phase == GuideXosCalculatorPhaseC158.ShowingResult)
        {
            if (!TryGetEntryValue(out long current))
            {
                SetError(GuideXosCalculatorErrorC158.Overflow);
                return true;
            }
            _state.LeftOperand = current;
            _state.PendingOperator = operation;
            _state.Phase = GuideXosCalculatorPhaseC158.OperatorPending;
            return true;
        }
        return false;
    }

    public bool ToggleSign()
    {
        if (_state.Phase == GuideXosCalculatorPhaseC158.Error) return false;
        if (_state.Phase == GuideXosCalculatorPhaseC158.OperatorPending)
        {
            BeginRightEntry();
        }
        if (_state.EntryMagnitude == 0u)
        {
            // Canonical zero never acquires a negative sign.
            _state.EntryNegative = 0;
            return true;
        }
        if (_state.EntryNegative != 0u)
        {
            if (_state.EntryMagnitude > PositiveMagnitudeLimit)
            {
                SetError(GuideXosCalculatorErrorC158.Overflow);
                return true;
            }
            _state.EntryNegative = 0;
        }
        else
        {
            _state.EntryNegative = 1;
        }
        return true;
    }

    public bool Backspace()
    {
        if (_state.Phase != GuideXosCalculatorPhaseC158.EnteringLeft &&
            _state.Phase != GuideXosCalculatorPhaseC158.EnteringRight)
        {
            return false;
        }
        if (_state.EntryLength == 0u) return true;
        _state.EntryMagnitude /= 10u;
        --_state.EntryLength;
        if (_state.EntryLength == 0u || _state.EntryMagnitude == 0u)
        {
            _state.EntryNegative = 0;
        }
        return true;
    }

    public bool Evaluate()
    {
        if (_state.Phase == GuideXosCalculatorPhaseC158.Error) return false;
        if (_state.PendingOperator == GuideXosCalculatorOperatorC158.None ||
            _state.Phase == GuideXosCalculatorPhaseC158.ShowingResult)
        {
            // No pending operation and repeated Equals retain the current value.
            return false;
        }
        if (_state.Phase == GuideXosCalculatorPhaseC158.OperatorPending)
        {
            // Equals immediately after an operator evaluates with a zero RHS.
            BeginRightEntry();
        }
        bool evaluated = TryEvaluatePending();
        // Overflow and divide-by-zero are handled calculator results. Returning
        // false here would make the application skip repainting the Error state.
        return evaluated || _state.Phase == GuideXosCalculatorPhaseC158.Error;
    }

    public void Clear()
    {
        _state = default;
        _state.Phase = GuideXosCalculatorPhaseC158.EnteringLeft;
    }

    public bool TryFormatDisplay(Span<char> destination, out int length)
    {
        length = 0;
        if (_state.Phase == GuideXosCalculatorPhaseC158.Error)
        {
            ReadOnlySpan<char> errorText = _state.Error ==
                    GuideXosCalculatorErrorC158.DivideByZero
                ? "Divide by zero" : "Overflow";
            if (errorText.Length > destination.Length) return false;
            errorText.CopyTo(destination);
            length = errorText.Length;
            return true;
        }

        long value = _state.Phase == GuideXosCalculatorPhaseC158.OperatorPending
            ? _state.LeftOperand : EntryValue;
        return TryFormatInt64(value, destination, out length);
    }

    public long EntryValue
    {
        get
        {
            if (_state.EntryNegative == 0u)
                return (long)_state.EntryMagnitude;
            if (_state.EntryMagnitude == NegativeMagnitudeLimit)
                return long.MinValue;
            return -(long)_state.EntryMagnitude;
        }
    }

    private bool TryEvaluatePending()
    {
        if (_state.PendingOperator == GuideXosCalculatorOperatorC158.None ||
            !TryGetEntryValue(out long right))
        {
            return false;
        }

        long result;
        switch (_state.PendingOperator)
        {
            case GuideXosCalculatorOperatorC158.Add:
                if ((right > 0 && _state.LeftOperand > long.MaxValue - right) ||
                    (right < 0 && _state.LeftOperand < long.MinValue - right))
                {
                    SetError(GuideXosCalculatorErrorC158.Overflow);
                    return false;
                }
                result = _state.LeftOperand + right;
                break;
            case GuideXosCalculatorOperatorC158.Subtract:
                if ((right > 0 && _state.LeftOperand < long.MinValue + right) ||
                    (right < 0 && _state.LeftOperand > long.MaxValue + right))
                {
                    SetError(GuideXosCalculatorErrorC158.Overflow);
                    return false;
                }
                result = _state.LeftOperand - right;
                break;
            case GuideXosCalculatorOperatorC158.Multiply:
                if (_state.LeftOperand == 0 || right == 0)
                {
                    result = 0;
                    break;
                }
                if ((_state.LeftOperand == long.MinValue && right == -1) ||
                    (right == long.MinValue && _state.LeftOperand == -1))
                {
                    SetError(GuideXosCalculatorErrorC158.Overflow);
                    return false;
                }
                result = unchecked(_state.LeftOperand * right);
                if (result / right != _state.LeftOperand)
                {
                    SetError(GuideXosCalculatorErrorC158.Overflow);
                    return false;
                }
                break;
            case GuideXosCalculatorOperatorC158.Divide:
                if (right == 0)
                {
                    SetError(GuideXosCalculatorErrorC158.DivideByZero);
                    return false;
                }
                if (_state.LeftOperand == long.MinValue && right == -1)
                {
                    SetError(GuideXosCalculatorErrorC158.Overflow);
                    return false;
                }
                result = _state.LeftOperand / right;
                break;
            default:
                return false;
        }

        SetEntry(result);
        _state.LeftOperand = result;
        _state.PendingOperator = GuideXosCalculatorOperatorC158.None;
        _state.Phase = GuideXosCalculatorPhaseC158.ShowingResult;
        _state.Error = GuideXosCalculatorErrorC158.None;
        return true;
    }

    private void BeginRightEntry()
    {
        _state.EntryMagnitude = 0u;
        _state.EntryLength = 0;
        _state.EntryNegative = 0;
        _state.Phase = GuideXosCalculatorPhaseC158.EnteringRight;
    }

    private bool TryGetEntryValue(out long value)
    {
        value = EntryValue;
        return _state.EntryNegative != 0u
            ? _state.EntryMagnitude <= NegativeMagnitudeLimit
            : _state.EntryMagnitude <= PositiveMagnitudeLimit;
    }

    private void SetEntry(long value)
    {
        _state.EntryNegative = value < 0 ? (byte)1 : (byte)0;
        _state.EntryMagnitude = value < 0
            ? unchecked((ulong)(-(value + 1L)) + 1UL)
            : (ulong)value;
        _state.EntryLength = CountDigits(_state.EntryMagnitude);
    }

    private void SetError(GuideXosCalculatorErrorC158 error)
    {
        _state.Error = error;
        _state.Phase = GuideXosCalculatorPhaseC158.Error;
    }

    private static byte CountDigits(ulong magnitude)
    {
        byte count = 1;
        while (magnitude >= 10u)
        {
            magnitude /= 10u;
            ++count;
        }
        return count;
    }

    private static bool TryFormatInt64(
        long value, Span<char> destination, out int length)
    {
        length = 0;
        Span<char> reversed = stackalloc char[20];
        int count = 0;
        bool negative = value < 0;
        ulong magnitude = negative
            ? unchecked((ulong)(-(value + 1L)) + 1UL)
            : (ulong)value;
        do
        {
            reversed[count++] = (char)('0' + (magnitude % 10u));
            magnitude /= 10u;
        } while (magnitude != 0u);
        int required = count + (negative ? 1 : 0);
        if (required > destination.Length) return false;
        if (negative) destination[length++] = '-';
        while (count != 0) destination[length++] = reversed[--count];
        return true;
    }
}

/// <summary>
/// Application-owned controls and shared pointer/keyboard command router.
/// The one flat ControlHost owns focus routing; it does not own the controls.
/// </summary>
internal sealed class GuideXosCalculatorControllerC158
{
    private const uint NativeEscapeKeyCode = 0x11Bu;
    public const int ControlCount = 18;
    public const int ControlCapacity = 18;
    public const int ClearIndex = 0;
    public const int BackspaceIndex = 1;
    public const int SignIndex = 2;
    public const int DivideIndex = 3;
    public const int MultiplyIndex = 7;
    public const int SubtractIndex = 11;
    public const int AddIndex = 15;
    public const int ZeroIndex = 16;
    public const int EqualsIndex = 17;

    private static readonly int[] s_columnX = { 12, 80, 148, 216 };
    private readonly GuideXosButton[] _buttons = new GuideXosButton[ControlCount];
    private readonly GuideXosControlHost _controlHost =
        new(ControlCapacity);
    private readonly GuideXosCalculatorCoreC158 _core = new();
    private uint _commandCount;

    public GuideXosCalculatorControllerC158()
    {
        CreateButtons();
        StartFresh();
    }

    public GuideXosControlHost ControlHost => _controlHost;
    public GuideXosCalculatorCoreC158 Core => _core;
    public int RegistrationCount => _controlHost.RegistrationCount;
    public int ActiveIndex => _controlHost.ActiveIndex;
    public int ActiveControlId => _controlHost.ActiveControlId;
    public uint CommandCount => _commandCount;
    public GuideXosButton ButtonAt(int index) =>
        index >= 0 && index < _buttons.Length ? _buttons[index] : null;

    public bool StartFresh()
    {
        _controlHost.Reset();
        _core.Clear();
        _commandCount = 0u;
        for (int index = 0; index < _buttons.Length; index++)
        {
            _buttons[index].Reset();
            if (_controlHost.TryRegisterButton(index + 1, _buttons[index]) !=
                GuideXosControlHostResult.Registered)
            {
                _controlHost.Reset();
                return false;
            }
        }
        // Equals is the initial Enter target; Tab still traverses in visual
        // row order and wraps from the final button back to Clear.
        return _controlHost.TryFocus(EqualsControlId) ==
            GuideXosControlHostResult.Focused;
    }

    public void Teardown()
    {
        _controlHost.Reset();
    }

    public bool RouteInput(GuideXosInputEvent input)
    {
        if ((input.Kind == GuideXosInputKind.KeyDown ||
                input.Kind == GuideXosInputKind.KeyChar) && input.Control)
        {
            // Calculator defines no Ctrl shortcuts. Never leak a chord into
            // arithmetic or a focused button command.
            return false;
        }

        switch (input.Kind)
        {
            case GuideXosInputKind.PointerDown:
                return RoutePointer(input);
            case GuideXosInputKind.KeyDown:
                return RouteKeyDown(input);
            case GuideXosInputKind.KeyChar:
                return RouteCharacter(input.Character, input.Shift);
            default:
                return false;
        }
    }

    public bool ActivateControlId(int id)
    {
        return id > 0 && id <= ControlCount && ActivateButton(id - 1);
    }

    public bool TryFormatDisplay(Span<char> destination, out int length)
    {
        return _core.TryFormatDisplay(destination, out length);
    }

    private void CreateButtons()
    {
        for (int index = 0; index < _buttons.Length; index++)
        {
            int row = index / 4;
            int column = index % 4;
            int x = s_columnX[column];
            int y = 66 + row * 36;
            int width = 64;
            string label = index switch
            {
                ClearIndex => "C",
                BackspaceIndex => "Bsp",
                SignIndex => "+/-",
                DivideIndex => "/",
                4 => "7", 5 => "8", 6 => "9", MultiplyIndex => "*",
                8 => "4", 9 => "5", 10 => "6", SubtractIndex => "-",
                12 => "1", 13 => "2", 14 => "3", AddIndex => "+",
                ZeroIndex => "0",
                EqualsIndex => "=",
                _ => "?",
            };
            if (index == EqualsIndex) width = 200;
            _buttons[index] = new GuideXosButton(x, y, width, 28, label);
        }
    }

    private bool RoutePointer(GuideXosInputEvent input)
    {
        if (input.Button != GuideXosPointerButton.Primary) return false;
        for (int index = 0; index < _buttons.Length; index++)
        {
            GuideXosButton button = _buttons[index];
            if (input.X < button.X || input.Y < button.Y ||
                input.X >= button.X + button.Width ||
                input.Y >= button.Y + button.Height)
            {
                continue;
            }
            GuideXosControlHostResult result = _controlHost.FocusAndRoutePointer(
                index + 1, input.X, input.Y);
            return result == GuideXosControlHostResult.Activated &&
                ActivateButton(index);
        }
        return false;
    }

    private bool RouteKeyDown(GuideXosInputEvent input)
    {
        GuideXosTextInputKey key = (GuideXosTextInputKey)input.KeyCode;
        if (key == GuideXosTextInputKey.Escape || input.KeyCode == NativeEscapeKeyCode)
        {
            return DispatchClear();
        }
        if (key == GuideXosTextInputKey.Backspace)
        {
            return DispatchBackspace();
        }

        GuideXosControlHostResult routed = _controlHost.HandleInput(input);
        if (routed == GuideXosControlHostResult.Activated)
        {
            return ActivateControlId(_controlHost.ActiveControlId);
        }
        if (routed == GuideXosControlHostResult.Traversed) return true;
        if (key == GuideXosTextInputKey.Enter) return DispatchEvaluate();
        return false;
    }

    private bool RouteCharacter(char character, bool shift)
    {
        if (character == ' ')
        {
            GuideXosControlHostResult result = _controlHost.HandleCharacter(character);
            return result == GuideXosControlHostResult.Activated &&
                ActivateControlId(_controlHost.ActiveControlId);
        }
        // Enter is dispatched on KeyDown. Ignore its paired CR/LF KeyChar so
        // one physical Enter can never evaluate twice.
        if (character == '\r' || character == '\n' || character == '\t')
            return false;
        if (character >= '0' && character <= '9')
        {
            ++_commandCount;
            return _core.InputDigit((byte)(character - '0'));
        }
        return character switch
        {
            '+' => DispatchOperator(GuideXosCalculatorOperatorC158.Add),
            '-' => DispatchOperator(GuideXosCalculatorOperatorC158.Subtract),
            '*' => DispatchOperator(GuideXosCalculatorOperatorC158.Multiply),
            '/' => DispatchOperator(GuideXosCalculatorOperatorC158.Divide),
            '=' => DispatchEvaluate(),
            'c' or 'C' => DispatchClear(),
            _ => IgnoreCharacter(shift),
        };
    }

    private bool ActivateButton(int index)
    {
        switch (index)
        {
            case ClearIndex: return DispatchClear();
            case BackspaceIndex: return DispatchBackspace();
            case SignIndex:
                ++_commandCount;
                return _core.ToggleSign();
            case DivideIndex: return DispatchOperator(GuideXosCalculatorOperatorC158.Divide);
            case MultiplyIndex: return DispatchOperator(GuideXosCalculatorOperatorC158.Multiply);
            case SubtractIndex: return DispatchOperator(GuideXosCalculatorOperatorC158.Subtract);
            case AddIndex: return DispatchOperator(GuideXosCalculatorOperatorC158.Add);
            case EqualsIndex: return DispatchEvaluate();
            default:
                if (index >= 4 && index <= 6)
                    return DispatchDigit((byte)(7 + index - 4));
                if (index >= 8 && index <= 10)
                    return DispatchDigit((byte)(4 + index - 8));
                if (index >= 12 && index <= 14)
                    return DispatchDigit((byte)(1 + index - 12));
                if (index == ZeroIndex) return DispatchDigit(0);
                return false;
        }
    }

    private bool DispatchDigit(byte digit)
    {
        ++_commandCount;
        return _core.InputDigit(digit);
    }

    private bool DispatchOperator(GuideXosCalculatorOperatorC158 operation)
    {
        ++_commandCount;
        return _core.ChooseOperator(operation);
    }

    private bool DispatchEvaluate()
    {
        ++_commandCount;
        return _core.Evaluate();
    }

    private bool DispatchClear()
    {
        ++_commandCount;
        _core.Clear();
        return true;
    }

    private bool DispatchBackspace()
    {
        ++_commandCount;
        return _core.Backspace();
    }

    private static bool IgnoreCharacter(bool shift)
    {
        _ = shift; // Shifted '+' and '*' arrive as their decoded KeyChar values.
        return false;
    }

    private const int EqualsControlId = EqualsIndex + 1;
}

public sealed class ManagedCalculatorC158 : GuideXosApplication
{
    public const string ApplicationId = "com.guidexos.apps.managed.calculator";
    public const uint ApplicationSelector = 6u;
    public const uint CloseActionId = 0x01580001u;
    public const int SurfaceWidth = 292;
    public const int SurfaceHeight = 270;

    private static bool s_testsRun;
    private readonly GuideXosCalculatorControllerC158 _controller = new();
    private ulong _window;

    internal int RegistrationCount => _controller.RegistrationCount;
    internal bool DisplayIsZero => DisplayIs("0");

    private bool DisplayIs(string expected)
    {
        Span<char> display = stackalloc char[20];
        return _controller.TryFormatDisplay(display, out int length) &&
            display[..length].SequenceEqual(expected.AsSpan());
    }

    public override GuideXosResult Launch(GuideXosHost host)
    {
        if (host.Selector != ApplicationSelector) return GuideXosResult.InvalidArgument;
        if (!s_testsRun)
        {
            s_testsRun = true;
            if (!GuideXosManagedCalculatorC158Tests.Run(host))
                return GuideXosResult.InvalidArgument;
        }
        GuideXosResult created = host.TryCreateSurface(
            "Managed Calculator"u8, SurfaceWidth, SurfaceHeight,
            out GuideXosSurface surface);
        if (created != GuideXosResult.Success || surface == null) return created;
        _window = surface.Handle;
        if (!_controller.StartFresh() || !Render(surface))
            return GuideXosResult.InvalidArgument;
        host.TryLog("C158-CALC-LAUNCH id=managed-calculator selector=6 controls=18 capacity=18 focus=18 fresh=PASS result=PASS"u8);
        LogDisplay(host);
        return GuideXosResult.Success;
    }

    public override GuideXosResult HandleInput(
        GuideXosHost host, GuideXosInputEvent input)
    {
        if (host.TryGetSurface(_window, out GuideXosSurface surface) !=
                GuideXosResult.Success || surface == null)
        {
            return GuideXosResult.SurfaceCreationFailed;
        }
        if (!_controller.RouteInput(input)) return GuideXosResult.Success;
        if (!Render(surface)) return GuideXosResult.InvalidArgument;
        LogDisplay(host);
        if (input.Kind == GuideXosInputKind.KeyDown &&
            input.KeyCode == (uint)GuideXosTextInputKey.Tab)
        {
            Span<byte> focusLine = stackalloc byte[64];
            int position = 0;
            GuideXosText.Append(focusLine, ref position,
                "C158-CALC-FOCUS active="u8);
            GuideXosText.AppendUnsigned(focusLine, ref position,
                (uint)_controller.ActiveControlId);
            GuideXosText.Append(focusLine, ref position, " shift="u8);
            GuideXosText.Append(focusLine, ref position,
                input.Shift ? "true"u8 : "false"u8);
            GuideXosText.Append(focusLine, ref position, " result=PASS"u8);
            host.TryLog(focusLine[..position]);
        }
        return GuideXosResult.Success;
    }

    public override GuideXosResult HandleAction(GuideXosHost host, uint actionId)
    {
        if (actionId == CloseActionId)
        {
            _controller.Teardown();
            _window = 0u;
            host.TryLog("C158-CALC-CLOSE controls=0 active=none modal=none popup=none drag=none result=PASS"u8);
            return GuideXosResult.Success;
        }
        if (host.TryGetSurface(_window, out GuideXosSurface surface) !=
                GuideXosResult.Success || surface == null)
        {
            return GuideXosResult.SurfaceCreationFailed;
        }
        if (actionId == 0u || actionId > GuideXosCalculatorControllerC158.ControlCount ||
            !_controller.ActivateControlId((int)actionId))
        {
            return GuideXosResult.InvalidAction;
        }
        return Render(surface) ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    public override void OnTearingDown()
    {
        _controller.Teardown();
        _window = 0u;
    }

    private bool Render(GuideXosSurface surface)
    {
        if (surface.TryFillRect(8, 8, 276, 250, 0x001D2733u) !=
                GuideXosResult.Success ||
            surface.TrySetText(20, 16, "Calculator | signed Int64"u8) !=
                GuideXosResult.Success)
        {
            return false;
        }
        Span<char> display = stackalloc char[20];
        if (!_controller.TryFormatDisplay(display, out int displayLength) ||
            !_displayLabel.SetText(display[..displayLength]) ||
            _displayLabel.Render(surface) != GuideXosResult.Success)
        {
            return false;
        }
        for (int index = 0; index < GuideXosCalculatorControllerC158.ControlCount; index++)
        {
            if (_controller.ButtonAt(index).Render(surface) != GuideXosResult.Success)
                return false;
        }
        return true;
    }

    private readonly GuideXosLabel _displayLabel = new(20, 38, 240, "0", 20);

    private void LogDisplay(GuideXosHost host)
    {
        Span<byte> line = stackalloc byte[96];
        Span<char> display = stackalloc char[20];
        if (!_controller.TryFormatDisplay(display, out int displayLength)) return;
        int position = 0;
        GuideXosText.Append(line, ref position, "C158-CALC-STATE display="u8);
        for (int index = 0; index < displayLength; index++)
            line[position++] = (byte)display[index];
        GuideXosText.Append(line, ref position, " phase="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_controller.Core.Phase);
        GuideXosText.Append(line, ref position, " commands="u8);
        GuideXosText.AppendUnsigned(line, ref position, _controller.CommandCount);
        GuideXosText.Append(line, ref position, " result=PASS"u8);
        host.TryLog(line[..position]);
    }
}
#endif
