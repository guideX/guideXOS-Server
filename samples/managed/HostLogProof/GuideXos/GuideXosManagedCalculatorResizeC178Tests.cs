#if HOSTLOGPROOF_C178_MANAGED_CALCULATOR_RESIZE
using System;
using HostLogProof.Applications;

namespace HostLogProof;

public static class GuideXosManagedCalculatorResizeC178Tests
{
    public const int CaseCount = 18;
    public const int StressCaseCount = 5;
    public const int RequiredLayoutUpdates = 1000;
    public const int RequiredAlternatingResizes = 500;
    public const int RequiredArithmeticResizeCycles = 250;
    public const int RequiredLifecycleCycles = 100;

    public static int LayoutUpdates { get; private set; }
    public static int AlternatingResizes { get; private set; }
    public static int ArithmeticResizeCycles { get; private set; }
    public static int LifecycleCycles { get; private set; }
    public static long ResizeAllocationBytes { get; private set; }
    public static int GeometryDriftCount { get; private set; }
    public static string FirstFailedCase { get; private set; } = string.Empty;

    public static bool Run(GuideXosHost host)
    {
        FirstFailedCase = string.Empty;
        int cases = 0;
        bool passed = true;
        ManagedCalculatorC158 calculator = new();
        int minWidth = ManagedCalculatorC158.MinimumWindowWidthC178;
        int minHeight = ManagedCalculatorC158.MinimumWindowHeightC178;
        int maxWidth = ManagedCalculatorC158.MaximumWindowWidthC178;
        int maxHeight = ManagedCalculatorC158.MaximumWindowHeightC178;

        passed &= Case(ref cases, "initial-geometry",
            calculator.WindowWidth == ManagedCalculatorC158.SurfaceWidth &&
            calculator.WindowHeight == ManagedCalculatorC158.SurfaceHeight &&
            calculator.Controller.RegistrationCount == 18 &&
            calculator.Controller.ActiveControlId == 18);
        passed &= Case(ref cases, "minimum-geometry",
            calculator.TryApplyResizeForTests(minWidth, minHeight) &&
            calculator.WindowWidth == minWidth && calculator.WindowHeight == minHeight &&
            calculator.Controller.ControlsFit(minWidth, minHeight - 24) &&
            DisplayFits(calculator, minWidth, minHeight));
        int[] minimumButtonX = CaptureButtonX(calculator.Controller);
        int[] minimumButtonY = CaptureButtonY(calculator.Controller);
        passed &= Case(ref cases, "minimum-one-pixel-rejection",
            !calculator.TryApplyResizeForTests(minWidth - 1, minHeight) &&
            calculator.WindowWidth == minWidth && calculator.WindowHeight == minHeight &&
            ButtonPositionsEqual(calculator.Controller, minimumButtonX, minimumButtonY));
        passed &= Case(ref cases, "minimum-height-one-pixel-rejection",
            !calculator.TryApplyResizeForTests(minWidth, minHeight - 1) &&
            calculator.WindowWidth == minWidth && calculator.WindowHeight == minHeight &&
            ButtonPositionsEqual(calculator.Controller, minimumButtonX, minimumButtonY));
        passed &= Case(ref cases, "maximum-geometry",
            calculator.TryApplyResizeForTests(maxWidth, maxHeight) &&
            calculator.WindowWidth == maxWidth && calculator.WindowHeight == maxHeight &&
            calculator.Controller.ControlsFit(maxWidth, maxHeight - 24) &&
            DisplayFits(calculator, maxWidth, maxHeight));
        passed &= Case(ref cases, "maximum-one-pixel-rejection",
            !calculator.TryApplyResizeForTests(maxWidth + 1, maxHeight) &&
            !calculator.TryApplyResizeForTests(maxWidth, maxHeight + 1) &&
            calculator.WindowWidth == maxWidth && calculator.WindowHeight == maxHeight);
        passed &= Case(ref cases, "focus-and-control-count-retained",
            calculator.Controller.ActiveControlId == 18 &&
            calculator.Controller.RegistrationCount == 18 &&
            !calculator.Controller.ControlHost.IsModalActive &&
            !calculator.Controller.ControlHost.HasTransientInputCapture &&
            !calculator.Controller.ControlHost.HasPointerDragCapture);

        calculator.Controller.StartFresh();
        Keyboard(calculator.Controller, "7");
        Keyboard(calculator.Controller, "*");
        Keyboard(calculator.Controller, "8");
        GuideXosCalculatorStateC178 pendingMultiply =
            calculator.Controller.CalculatorState;
        bool pendingPreserved = calculator.TryApplyResizeForTests(minWidth, minHeight) &&
            pendingMultiply.Equals(calculator.Controller.CalculatorState) &&
            calculator.TryApplyResizeForTests(maxWidth, maxHeight) &&
            pendingMultiply.Equals(calculator.Controller.CalculatorState);
        passed &= Case(ref cases, "pending-operands-and-operator-preserved",
            pendingPreserved &&
            calculator.Controller.CalculatorState.PendingOperator ==
                GuideXosCalculatorOperatorC158.Multiply &&
            calculator.Controller.CalculatorState.LeftOperand == 7 &&
            calculator.Controller.CalculatorState.EntryMagnitude == 8u);
        bool completed = calculator.Controller.RouteInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Enter));
        passed &= Case(ref cases, "multiply-after-resize",
            completed && DisplayIs(calculator, "56"));

        calculator.Controller.StartFresh();
        Keyboard(calculator.Controller, "2");
        Keyboard(calculator.Controller, "+");
        GuideXosCalculatorStateC178 operatorPending =
            calculator.Controller.CalculatorState;
        bool operatorPendingPreserved = calculator.TryApplyResizeForTests(
                minWidth, minHeight) &&
            operatorPending.Equals(calculator.Controller.CalculatorState) &&
            calculator.Controller.CalculatorState.Phase ==
                GuideXosCalculatorPhaseC158.OperatorPending &&
            calculator.Controller.CalculatorState.PendingOperator ==
                GuideXosCalculatorOperatorC158.Add;
        passed &= Case(ref cases, "operator-pending-empty-entry-preserved",
            operatorPendingPreserved);

        calculator.Controller.StartFresh();
        Keyboard(calculator.Controller, "9");
        Keyboard(calculator.Controller, "+");
        Keyboard(calculator.Controller, "4");
        GuideXosCalculatorStateC178 pendingAddition =
            calculator.Controller.CalculatorState;
        bool addPreserved = calculator.TryApplyResizeForTests(minWidth, minHeight) &&
            pendingAddition.Equals(calculator.Controller.CalculatorState);
        bool addCompleted = calculator.Controller.RouteInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Enter));
        passed &= Case(ref cases, "addition-after-resize",
            addPreserved && addCompleted && DisplayIs(calculator, "13"));

        calculator.Controller.StartFresh();
        Keyboard(calculator.Controller, "8");
        Keyboard(calculator.Controller, "-");
        Keyboard(calculator.Controller, "3");
        calculator.TryApplyResizeForTests(maxWidth, maxHeight);
        bool subtractCompleted = calculator.Controller.RouteInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Enter));
        passed &= Case(ref cases, "subtraction-after-resize",
            subtractCompleted && DisplayIs(calculator, "5"));

        calculator.Controller.StartFresh();
        Keyboard(calculator.Controller, "9");
        Keyboard(calculator.Controller, "/");
        Keyboard(calculator.Controller, "2");
        calculator.TryApplyResizeForTests(minWidth, minHeight);
        bool divideCompleted = calculator.Controller.RouteInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Enter));
        passed &= Case(ref cases, "division-after-resize",
            divideCompleted && DisplayIs(calculator, "4"));

        calculator.Controller.StartFresh();
        Keyboard(calculator.Controller, "7");
        Keyboard(calculator.Controller, "/");
        Keyboard(calculator.Controller, "0");
        calculator.Controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter));
        GuideXosCalculatorStateC178 divideByZero =
            calculator.Controller.CalculatorState;
        bool errorResized = calculator.TryApplyResizeForTests(maxWidth, maxHeight);
        bool errorStateRetained = divideByZero.Equals(
            calculator.Controller.CalculatorState);
        bool errorDisplayRetained = DisplayIs(calculator, "Divide by zero");
        bool clearHandled = calculator.Controller.RouteInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape));
        bool divideClearResult = DisplayIs(calculator, "0") &&
            calculator.Controller.CalculatorState.PendingOperator ==
                GuideXosCalculatorOperatorC158.None;
        calculator.Controller.StartFresh();
        Keyboard(calculator.Controller, "9223372036854775807");
        Keyboard(calculator.Controller, "+");
        Keyboard(calculator.Controller, "1");
        calculator.Controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter));
        GuideXosCalculatorStateC178 overflow =
            calculator.Controller.CalculatorState;
        bool overflowResized = calculator.TryApplyResizeForTests(
            minWidth, minHeight);
        bool overflowStateRetained = overflow.Equals(
            calculator.Controller.CalculatorState);
        bool overflowDisplayRetained = DisplayIs(calculator, "Overflow");
        bool overflowClearHandled = calculator.Controller.RouteInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape));
        bool overflowClearResult = DisplayIs(calculator, "0") &&
            calculator.Controller.CalculatorState.Error ==
                GuideXosCalculatorErrorC158.None;
        passed &= Case(ref cases,
            $"error-and-clear-preserved-divide={errorResized}-{errorStateRetained}-{errorDisplayRetained}-{clearHandled}-{divideClearResult}-overflow={overflowResized}-{overflowStateRetained}-{overflowDisplayRetained}-{overflowClearHandled}-{overflowClearResult}",
            errorResized && errorStateRetained && errorDisplayRetained &&
            clearHandled && divideClearResult && overflowResized &&
            overflowStateRetained && overflowDisplayRetained &&
            overflowClearHandled && overflowClearResult);

        calculator.Controller.StartFresh();
        Keyboard(calculator.Controller, "2");
        Keyboard(calculator.Controller, "+");
        Keyboard(calculator.Controller, "3");
        calculator.Controller.RouteInput(GuideXosInputEvent.ForKeyChar('='));
        GuideXosCalculatorStateC178 result = calculator.Controller.CalculatorState;
        bool repeatUnchanged = calculator.TryApplyResizeForTests(minWidth, minHeight) &&
            result.Equals(calculator.Controller.CalculatorState) &&
            !calculator.Controller.RouteInput(GuideXosInputEvent.ForKeyChar('=')) &&
            result.Equals(calculator.Controller.CalculatorState);
        passed &= Case(ref cases, "repeated-equals-retained",
            repeatUnchanged && DisplayIs(calculator, "5"));

        calculator.Controller.StartFresh();
        bool hitTests = true;
        for (int index = 0; index < 18; index++)
        {
            GuideXosButton button = calculator.Controller.ButtonAt(index);
            hitTests &= button != null && button.X >= 0 && button.Y >= 0 &&
                button.X + button.Width <= minWidth &&
                button.Y + button.Height <= minHeight - 24 &&
                calculator.Controller.RouteInput(PointerAt(button)) &&
                calculator.Controller.ActiveControlId == index + 1;
        }
        passed &= Case(ref cases, "all-buttons-hit-testable-at-minimum", hitTests);

        bool actionEncoding = GuideXosManagedWindowGeometryC172.TryEncodeAction(
            minWidth, minHeight, out uint resizeAction) &&
            GuideXosManagedWindowGeometryC172.TryDecodeAction(resizeAction,
                out int decodedWidth, out int decodedHeight) &&
            decodedWidth == minWidth && decodedHeight == minHeight &&
            !GuideXosManagedWindowGeometryC172.TryDecodeAction(
                ManagedCalculatorC158.CloseActionId, out _, out _);
        passed &= Case(ref cases, "existing-action-channel-retained", actionEncoding);

        GuideXosManagedWindowResizeTransactionC174 rollback = new(
            in ManagedCalculatorC158.ResizeBoundsC178, minWidth, minHeight,
            maxWidth, maxHeight);
        GuideXosManagedWindowResizeCandidateC174 candidate = rollback.Candidate;
        bool applied = calculator.Controller.TryRecomputeLayout(in candidate);
        bool rejected = !rollback.Complete(layoutAccepted: false,
            renderResult: GuideXosResult.Success, stateAccepted: false);
        bool restored = rollback.RequiresRollback &&
            calculator.Controller.TryRecomputeLayout(rollback.PriorOuterWidth,
                rollback.PriorOuterHeight) &&
            calculator.WindowWidth == minWidth && calculator.WindowHeight == minHeight &&
            calculator.Controller.ControlsFit(minWidth, minHeight - 24);
        passed &= Case(ref cases, "transaction-rejection-rolls-layout-back",
            applied && rejected && restored);

        if (cases != CaseCount && string.IsNullOrEmpty(FirstFailedCase))
            FirstFailedCase = $"case-count-{cases}";
        if (cases != CaseCount) passed = false;
        host?.TryLog(passed
            ? "C178-CALCULATOR-RESIZE focused=18 state=retained rollback=PASS hit-test=PASS result=PASS"u8
            : "C178-CALCULATOR-RESIZE result=FAIL"u8);
        return passed;
    }

    public static bool RunStress(GuideXosHost host)
    {
        LayoutUpdates = AlternatingResizes = ArithmeticResizeCycles =
            LifecycleCycles = GeometryDriftCount = 0;
        ResizeAllocationBytes = 0;
        ManagedCalculatorC158 calculator = new();
        int defaultWidth = ManagedCalculatorC158.SurfaceWidth;
        int defaultHeight = ManagedCalculatorC158.SurfaceHeight;
        int minWidth = ManagedCalculatorC158.MinimumWindowWidthC178;
        int minHeight = ManagedCalculatorC158.MinimumWindowHeightC178;
        int maxWidth = ManagedCalculatorC158.MaximumWindowWidthC178;
        int maxHeight = ManagedCalculatorC158.MaximumWindowHeightC178;
        if (!calculator.TryApplyResizeForTests(defaultWidth, defaultHeight))
        {
            FirstFailedCase = "stress-initial-layout";
            return false;
        }

        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < RequiredLayoutUpdates; index++)
        {
            int width = (index % 3) switch
            {
                0 => minWidth,
                1 => defaultWidth,
                _ => maxWidth,
            };
            int height = (index % 3) switch
            {
                0 => minHeight,
                1 => defaultHeight,
                _ => maxHeight,
            };
            if (!calculator.TryApplyResizeForTests(width, height))
            {
                FirstFailedCase = $"stress-layout-{index}";
                return false;
            }
            ++LayoutUpdates;
        }
        ResizeAllocationBytes = GC.GetAllocatedBytesForCurrentThread() - before;

        if (!calculator.TryApplyResizeForTests(minWidth, minHeight))
        {
            FirstFailedCase = "stress-alternating-initial-layout";
            return false;
        }
        for (int index = 0; index < RequiredAlternatingResizes; index++)
        {
            bool maximum = (index & 1) == 0;
            if (!calculator.TryApplyResizeForTests(
                    maximum ? maxWidth : minWidth,
                    maximum ? maxHeight : minHeight))
            {
                FirstFailedCase = $"stress-alternating-{index}";
                return false;
            }
            ++AlternatingResizes;
        }
        if (calculator.WindowWidth != minWidth ||
            calculator.WindowHeight != minHeight) ++GeometryDriftCount;
        for (int index = 0; index < 18; index++)
        {
            GuideXosButton button = calculator.Controller.ButtonAt(index);
            int expectedX = (minWidth - defaultWidth) / 2 +
                ColumnX[index % 4];
            int expectedY = (index / 4) * 36 + 66 +
                ((minHeight - 24) - (defaultHeight - 24)) / 2;
            if (button.X != expectedX || button.Y != expectedY)
                ++GeometryDriftCount;
        }

        for (int index = 0; index < RequiredArithmeticResizeCycles; index++)
        {
            calculator.Controller.RouteInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Escape));
            Keyboard(calculator.Controller, "7");
            Keyboard(calculator.Controller, "*");
            Keyboard(calculator.Controller, "8");
            GuideXosCalculatorStateC178 state =
                calculator.Controller.CalculatorState;
            bool resize = calculator.TryApplyResizeForTests(
                (index & 1) == 0 ? maxWidth : minWidth,
                (index & 1) == 0 ? maxHeight : minHeight);
            bool preserved = state.Equals(calculator.Controller.CalculatorState);
            bool evaluate = calculator.Controller.RouteInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Enter));
            if (!resize || !preserved || !evaluate || !DisplayIs(calculator, "56"))
            {
                FirstFailedCase = $"stress-arithmetic-{index}";
                return false;
            }
            ++ArithmeticResizeCycles;
        }

        GuideXosApplicationDescriptor descriptor = new(6u,
            "Managed Calculator"u8, static () => new ManagedCalculatorC158());
        for (int index = 0; index < RequiredLifecycleCycles; index++)
        {
            GuideXosManagedApplicationLifetime lifetime = new();
            if (!lifetime.TryStart(descriptor,
                    out GuideXosApplication active, out _) ||
                active is not ManagedCalculatorC158 instance ||
                instance.Controller.RegistrationCount != 18 ||
                !instance.TryApplyResizeForTests(minWidth, minHeight) ||
                !instance.TryApplyResizeForTests(maxWidth, maxHeight))
            {
                FirstFailedCase = $"stress-lifecycle-start-{index}";
                return false;
            }
            lifetime.Clear(6u);
            if (instance.Controller.RegistrationCount != 0 ||
                instance.Controller.ActiveIndex != -1 ||
                instance.Controller.ControlHost.IsModalActive ||
                instance.Controller.ControlHost.HasTransientInputCapture ||
                instance.Controller.ControlHost.HasPointerDragCapture)
            {
                FirstFailedCase = $"stress-lifecycle-teardown-{index}";
                return false;
            }
            ++LifecycleCycles;
        }

        bool passed = LayoutUpdates == RequiredLayoutUpdates &&
            AlternatingResizes == RequiredAlternatingResizes &&
            ArithmeticResizeCycles == RequiredArithmeticResizeCycles &&
            LifecycleCycles == RequiredLifecycleCycles &&
            ResizeAllocationBytes == 0 && GeometryDriftCount == 0;
        FirstFailedCase = passed ? string.Empty : "stress-final-invariant";
        host?.TryLog(passed
            ? "C178-CALCULATOR-RESIZE-STRESS layout=1000 alternating=500 arithmetic=250 lifecycles=100 allocationBytes=0 drift=0 result=PASS"u8
            : "C178-CALCULATOR-RESIZE-STRESS result=FAIL"u8);
        return passed;
    }

    private static bool DisplayFits(ManagedCalculatorC158 calculator,
        int outerWidth, int outerHeight)
    {
        return calculator.DisplayX >= 0 && calculator.DisplayY >= 0 &&
            calculator.DisplayX + 240 <= outerWidth &&
            calculator.DisplayY + GuideXosLabel.TextRowHeight <= outerHeight - 24;
    }

    private static bool DisplayIs(ManagedCalculatorC158 calculator,
        string expected)
    {
        Span<char> display = stackalloc char[20];
        return calculator.Controller.TryFormatDisplay(display, out int length) &&
            display[..length].SequenceEqual(expected.AsSpan());
    }

    private static int[] CaptureButtonX(GuideXosCalculatorControllerC158 controller)
    {
        int[] positions = new int[18];
        for (int index = 0; index < positions.Length; index++)
            positions[index] = controller.ButtonAt(index).X;
        return positions;
    }

    private static int[] CaptureButtonY(GuideXosCalculatorControllerC158 controller)
    {
        int[] positions = new int[18];
        for (int index = 0; index < positions.Length; index++)
            positions[index] = controller.ButtonAt(index).Y;
        return positions;
    }

    private static bool ButtonPositionsEqual(
        GuideXosCalculatorControllerC158 controller, int[] x, int[] y)
    {
        for (int index = 0; index < 18; index++)
        {
            if (controller.ButtonAt(index).X != x[index] ||
                controller.ButtonAt(index).Y != y[index]) return false;
        }
        return true;
    }

    private static bool Case(ref int cases, string name, bool passed)
    {
        ++cases;
        if (!passed && string.IsNullOrEmpty(FirstFailedCase))
            FirstFailedCase = name;
        return passed;
    }

    private static GuideXosInputEvent PointerAt(GuideXosButton button) =>
        GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
            GuideXosPointerButton.Primary, button.X + button.Width / 2,
            button.Y + button.Height / 2);

    private static void Keyboard(GuideXosCalculatorControllerC158 controller,
        string characters)
    {
        foreach (char character in characters)
            controller.RouteInput(GuideXosInputEvent.ForKeyChar(character));
    }

    private static readonly int[] ColumnX = { 12, 80, 148, 216 };
}
#endif
