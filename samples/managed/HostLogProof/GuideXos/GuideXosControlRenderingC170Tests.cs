#if HOSTLOGPROOF_C170_CONTROL_RENDERING
using System;
using System.Runtime.InteropServices;

namespace HostLogProof;

/// <summary>Hosted checks for the shared button and ComboBox render commands.</summary>
public static unsafe class GuideXosControlRenderingC170Tests
{
    public const int CaseCount = 21;
    public const int ButtonFocusChangeCount = 1000;
    public const int ButtonActivationCount = 500;
    public const int ComboFocusChangeCount = 1000;
    public const int DropDownCycleCount = 500;
    public const int KeyboardSelectionChangeCount = 250;
    public const int PointerSelectionChangeCount = 250;
    public const int SettingsCloseReopenCycleCount = 100;

    private const int MaximumCapturedCommands = 8;
    private const int MaximumCapturedCommandBytes = 96;
    private static readonly byte[] s_textCommands =
        new byte[MaximumCapturedCommands * MaximumCapturedCommandBytes];
    private static readonly int[] s_textLengths =
        new int[MaximumCapturedCommands];
    private static int s_textCommandCount;
    private static int s_fillCommandCount;
    private static int s_selectionChangedCount;
    [UnmanagedCallersOnly]
    private static int CaptureText(
        NativeGxAppContext* context,
        ulong window,
        int x,
        int y,
        byte* text)
    {
        if (text == null || s_textCommandCount >= MaximumCapturedCommands)
            return -2;

        int length = 0;
        while (length < MaximumCapturedCommandBytes - 1 && text[length] != 0)
            length++;
        int offset = s_textCommandCount * MaximumCapturedCommandBytes;
        for (int index = 0; index < length; index++)
            s_textCommands[offset + index] = text[index];
        s_textLengths[s_textCommandCount] = length;
        s_textCommandCount++;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int CaptureFill(
        NativeGxAppContext* context,
        ulong window,
        int x,
        int y,
        int width,
        int height,
        uint color)
    {
        s_fillCommandCount++;
        return 0;
    }

    private static void CountSelectionChange(int index)
    {
        s_selectionChangedCount++;
    }

    public static bool Run(GuideXosHost ignoredHost)
    {
        NativeHostCallTable table = default;
        table.size = GxAbi.HostCallTableSize;
        table.version = GxAbi.HostAbiVersion;
        table.capabilities = (ulong)(GuideXosCapability.Surface |
            GuideXosCapability.Text | GuideXosCapability.Primitive);
        table.drawText = &CaptureText;
        table.drawRect = &CaptureFill;

        NativeGxAppContext context = default;
        context.size = (uint)sizeof(NativeGxAppContext);
        context.apiVersion = GxAbi.ApiVersion;
        context.host = &table;
        if (!GuideXosHost.TryCreate(&context, 0u, out GuideXosHost host,
                out GuideXosResult createResult) ||
            createResult != GuideXosResult.Success ||
            host.TryGetSurface(1u, out GuideXosSurface surface) !=
                GuideXosResult.Success)
        {
            host?.TryLog("C170-CONTROL-RENDER setup=FAIL"u8);
            return false;
        }

        int cases = 0;
        bool passed = true;
        GuideXosButton button = new(10, 18, 128, 18, "Apply");
        GuideXosButton nextButton = new(10, 42, 128, 18, "Reset");
        GuideXosControlHost buttonHost = new(2);
        bool buttonRegistered =
            buttonHost.TryRegisterButton(1, button, true) ==
                GuideXosControlHostResult.Registered &&
            buttonHost.TryRegisterButton(2, nextButton, true) ==
                GuideXosControlHostResult.Registered;
        passed &= Case(ref cases, buttonRegistered &&
            buttonHost.TryFocus(1) == GuideXosControlHostResult.Focused &&
            RenderButton(surface, button, "> [ Apply ]"u8));
        passed &= Case(ref cases, buttonHost.TryFocus(2) ==
                GuideXosControlHostResult.Focused &&
            !button.IsFocused && RenderButton(surface, button, "[ Apply ]"u8));
        passed &= Case(ref cases, buttonHost.TryFocus(1) ==
                GuideXosControlHostResult.Focused && button.IsFocused &&
            buttonHost.ActiveControlId == 1 &&
            RenderButton(surface, button, "> [ Apply ]"u8));
        passed &= Case(ref cases, buttonHost.HandleKey(
                GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed &&
            buttonHost.ActiveControlId == 2 && !button.IsFocused &&
            RenderButton(surface, button, "[ Apply ]"u8));

        GuideXosControlHostResult pointerPress =
            buttonHost.FocusAndRoutePointer(1, 12, 20, 0, 0, 8, 18);
        passed &= Case(ref cases, pointerPress ==
                GuideXosControlHostResult.Activated && button.IsFocused &&
            RenderButton(surface, button, "> [ Apply ]"u8));
        GuideXosControlHostResult pointerRelease =
            buttonHost.HandlePointerUp(12, 20);
        passed &= Case(ref cases, pointerRelease ==
                GuideXosControlHostResult.Ignored && button.IsFocused &&
            RenderButton(surface, button, "> [ Apply ]"u8));
        passed &= Case(ref cases, buttonHost.TryFocus(1) ==
                GuideXosControlHostResult.Focused &&
            buttonHost.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosControlHostResult.Activated && button.IsFocused);

        GuideXosButton disabledButton = new(10, 18, 128, 18, "Apply");
        disabledButton.SetEnabled(false);
        GuideXosControlHost disabledButtonHost = new(1);
        bool disabledButtonRegistered = disabledButtonHost.TryRegisterButton(
            3, disabledButton, true) == GuideXosControlHostResult.Registered;
        GuideXosControlHostResult disabledFocusResult =
            disabledButtonHost.TryFocus(3);
        GuideXosControlHostResult disabledKeyResult =
            disabledButtonHost.HandleKey(GuideXosTextInputKey.Enter);
        GuideXosControlHostResult disabledPointerResult =
            disabledButtonHost.FocusAndRoutePointer(3, 12, 20, 0, 0, 8, 18);
        bool disabledButtonRender = RenderButton(surface, disabledButton,
            "x Apply"u8);
        passed &= Case(ref cases, disabledButtonRegistered &&
            disabledFocusResult == GuideXosControlHostResult.Disabled &&
            (disabledKeyResult == GuideXosControlHostResult.Disabled ||
             disabledKeyResult == GuideXosControlHostResult.Ignored) &&
            disabledPointerResult == GuideXosControlHostResult.Disabled &&
            !disabledButton.IsFocused && disabledButtonRender);

        GuideXosComboBox combo = MakeCombo(72, 128, 4);
        GuideXosButton comboNextButton = new(10, 132, 128, 18, "Apply");
        GuideXosControlHost comboHost = new(2);
        bool comboRegistered =
            comboHost.TryRegisterComboBox(10, combo, true) ==
                GuideXosControlHostResult.Registered &&
            comboHost.TryRegisterButton(11, comboNextButton, true) ==
                GuideXosControlHostResult.Registered;
        passed &= Case(ref cases, comboRegistered &&
            comboHost.TryFocus(10) == GuideXosControlHostResult.Focused &&
            combo.IsFocused && RenderComboFace(surface, combo,
                "> [ Notes"u8, "v]"u8));
        passed &= Case(ref cases, comboHost.HandleKey(
                GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed &&
            comboHost.ActiveControlId == 11 && !combo.IsFocused &&
            RenderComboFace(surface, combo, "[ Notes"u8, "v]"u8));

        _ = comboHost.TryFocus(10);
        GuideXosControlHostResult openResult = comboHost.HandleKey(
            GuideXosTextInputKey.Enter);
        bool opened = combo.IsOpen && comboHost.HasTransientInputCapture &&
            comboHost.TransientInputCaptureOwnerId == 10;
        ClearCapturedCommands();
        GuideXosResult openRenderResult = combo.Render(surface);
        bool openCommandCount = s_textCommandCount == 1 + combo.VisibleRowCount;
        bool openFaceMarkers = CommandStartsWith(0, "> [ Notes"u8) &&
            CommandEndsWith(0, "^]"u8);
        bool openRows = CommandEquals(1, ">* Notes"u8) &&
            CommandEquals(2, "   Calculator"u8) &&
            CommandEquals(3, "   Explorer"u8);
        bool openRendered = openRenderResult == GuideXosResult.Success &&
            openCommandCount && openFaceMarkers && openRows;
        passed &= Case(ref cases,
            (openResult == GuideXosControlHostResult.Activated ||
             openResult == GuideXosControlHostResult.Focused) &&
            opened && openRendered);
        passed &= Case(ref cases, opened && openRendered &&
            CommandEquals(1, ">* Notes"u8));

        GuideXosControlHostResult dismissed = comboHost.HandleKey(
            GuideXosTextInputKey.Escape);
        ClearCapturedCommands();
        bool dismissalClearsRows = combo.Render(surface) ==
                GuideXosResult.Success && s_textCommandCount ==
                1 + combo.VisibleRowCount &&
            CommandStartsWith(0, "> [ Notes"u8) &&
            CommandEndsWith(0, "v]"u8) && CommandLength(1) == 0 &&
            CommandLength(2) == 0 && CommandLength(3) == 0;
        passed &= Case(ref cases, dismissed ==
                GuideXosControlHostResult.Cancelled && !combo.IsOpen &&
            !comboHost.HasTransientInputCapture && dismissalClearsRows);
        passed &= Case(ref cases, comboHost.ActiveControlId == 10 &&
            combo.IsFocused && RenderComboFace(surface, combo,
                "> [ Notes"u8, "v]"u8));

        GuideXosComboBox disabledCombo = MakeCombo(72, 128, 4);
        disabledCombo.SetEnabled(false);
        GuideXosControlHost disabledComboHost = new(1);
        bool disabledComboRegistered = disabledComboHost.TryRegisterComboBox(
            20, disabledCombo, true) == GuideXosControlHostResult.Registered;
        passed &= Case(ref cases, disabledComboRegistered &&
            disabledComboHost.TryFocus(20) == GuideXosControlHostResult.Disabled &&
            disabledCombo.Open() == GuideXosComboBoxResult.Disabled &&
            disabledComboHost.FocusAndRoutePointer(20, 12, 74, 0, 0, 8, 18) ==
                GuideXosControlHostResult.Disabled && !disabledCombo.IsOpen &&
            RenderComboFace(surface, disabledCombo, "x Notes"u8,
                ReadOnlySpan<byte>.Empty));

        _ = comboHost.TryFocus(10);
        _ = comboHost.HandleKey(GuideXosTextInputKey.Enter);
        ClearCapturedCommands();
        _ = combo.Render(surface);
        GuideXosControlHostResult navigated = comboHost.HandleKey(
            GuideXosTextInputKey.Tab);
        ClearCapturedCommands();
        GuideXosResult navigationRenderResult = combo.Render(surface);
        bool staleRowsCleared = navigationRenderResult ==
                GuideXosResult.Success && !combo.IsOpen && !combo.IsFocused &&
            !comboHost.HasTransientInputCapture && s_textCommandCount ==
                1 + combo.VisibleRowCount && CommandStartsWith(0, "[ Notes"u8) &&
            CommandEndsWith(0, "v]"u8) && CommandLength(1) == 0 &&
            CommandLength(2) == 0 && CommandLength(3) == 0 &&
            CommandLength(4) == 0;
        passed &= Case(ref cases, navigated ==
                GuideXosControlHostResult.Traversed &&
            comboHost.ActiveControlId == 11 && staleRowsCleared);

        bool boundsStable = button.X == 10 && button.Y == 18 &&
            button.Width == 128 && button.Height == 18 &&
            buttonHost.FocusAndRoutePointer(1, 137, 35, 0, 0, 8, 18) ==
                GuideXosControlHostResult.Activated &&
            buttonHost.FocusAndRoutePointer(1, 138, 35, 0, 0, 8, 18) ==
                GuideXosControlHostResult.Ignored &&
            combo.X == 10 && combo.Y == 72 && combo.Width == 128 &&
            combo.Height == 18 && combo.ContainsPoint(10, 72) &&
            !combo.ContainsPoint(138, 72) && combo.PopupY == 90 &&
            comboHost.FocusAndRoutePointer(10, 10, 72, 0, 0, 8, 18) ==
                GuideXosControlHostResult.Activated && combo.IsOpen &&
            combo.ContainsDropDownPoint(10, 90) &&
            !combo.ContainsDropDownPoint(10, 162);
        passed &= Case(ref cases, boundsStable);

        ClearCapturedCommands();
        bool fillsUnchanged = button.Render(surface) == GuideXosResult.Success &&
            combo.Render(surface) == GuideXosResult.Success &&
            disabledButton.Render(surface) == GuideXosResult.Success &&
            disabledCombo.Render(surface) == GuideXosResult.Success &&
            s_fillCommandCount == 0;
        passed &= Case(ref cases, fillsUnchanged);

        GuideXosComboBox expandedCombo = new(10, 180,
            GuideXosComboBox.MaximumSupportedWidth, 18, 1, 24, 3);
        bool expandedComboReady = expandedCombo.TryAddItem("Expanded") &&
            expandedCombo.TrySetSelectedIndex(0);
        expandedCombo.Focus();
        ClearCapturedCommands();
        bool expandedComboRendered = expandedComboReady &&
            expandedCombo.Render(surface) == GuideXosResult.Success &&
            s_textCommandCount == 1 && CommandLength(0) == 62 &&
            CommandStartsWith(0, "> [ Expanded"u8) &&
            CommandEndsWith(0, "v]"u8) && s_fillCommandCount == 0;
        passed &= Case(ref cases, expandedComboRendered);

        bool focusedListCompatibility = CheckListBoxMarkers();
        passed &= Case(ref cases, focusedListCompatibility);

        bool allocationFreeFocus = CheckFocusTransitions(buttonHost);
        passed &= Case(ref cases, allocationFreeFocus);

        bool exactCases = cases == CaseCount;
        passed &= exactCases;
        host?.TryLog(passed
            ? "C170-CONTROL-RENDER cases=21 result=PASS"u8
            : "C170-CONTROL-RENDER result=FAIL"u8);
        return passed;
    }

    public static bool RunStress(GuideXosHost host)
    {
        GuideXosButton buttonA = new(10, 18, 128, 18, "Apply");
        GuideXosButton buttonB = new(10, 42, 128, 18, "Reset");
        GuideXosControlHost buttonHost = new(2);
        bool ready = buttonHost.TryRegisterButton(1, buttonA, true) ==
                GuideXosControlHostResult.Registered &&
            buttonHost.TryRegisterButton(2, buttonB, true) ==
                GuideXosControlHostResult.Registered &&
            buttonHost.TryFocus(1) == GuideXosControlHostResult.Focused;

        _ = buttonHost.TryFocus(2);
        _ = buttonHost.TryFocus(1);
        long allocationStart = GC.GetAllocatedBytesForCurrentThread();
        bool buttonFocus = ready;
        for (int index = 0; index < ButtonFocusChangeCount; index++)
        {
            int target = (index & 1) == 0 ? 2 : 1;
            buttonFocus &= buttonHost.TryFocus(target) ==
                GuideXosControlHostResult.Focused &&
                buttonHost.ActiveControlId == target;
        }
        bool buttonFocusNoAllocation =
            GC.GetAllocatedBytesForCurrentThread() == allocationStart;

        _ = buttonHost.TryFocus(1);
        bool activations = true;
        for (int index = 0; index < ButtonActivationCount; index++)
        {
            activations &= buttonHost.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosControlHostResult.Activated && buttonA.IsFocused;
        }

        GuideXosComboBox combo = MakeCombo(72, 128, 4);
        combo.Changed = CountSelectionChange;
        GuideXosButton afterCombo = new(10, 132, 128, 18, "Apply");
        GuideXosControlHost comboHost = new(2);
        bool comboReady = comboHost.TryRegisterComboBox(10, combo, true) ==
                GuideXosControlHostResult.Registered &&
            comboHost.TryRegisterButton(11, afterCombo, true) ==
                GuideXosControlHostResult.Registered &&
            comboHost.TryFocus(10) == GuideXosControlHostResult.Focused;

        _ = comboHost.TryFocus(11);
        _ = comboHost.TryFocus(10);
        allocationStart = GC.GetAllocatedBytesForCurrentThread();
        bool comboFocus = comboReady;
        for (int index = 0; index < ComboFocusChangeCount; index++)
        {
            int target = (index & 1) == 0 ? 11 : 10;
            comboFocus &= comboHost.TryFocus(target) ==
                GuideXosControlHostResult.Focused &&
                comboHost.ActiveControlId == target;
        }
        bool comboFocusNoAllocation =
            GC.GetAllocatedBytesForCurrentThread() == allocationStart;

        _ = comboHost.TryFocus(10);
        bool dropDownCycles = true;
        for (int index = 0; index < DropDownCycleCount; index++)
        {
            GuideXosControlHostResult opened = comboHost.HandleKey(
                GuideXosTextInputKey.Enter);
            dropDownCycles &= (opened == GuideXosControlHostResult.Activated ||
                    opened == GuideXosControlHostResult.Focused) && combo.IsOpen &&
                comboHost.HasTransientInputCapture &&
                comboHost.HandleKey(GuideXosTextInputKey.Escape) ==
                    GuideXosControlHostResult.Cancelled && !combo.IsOpen &&
                !comboHost.HasTransientInputCapture;
        }

        _ = comboHost.TryFocus(10);
        _ = comboHost.HandleKey(GuideXosTextInputKey.Enter);
        bool keyboardSelections = true;
        int priorActiveIndex = combo.ActiveIndex;
        for (int index = 0; index < KeyboardSelectionChangeCount; index++)
        {
            int prior = combo.ActiveIndex;
            GuideXosTextInputKey selectionKey =
                prior == combo.ItemCount - 1
                    ? GuideXosTextInputKey.Up
                    : GuideXosTextInputKey.Down;
            GuideXosControlHostResult moveResult = comboHost.HandleKey(
                selectionKey);
            bool reported = moveResult == GuideXosControlHostResult.Moved;
            bool changed = combo.ActiveIndex != prior && combo.ActiveIndex >= 0;
            keyboardSelections &= reported && changed;
        }
        bool keyboardSelectionStayedPending = combo.SelectedIndex == 0 &&
            combo.ActiveIndex != priorActiveIndex && s_selectionChangedCount == 0;
        keyboardSelections &= keyboardSelectionStayedPending;
        _ = comboHost.HandleKey(GuideXosTextInputKey.Escape);

        s_selectionChangedCount = 0;
        bool pointerSelections = true;
        for (int index = 0; index < PointerSelectionChangeCount; index++)
        {
            int targetIndex = (index & 1) == 0 ? 1 : 0;
            GuideXosControlHostResult pointerOpen =
                comboHost.FocusAndRoutePointer(
                    10, combo.X + 1, combo.Y + 1, 0, 0, 8, 18);
            pointerSelections &=
                (pointerOpen == GuideXosControlHostResult.Focused ||
                 pointerOpen == GuideXosControlHostResult.Activated) && combo.IsOpen &&
                comboHost.HasTransientInputCapture &&
                comboHost.FocusAndRoutePointer(10, combo.X + 1,
                    combo.PopupY + targetIndex * GuideXosComboBox.PopupRowHeight + 1,
                    0, 0, 8, 18) == GuideXosControlHostResult.Changed &&
                combo.SelectedIndex == targetIndex && !combo.IsOpen &&
                !comboHost.HasTransientInputCapture;
        }
        pointerSelections &= s_selectionChangedCount ==
            PointerSelectionChangeCount;

        bool settingsCycles = true;
        for (int index = 0; index < SettingsCloseReopenCycleCount; index++)
        {
            settingsCycles &= GuideXosDefaultAppsC168Tests.Run(null,
                new Applications.ManagedSettingsCenter());
        }

        bool stableCounts = buttonHost.RegistrationCount == 2 &&
            comboHost.RegistrationCount == 2 &&
            !comboHost.HasTransientInputCapture && !combo.IsOpen &&
            !comboHost.IsModalActive && !comboHost.HasPointerDragCapture;
        bool passed = ready && buttonFocus && buttonFocusNoAllocation &&
            activations && comboReady && comboFocus && comboFocusNoAllocation &&
            dropDownCycles && keyboardSelections && pointerSelections &&
            settingsCycles && stableCounts;
        host?.TryLog(passed
            ? "C170-STRESS buttonFocusChanges=1000 buttonActivations=500 comboFocusChanges=1000 dropdownCycles=500 keyboardSelectionChanges=250 pointerSelectionChanges=250 settingsCloseReopenCycles=100 buttonFocusAllocations=0 comboFocusAllocations=0 result=PASS"u8
            : "C170-STRESS result=FAIL"u8);
        return passed;
    }

    private static GuideXosComboBox MakeCombo(int y, int width, int itemCount)
    {
        GuideXosComboBox combo = new(10, y, width, 18,
            Math.Max(itemCount, 4), 24, Math.Max(itemCount, 3));
        combo.TryAddItem("Notes");
        combo.TryAddItem("Calculator");
        combo.TryAddItem("Explorer");
        if (itemCount > 3) combo.TryAddItem("Task Manager");
        combo.TrySetSelectedIndex(0);
        return combo;
    }

    private static bool RenderButton(
        GuideXosSurface surface,
        GuideXosButton button,
        ReadOnlySpan<byte> expected)
    {
        ClearCapturedCommands();
        return button.Render(surface) == GuideXosResult.Success &&
            s_textCommandCount == 1 && CommandEquals(0, expected) &&
            s_fillCommandCount == 0;
    }

    private static bool RenderComboFace(
        GuideXosSurface surface,
        GuideXosComboBox combo,
        ReadOnlySpan<byte> prefix,
        ReadOnlySpan<byte> suffix)
    {
        ClearCapturedCommands();
        return combo.Render(surface) == GuideXosResult.Success &&
            s_textCommandCount == 1 && CommandStartsWith(0, prefix) &&
            CommandEndsWith(0, suffix) && s_fillCommandCount == 0;
    }

    private static bool CheckFocusTransitions(GuideXosControlHost host)
    {
        _ = host.TryFocus(1);
        _ = host.TryFocus(2);
        _ = host.TryFocus(1);
        long before = GC.GetAllocatedBytesForCurrentThread();
        bool transitions = true;
        for (int index = 0; index < 200; index++)
        {
            transitions &= host.TryFocus(2) ==
                    GuideXosControlHostResult.Focused &&
                host.TryFocus(1) == GuideXosControlHostResult.Focused;
        }
        return transitions && host.ActiveControlId == 1 &&
            GC.GetAllocatedBytesForCurrentThread() == before;
    }

    private static bool CheckListBoxMarkers()
    {
        GuideXosListBox list = new(10, 18, 2, 24, 12, 18);
        list.TryAdd("Notes");
        list.TryAdd("Calculator");
        list.Focus();
        bool selectedFocused = list.GetRenderMarker(0) == (byte)'>';
        list.Blur();
        bool selectedUnfocused = list.GetRenderMarker(0) == (byte)'*';
        list.ClearSelection();
        bool unselected = list.GetRenderMarker(0) == (byte)' ';
        list.SetEnabled(false);
        return selectedFocused && selectedUnfocused && unselected &&
            list.GetRenderMarker(0) == (byte)'x';
    }

    private static bool CommandEquals(int commandIndex, ReadOnlySpan<byte> value)
    {
        if (commandIndex < 0 || commandIndex >= s_textCommandCount ||
            s_textLengths[commandIndex] != value.Length)
        {
            return false;
        }
        int offset = commandIndex * MaximumCapturedCommandBytes;
        for (int index = 0; index < value.Length; index++)
        {
            if (s_textCommands[offset + index] != value[index]) return false;
        }
        return true;
    }

    private static bool CommandStartsWith(
        int commandIndex,
        ReadOnlySpan<byte> prefix)
    {
        if (commandIndex < 0 || commandIndex >= s_textCommandCount ||
            s_textLengths[commandIndex] < prefix.Length)
        {
            return false;
        }
        int offset = commandIndex * MaximumCapturedCommandBytes;
        for (int index = 0; index < prefix.Length; index++)
        {
            if (s_textCommands[offset + index] != prefix[index]) return false;
        }
        return true;
    }

    private static bool CommandEndsWith(
        int commandIndex,
        ReadOnlySpan<byte> suffix)
    {
        if (commandIndex < 0 || commandIndex >= s_textCommandCount ||
            s_textLengths[commandIndex] < suffix.Length)
        {
            return false;
        }
        int offset = commandIndex * MaximumCapturedCommandBytes +
            s_textLengths[commandIndex] - suffix.Length;
        for (int index = 0; index < suffix.Length; index++)
        {
            if (s_textCommands[offset + index] != suffix[index]) return false;
        }
        return true;
    }

    private static int CommandLength(int commandIndex)
    {
        return commandIndex >= 0 && commandIndex < s_textCommandCount
            ? s_textLengths[commandIndex] : -1;
    }

    private static void ClearCapturedCommands()
    {
        s_textCommandCount = 0;
        s_fillCommandCount = 0;
    }

    private static bool Case(ref int cases, bool passed)
    {
        cases++;
        return passed;
    }
}
#endif
