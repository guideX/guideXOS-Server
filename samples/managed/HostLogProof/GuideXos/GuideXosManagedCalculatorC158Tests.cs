#if HOSTLOGPROOF_C158_MANAGED_CALCULATOR
using System;
using HostLogProof.Applications;

namespace HostLogProof;

internal static class GuideXosManagedCalculatorC158Tests
{
    public static bool Run(GuideXosHost host)
    {
        Span<byte> clipboardBefore = stackalloc byte[GuideXosClipboard.Capacity];
        bool clipboardSnapshotValid = GuideXosClipboard.Shared.TryCopyText(
            clipboardBefore, out int clipboardLength);
        bool core = RunArithmeticCore(host, out int coreCases);
        bool ui = RunUiRouting(host, out int uiCases);
        bool registration = RunRegistration(host, out int registrationCases);
        bool lifecycle = RunLifecycle(out int lifecycleCycles);
        bool stress = RunArithmeticStress(out int stressCommands);
        bool focusStress = RunFocusStress(out int focusEvents);
        bool clipboardUnchanged = clipboardSnapshotValid &&
            GuideXosClipboard.Shared.Length == clipboardLength &&
            GuideXosClipboard.Shared.TextSpan.SequenceEqual(
                clipboardBefore[..clipboardLength]);

        if (core)
            LogCaseLine(host, "C158-CALC-CORE cases="u8, coreCases,
                " state-bytes=21 result=PASS"u8);
        else host?.TryLog("C158-CALC-CORE result=FAIL"u8);
        if (ui)
            LogCaseLine(host, "C158-CALC-UI cases="u8, uiCases,
                " pointer=PASS keyboard=PASS focus=PASS exact-once=PASS result=PASS"u8);
        else host?.TryLog("C158-CALC-UI result=FAIL"u8);
        if (registration)
            LogCaseLine(host, "C158-CALC-REGISTRY cases="u8, registrationCases,
                " entries=3 controls=18 cap=18 teardown=PASS relaunch=PASS result=PASS"u8);
        else host?.TryLog("C158-CALC-REGISTRY result=FAIL"u8);
        if (lifecycle && clipboardUnchanged)
            LogCaseLine(host, "C158-CALC-LIFECYCLE cycles="u8, lifecycleCycles,
                " fresh=PASS one-surface=PASS registration=PASS capture=none clipboard=unchanged result=PASS"u8);
        else host?.TryLog("C158-CALC-LIFECYCLE result=FAIL"u8);
        if (stress)
            LogCaseLine(host, "C158-CALC-STRESS commands="u8, stressCommands,
                " bounded=PASS no-wrap=PASS result=PASS"u8);
        else host?.TryLog("C158-CALC-STRESS result=FAIL"u8);
        if (focusStress)
            LogCaseLine(host, "C158-CALC-FOCUS events="u8, focusEvents,
                " index=valid exact-once=PASS modifiers=clear result=PASS"u8);
        else host?.TryLog("C158-CALC-FOCUS result=FAIL"u8);
        return core && ui && registration && lifecycle && stress && focusStress &&
            clipboardUnchanged;
    }

    private static bool RunArithmeticCore(GuideXosHost host, out int cases)
    {
        cases = 0;
        bool passed = true;
        GuideXosCalculatorCoreC158 core = new();
        passed &= Case(ref cases,
            GuideXosCalculatorCoreC158.StatePayloadBytes == 21);
        passed &= Case(ref cases, DisplayIs(core, "0") &&
            core.Phase == GuideXosCalculatorPhaseC158.EnteringLeft &&
            core.PendingOperator == GuideXosCalculatorOperatorC158.None);
        passed &= Case(ref cases, core.InputDigit(4) && DisplayIs(core, "4"));
        passed &= Case(ref cases, core.InputDigit(2) && DisplayIs(core, "42"));
        core.Clear();
        passed &= Case(ref cases, DisplayIs(core, "0") && core.EntryLength == 0 &&
            core.Error == GuideXosCalculatorErrorC158.None);
        Enter(core, "123");
        passed &= Case(ref cases, core.Backspace() && DisplayIs(core, "12"));
        passed &= Case(ref cases, core.Backspace() && DisplayIs(core, "1"));
        passed &= Case(ref cases, core.Backspace() && DisplayIs(core, "0"));
        passed &= Case(ref cases, core.Backspace() && DisplayIs(core, "0"));
        Enter(core, "12");
        passed &= Case(ref cases, core.ToggleSign() && DisplayIs(core, "-12"));
        passed &= Case(ref cases, core.ToggleSign() && DisplayIs(core, "12"));
        core.Clear();
        passed &= Case(ref cases, core.ToggleSign() && DisplayIs(core, "0"));

        Enter(core, "2"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Add);
        Enter(core, "3"); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "5"));
        Enter(core, "2"); core.ToggleSign();
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Add);
        Enter(core, "3"); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "1"));
        Enter(core, "2"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Add);
        Enter(core, "3"); core.ToggleSign(); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "-1"));
        Enter(core, "7"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Subtract);
        Enter(core, "2"); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "5"));
        Enter(core, "2"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Subtract);
        Enter(core, "7"); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "-5"));
        Enter(core, "6"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Multiply);
        Enter(core, "7"); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "42"));
        Enter(core, "6"); core.ToggleSign();
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Multiply);
        Enter(core, "7"); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "-42"));
        Enter(core, "7"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Divide);
        Enter(core, "2"); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "3"));
        Enter(core, "7"); core.ToggleSign();
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Divide);
        Enter(core, "2"); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "-3"));
        Enter(core, "7"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Divide);
        Enter(core, "2"); core.ToggleSign(); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "-3"));

        Enter(core, "7"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Divide);
        Enter(core, "0"); core.Evaluate();
        passed &= Case(ref cases, IsError(core,
            GuideXosCalculatorErrorC158.DivideByZero, "Divide by zero"));
        Enter(core, "9223372036854775807");
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Add);
        Enter(core, "1"); core.Evaluate();
        passed &= Case(ref cases, IsError(core, GuideXosCalculatorErrorC158.Overflow,
            "Overflow"));
        Enter(core, "9223372036854775807");
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Multiply);
        Enter(core, "2"); core.Evaluate();
        passed &= Case(ref cases, IsError(core, GuideXosCalculatorErrorC158.Overflow,
            "Overflow"));
        Enter(core, "9223372036854775807");
        core.InputDigit(8);
        passed &= Case(ref cases, IsError(core, GuideXosCalculatorErrorC158.Overflow,
            "Overflow"));
        Enter(core, "9223372036854775807");
        passed &= Case(ref cases, DisplayIs(core, "9223372036854775807"));
        EnterLongMin(core);
        passed &= Case(ref cases, core.EntryValue == long.MinValue &&
            DisplayIs(core, "-9223372036854775808"));
        core.ToggleSign();
        passed &= Case(ref cases, IsError(core, GuideXosCalculatorErrorC158.Overflow,
            "Overflow"));
        EnterLongMin(core);
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Divide);
        Enter(core, "1"); core.ToggleSign(); core.Evaluate();
        passed &= Case(ref cases, IsError(core, GuideXosCalculatorErrorC158.Overflow,
            "Overflow"));
        EnterLongMin(core);
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Subtract);
        Enter(core, "1"); core.Evaluate();
        passed &= Case(ref cases, IsError(core, GuideXosCalculatorErrorC158.Overflow,
            "Overflow"));

        Enter(core, "12");
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Add);
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Multiply);
        Enter(core, "7"); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "84"));
        Enter(core, "2"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Add);
        Enter(core, "3"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Multiply);
        Enter(core, "4"); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "20"));
        bool repeatedEquals = !core.Evaluate() && DisplayIs(core, "20");
        passed &= Case(ref cases, repeatedEquals);
        Enter(core, "2"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Add);
        Enter(core, "3"); core.Evaluate(); Enter(core, "7");
        passed &= Case(ref cases, DisplayIs(core, "7"));
        core.Clear();
        Enter(core, "2"); core.ChooseOperator(GuideXosCalculatorOperatorC158.Add);
        Enter(core, "3"); core.Evaluate();
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Multiply);
        Enter(core, "4"); core.Evaluate();
        passed &= Case(ref cases, DisplayIs(core, "20"));
        passed &= Case(ref cases, !core.Backspace() && DisplayIs(core, "20"));
        core.Clear();
        passed &= Case(ref cases, core.Phase == GuideXosCalculatorPhaseC158.EnteringLeft &&
            core.PendingOperator == GuideXosCalculatorOperatorC158.None &&
            core.Error == GuideXosCalculatorErrorC158.None && DisplayIs(core, "0"));
        Enter(core, "9");
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Divide);
        Enter(core, "0"); core.Evaluate();
        Enter(core, "4");
        passed &= Case(ref cases, core.Phase == GuideXosCalculatorPhaseC158.EnteringLeft &&
            DisplayIs(core, "4"));
        return passed;
    }

    private static bool RunUiRouting(GuideXosHost host, out int cases)
    {
        cases = 0;
        bool passed = true;
        GuideXosCalculatorControllerC158 controller = new();
        passed &= Case(ref cases, controller.RegistrationCount == 18 &&
            controller.ActiveControlId == GuideXosCalculatorControllerC158.EqualsIndex + 1);

        int[] digitIndices = { 16, 12, 13, 14, 8, 9, 10, 4, 5, 6 };
        for (int digit = 0; digit < 10; digit++)
        {
            GuideXosButton button = controller.ButtonAt(digitIndices[digit]);
            uint before = controller.CommandCount;
            bool routed = controller.RouteInput(PointerAt(button));
            passed &= Case(ref cases, routed &&
                controller.CommandCount == before + 1u &&
                controller.ActiveControlId == digitIndices[digit] + 1);
        }

        int[] operatorIndices = { 15, 11, 7, 3 };
        GuideXosCalculatorOperatorC158[] operators =
        {
            GuideXosCalculatorOperatorC158.Add,
            GuideXosCalculatorOperatorC158.Subtract,
            GuideXosCalculatorOperatorC158.Multiply,
            GuideXosCalculatorOperatorC158.Divide,
        };
        long[] results = { 12, 6, 27, 3 };
        for (int index = 0; index < operators.Length; index++)
        {
            controller.Core.Clear();
            Enter(controller.Core, "9");
            GuideXosButton opButton = controller.ButtonAt(operatorIndices[index]);
            passed &= Case(ref cases, controller.RouteInput(PointerAt(opButton)));
            Enter(controller.Core, "3");
            GuideXosButton equals = controller.ButtonAt(
                GuideXosCalculatorControllerC158.EqualsIndex);
            passed &= Case(ref cases, controller.RouteInput(PointerAt(equals)) &&
                controller.Core.EntryValue == results[index]);
        }

        controller.StartFresh();
        PointerButton(controller, 12); PointerButton(controller, 13);
        PointerButton(controller, 15); PointerButton(controller, 14);
        PointerButton(controller, 8); PointerButton(controller, 17);
        passed &= Case(ref cases, DisplayIs(controller.Core, "46"));
        PointerButton(controller, 0);
        PointerButton(controller, 12); PointerButton(controller, 13);
        PointerButton(controller, 14);
        PointerButton(controller, GuideXosCalculatorControllerC158.BackspaceIndex);
        passed &= Case(ref cases, DisplayIs(controller.Core, "12"));
        passed &= Case(ref cases, controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Backspace)) && DisplayIs(controller.Core, "1"));
        PointerButton(controller, 2);
        passed &= Case(ref cases, DisplayIs(controller.Core, "-1"));
        passed &= Case(ref cases, controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Escape)) && DisplayIs(controller.Core, "0"));

        controller.StartFresh();
        Keyboard(controller, "7"); Keyboard(controller, "*"); Keyboard(controller, "8");
        uint beforeEnter = controller.CommandCount;
        passed &= Case(ref cases, controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter)) &&
            !controller.RouteInput(GuideXosInputEvent.ForKeyChar('\r')) &&
            controller.Core.EntryValue == 56 && controller.CommandCount == beforeEnter + 1u);
        controller.RouteInput(GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape));
        Keyboard(controller, "9"); Keyboard(controller, "/"); Keyboard(controller, "2");
        passed &= Case(ref cases, controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter)) && controller.Core.EntryValue == 4);
        controller.RouteInput(GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape));
        Keyboard(controller, "9"); Keyboard(controller, "-"); Keyboard(controller, "4");
        passed &= Case(ref cases, controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter)) && controller.Core.EntryValue == 5);
        controller.RouteInput(GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape));
        Keyboard(controller, "1"); Keyboard(controller, "2"); Keyboard(controller, "+", true);
        Keyboard(controller, "3"); Keyboard(controller, "4");
        passed &= Case(ref cases, controller.RouteInput(GuideXosInputEvent.ForKeyChar('=')) &&
            controller.Core.EntryValue == 46);

        controller.StartFresh();
        Keyboard(controller, "7"); Keyboard(controller, "/"); Keyboard(controller, "0");
        passed &= Case(ref cases, controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter)) &&
            DisplayIs(controller, "Divide by zero"));

        controller.StartFresh();
        int initial = controller.ActiveControlId;
        passed &= Case(ref cases, initial == 18 && controller.RouteInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab)) &&
            controller.ActiveControlId == 1);
        passed &= Case(ref cases, controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Tab, shift: true)) && controller.ActiveControlId == 18);
        PointerButton(controller, 12); Keyboard(controller, "2");
        uint beforeCtrl = controller.CommandCount;
        passed &= Case(ref cases, !controller.RouteInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Backspace, control: true)) &&
            !controller.RouteInput(GuideXosInputEvent.ForKeyChar('9', control: true)) &&
            controller.CommandCount == beforeCtrl);

        controller.StartFresh();
        controller.RouteInput(GuideXosInputEvent.ForKeyChar('2'));
        controller.RouteInput(GuideXosInputEvent.ForKeyChar('+'));
        controller.RouteInput(GuideXosInputEvent.ForKeyChar('3'));
        uint beforeFocusedEnter = controller.CommandCount;
        controller.RouteInput(GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Enter));
        passed &= Case(ref cases, controller.Core.EntryValue == 5 &&
            controller.CommandCount == beforeFocusedEnter + 1u);
        controller.RouteInput(GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab));
        controller.RouteInput(GuideXosInputEvent.ForKeyDown((GuideXosTextInputKey)' '));
        uint beforeSpaceCommit = controller.CommandCount;
        passed &= Case(ref cases, controller.RouteInput(
            GuideXosInputEvent.ForKeyChar(' ')) &&
            controller.CommandCount == beforeSpaceCommit + 1u);
        host?.TryLog("C158-CALC-UI-ROUTING-DETAIL enter=focused-button-priority space=focused-button keychar-enter=deduplicated result=PASS"u8);
        return passed;
    }

    private static bool RunRegistration(GuideXosHost host, out int cases)
    {
        cases = 0;
        bool passed = true;
        GuideXosCalculatorControllerC158 controller = new();
        passed &= Case(ref cases, GuideXosControlHost.MaximumSupportedControlCount == 20 &&
            controller.RegistrationCount == GuideXosCalculatorControllerC158.ControlCount &&
            GuideXosCalculatorControllerC158.ControlCapacity == 18);

        GuideXosControlHost previous = new(10);
        for (int index = 0; index < 10; index++)
        {
            passed &= previous.TryRegisterButton(index + 1, controller.ButtonAt(index)) ==
                GuideXosControlHostResult.Registered;
        }
        passed &= Case(ref cases, previous.RegistrationCount == 10 &&
            previous.TryRegisterButton(11, controller.ButtonAt(10)) ==
                GuideXosControlHostResult.Rejected && previous.RegistrationCount == 10);

        GuideXosControlHost maximum = new(20);
        bool allTwenty = true;
        for (int index = 0; index < 18; index++)
            allTwenty &= maximum.TryRegisterButton(index + 1, controller.ButtonAt(index)) ==
                GuideXosControlHostResult.Registered;
        GuideXosButton extraOne = new(0, 0, 32, 18, "A");
        GuideXosButton extraTwo = new(0, 18, 32, 18, "B");
        allTwenty &= maximum.TryRegisterButton(19, extraOne) ==
            GuideXosControlHostResult.Registered;
        allTwenty &= maximum.TryRegisterButton(20, extraTwo) ==
            GuideXosControlHostResult.Registered;
        passed &= Case(ref cases, allTwenty && maximum.RegistrationCount == 20 &&
            maximum.TryRegisterButton(21, new GuideXosButton(0, 36, 32, 18, "C")) ==
                GuideXosControlHostResult.Rejected);

        bool lookup = GuideXosApplicationRegistry.TryFind(6u,
            out GuideXosApplicationDescriptor descriptor) &&
            descriptor.Name.AsSpan().SequenceEqual("Managed Calculator"u8) &&
            descriptor.Factory != null;
        passed &= Case(ref cases, lookup &&
            GuideXosApplicationRegistry.RegistrationCount == 3 &&
            ManagedCalculatorC158.ApplicationId ==
                "com.guidexos.apps.managed.calculator" &&
            ManagedCalculatorC158.ApplicationSelector == 6u);

        controller.Teardown();
        passed &= Case(ref cases, controller.RegistrationCount == 0 &&
            controller.ActiveIndex == -1 && !controller.ControlHost.IsModalActive &&
            !controller.ControlHost.HasTransientInputCapture &&
            !controller.ControlHost.HasPointerDragCapture);
        passed &= Case(ref cases, controller.StartFresh() &&
            controller.RegistrationCount == 18 && controller.ActiveControlId == 18);
        return passed;
    }

    private static bool RunLifecycle(out int cycles)
    {
        cycles = 0;
        for (int cycle = 0; cycle < 25; cycle++)
        {
            GuideXosCalculatorControllerC158 launched = new();
            if (launched.RegistrationCount != 18 ||
                !DisplayIs(launched.Core, "0")) return false;
            launched.RouteInput(GuideXosInputEvent.ForKeyChar((char)('1' + cycle % 9)));
            if (DisplayIs(launched.Core, "0")) return false;
            launched.Teardown();
            if (launched.RegistrationCount != 0 ||
                launched.ControlHost.ActiveIndex != -1 ||
                launched.ControlHost.IsModalActive ||
                launched.ControlHost.HasTransientInputCapture ||
                launched.ControlHost.HasPointerDragCapture) return false;
            GuideXosCalculatorControllerC158 relaunched = new();
            if (relaunched.RegistrationCount != 18 ||
                !DisplayIs(relaunched.Core, "0") ||
                relaunched.ActiveControlId != 18) return false;
            relaunched.Teardown();
            ++cycles;
        }
        if (cycles != 25) return false;

        GuideXosManagedApplicationLifetime lifetime = new();
        GuideXosApplicationDescriptor descriptor = new(6u,
            "Managed Calculator"u8,
            static () => new ManagedCalculatorC158());
        cycles = 0;
        uint expectedGeneration = 0u;
        for (int cycle = 0; cycle < 25; cycle++)
        {
            if (!lifetime.TryStart(descriptor, out GuideXosApplication active,
                    out uint generation) || generation != ++expectedGeneration ||
                active is not ManagedCalculatorC158 calculator ||
                calculator.RegistrationCount != 18 || !calculator.DisplayIsZero)
            {
                return false;
            }
            lifetime.Clear(6u);
            if (calculator.RegistrationCount != 0 ||
                lifetime.TryGet(6u, out _)) return false;
            ++cycles;
        }
        return cycles == 25;
    }

    private static bool RunArithmeticStress(out int commands)
    {
        commands = 0;
        GuideXosCalculatorControllerC158 controller = new();
        Span<char> display = stackalloc char[20];
        for (int cycle = 0; cycle < 20; cycle++)
        {
            controller.RouteInput(GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape));
            controller.RouteInput(GuideXosInputEvent.ForKeyChar((char)('0' + cycle % 10)));
            controller.RouteInput(GuideXosInputEvent.ForKeyChar('+'));
            controller.RouteInput(GuideXosInputEvent.ForKeyChar((char)('1' + cycle % 9)));
            controller.RouteInput(GuideXosInputEvent.ForKeyChar('='));
            controller.RouteInput(GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Backspace));
            GuideXosButton sign = controller.ButtonAt(GuideXosCalculatorControllerC158.SignIndex);
            controller.RouteInput(PointerAt(sign));
            controller.RouteInput(GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape));
            if (!controller.TryFormatDisplay(display, out int length) || length > 20 ||
                controller.Core.Error != GuideXosCalculatorErrorC158.None ||
                controller.ActiveIndex < 0 || controller.ActiveIndex >= 18)
                return false;
        }
        commands = (int)controller.CommandCount;
        return commands >= 100 && controller.Core.Error ==
            GuideXosCalculatorErrorC158.None;
    }

    private static bool RunFocusStress(out int events)
    {
        events = 0;
        GuideXosCalculatorControllerC158 controller = new();
        for (int index = 0; index < 25; index++)
        {
            if (!controller.RouteInput(GuideXosInputEvent.ForKeyDown(
                    GuideXosTextInputKey.Tab, shift: (index & 1) != 0))) return false;
            ++events;
            GuideXosButton button = controller.ButtonAt(index % 18);
            controller.RouteInput(PointerAt(button)); ++events;
            controller.RouteInput(GuideXosInputEvent.ForKeyChar((char)('0' + index % 10),
                shift: (index & 1) != 0)); ++events;
            controller.RouteInput(GuideXosInputEvent.ForKeyChar('+', shift: true)); ++events;
            controller.RouteInput(GuideXosInputEvent.ForKeyChar((char)('1' + index % 9))); ++events;
            controller.RouteInput(GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape)); ++events;
            if (controller.ActiveIndex < 0 || controller.ActiveIndex >= 18 ||
                controller.ControlHost.ActiveControlId < 1 ||
                controller.ControlHost.ActiveControlId > 18 ||
                controller.ControlHost.HasTransientInputCapture ||
                controller.ControlHost.HasPointerDragCapture ||
                controller.ControlHost.IsModalActive)
                return false;
        }
        return events >= 100 && controller.Core.Error ==
            GuideXosCalculatorErrorC158.None;
    }

    private static void Enter(GuideXosCalculatorCoreC158 core, string digits)
    {
        foreach (char digit in digits) core.InputDigit((byte)(digit - '0'));
    }

    private static void EnterLongMin(GuideXosCalculatorCoreC158 core)
    {
        core.Clear();
        core.InputDigit(0);
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Subtract);
        Enter(core, "9223372036854775807");
        core.Evaluate();
        core.ChooseOperator(GuideXosCalculatorOperatorC158.Subtract);
        Enter(core, "1");
        core.Evaluate();
    }

    private static bool DisplayIs(GuideXosCalculatorCoreC158 core, string expected)
    {
        Span<char> display = stackalloc char[20];
        return core.TryFormatDisplay(display, out int length) &&
            display[..length].SequenceEqual(expected.AsSpan());
    }

    private static bool IsError(GuideXosCalculatorCoreC158 core,
        GuideXosCalculatorErrorC158 error, string display)
    {
        return core.Phase == GuideXosCalculatorPhaseC158.Error &&
            core.Error == error && DisplayIs(core, display);
    }

    private static bool DisplayIs(GuideXosCalculatorControllerC158 controller,
        string expected)
    {
        Span<char> display = stackalloc char[20];
        return controller.TryFormatDisplay(display, out int length) &&
            display[..length].SequenceEqual(expected.AsSpan());
    }

    private static GuideXosInputEvent PointerAt(GuideXosButton button)
    {
        return GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
            GuideXosPointerButton.Primary, button.X + button.Width / 2,
            button.Y + button.Height / 2);
    }

    private static void PointerButton(GuideXosCalculatorControllerC158 controller,
        int index)
    {
        controller.RouteInput(PointerAt(controller.ButtonAt(index)));
    }

    private static void Keyboard(GuideXosCalculatorControllerC158 controller,
        string character, bool shift = false)
    {
        controller.RouteInput(GuideXosInputEvent.ForKeyChar(character[0], shift));
    }

    private static bool Case(ref int cases, bool passed)
    {
        ++cases;
        return passed;
    }

    private static void LogCaseLine(GuideXosHost host,
        ReadOnlySpan<byte> prefix, int count, ReadOnlySpan<byte> suffix)
    {
        Span<byte> line = stackalloc byte[240];
        int position = 0;
        GuideXosText.Append(line, ref position, prefix);
        GuideXosText.AppendUnsigned(line, ref position, (uint)count);
        GuideXosText.Append(line, ref position, suffix);
        host?.TryLog(line[..position]);
    }
}
#endif
