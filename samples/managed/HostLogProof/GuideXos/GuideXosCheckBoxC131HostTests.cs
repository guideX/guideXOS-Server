using System;

namespace HostLogProof;

/// <summary>Focused C131 host, Panel, modal, and interrupted-gesture probes.</summary>
public static class GuideXosCheckBoxC131HostTests
{
    private const int ExpectedCaseCount = 23;
    private static int s_caseCount;

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        bool result = RegistrationAndTraversal() &&
            PointerAndDisabledRouting() &&
            InterruptedSpaceLifecycle() &&
            ModalIsolation() &&
            ResetAndRelaunchAccounting();
        if (host != null)
        {
            Span<byte> line = stackalloc byte[128];
            int position = 0;
            GuideXosText.Append(line, ref position,
                "C131-CHECKBOX-HOST-TESTS cases="u8);
            GuideXosText.AppendUnsigned(line, ref position, (uint)s_caseCount);
            GuideXosText.Append(line, ref position,
                result ? " result=PASS"u8 : " result=FAIL"u8);
            host.TryLog(line[..position]);
        }
        return result && s_caseCount == ExpectedCaseCount;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static GuideXosControlHost CreateHost(
        out GuideXosButton button,
        out GuideXosCheckBox checkBox)
    {
        GuideXosControlHost host = new(2);
        button = new GuideXosButton(10, 60, 80, 24, "Fallback");
        checkBox = new GuideXosCheckBox(100, 20, 160, 24, "Show status");
        host.TryRegisterCheckBox(1, checkBox);
        host.TryRegisterButton(2, button);
        return host;
    }

    private static bool RegistrationAndTraversal()
    {
        GuideXosCheckBox checkBox =
            new(10, 20, 160, 24, "Show status");
        GuideXosControlHost host = new(1);
        bool registered = Check(host.TryRegisterCheckBox(7, checkBox) ==
            GuideXosControlHostResult.Registered && host.RegistrationCount == 1);
        bool overflow = Check(host.TryRegisterCheckBox(8,
            new GuideXosCheckBox(10, 20, 160, 24, "Other")) ==
            GuideXosControlHostResult.Rejected && host.RegistrationCount == 1);

        GuideXosButton button = new(10, 60, 80, 24, "Fallback");
        GuideXosControlHost traversalHost = new(2);
        traversalHost.TryRegisterCheckBox(1, checkBox);
        traversalHost.TryRegisterButton(2, button);
        bool forward = Check(traversalHost.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed &&
            traversalHost.ActiveControlId == 1 && checkBox.IsFocused);
        bool reverse = Check(traversalHost.HandleKey(
            GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed &&
            traversalHost.ActiveControlId == 2 && button.IsFocused);
        bool reverseAgain = Check(traversalHost.HandleKey(
            GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed &&
            traversalHost.ActiveControlId == 1 && checkBox.IsFocused);
        return registered && overflow && forward && reverse && reverseAgain;
    }

    private static bool PointerAndDisabledRouting()
    {
        GuideXosControlHost host = CreateHost(out _, out GuideXosCheckBox checkBox);
        int callbackCount = 0;
        checkBox.Changed = _ => ++callbackCount;
        bool focused = Check(host.FocusAndRoutePointer(1, 250, 30) ==
            GuideXosControlHostResult.Toggled && checkBox.Checked &&
            checkBox.IsFocused && callbackCount == 1);
        bool oneClick = Check(callbackCount == 1);
        checkBox.Enabled = false;
        bool disabledPointer = Check(host.FocusAndRoutePointer(1, 250, 30) ==
            GuideXosControlHostResult.Disabled && checkBox.Checked &&
            callbackCount == 1);
        bool disabledKeyboard = Check(host.TryFocus(1) ==
            GuideXosControlHostResult.Disabled && checkBox.Checked &&
            callbackCount == 1);
        checkBox.Enabled = true;
        checkBox.Visible = false;
        bool hidden = Check(host.RefreshVisibility() ==
            GuideXosControlHostResult.Focused && host.ActiveControlId == 2 &&
            host.FocusAndRoutePointer(1, 250, 30) ==
                GuideXosControlHostResult.Rejected);
        return focused && oneClick && disabledPointer && disabledKeyboard && hidden;
    }

    private static bool InterruptedSpaceLifecycle()
    {
        bool visibility = Check(InterruptedByVisibility());
        bool enabled = Check(InterruptedByEnabledState());
        bool focus = Check(InterruptedByFocusChange());
        bool membership = Check(InterruptedByPanelMembership());
        bool staleKeyChar = Check(StaleKeyCharCannotToggleRecoveredTarget());
        return visibility && enabled && focus && membership && staleKeyChar;
    }

    private static bool BeginPending(
        GuideXosControlHost host, GuideXosCheckBox checkBox)
    {
        return host.TryFocus(1) == GuideXosControlHostResult.Focused &&
            host.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosControlHostResult.Ignored && !checkBox.Checked;
    }

    private static bool InterruptedByVisibility()
    {
        GuideXosControlHost host = CreateHost(out _, out GuideXosCheckBox checkBox);
        bool began = BeginPending(host, checkBox);
        checkBox.Visible = false;
        host.RefreshVisibility();
        checkBox.Visible = true;
        host.RefreshVisibility();
        return began && host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !checkBox.Checked &&
            host.ActiveControlId == 2;
    }

    private static bool InterruptedByEnabledState()
    {
        GuideXosControlHost host = CreateHost(out _, out GuideXosCheckBox checkBox);
        bool began = BeginPending(host, checkBox);
        checkBox.Enabled = false;
        host.RefreshVisibility();
        checkBox.Enabled = true;
        host.RefreshVisibility();
        return began && host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !checkBox.Checked &&
            host.ActiveControlId == 2;
    }

    private static bool InterruptedByFocusChange()
    {
        GuideXosControlHost host = CreateHost(out GuideXosButton button,
            out GuideXosCheckBox checkBox);
        bool began = BeginPending(host, checkBox);
        bool changed = host.TryFocus(2) == GuideXosControlHostResult.Focused &&
            button.IsFocused;
        return began && changed && host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !checkBox.Checked;
    }

    private static bool InterruptedByPanelMembership()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosControlHost host = CreateHost(out _, out GuideXosCheckBox checkBox);
        bool added = panel.TryAddChild(checkBox, 8, 14) ==
            GuideXosPanelResult.Added;
        bool began = added && BeginPending(host, checkBox);
        bool removed = panel.TryRemoveChild(checkBox) ==
            GuideXosPanelResult.Removed && checkBox.ParentPanel == null;
        return began && removed && host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !checkBox.Checked &&
            host.ActiveControlId == 2;
    }

    private static bool StaleKeyCharCannotToggleRecoveredTarget()
    {
        GuideXosControlHost host = CreateHost(out _, out GuideXosCheckBox checkBox);
        bool began = BeginPending(host, checkBox);
        checkBox.Visible = false;
        host.RefreshVisibility();
        checkBox.Visible = true;
        host.RefreshVisibility();
        bool staleConsumed = host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !checkBox.Checked;
        bool ordinary = host.TryFocus(1) == GuideXosControlHostResult.Focused &&
            host.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosControlHostResult.Ignored &&
            host.HandleCharacter(' ') == GuideXosControlHostResult.Toggled &&
            checkBox.Checked;
        return began && staleConsumed && ordinary;
    }

    private static bool ModalIsolation()
    {
        GuideXosControlHost main = CreateHost(out _, out GuideXosCheckBox checkBox);
        GuideXosControlHost modal = new(1);
        GuideXosButton modalButton = new(10, 10, 80, 24, "Modal");
        modal.TryRegisterButton(9, modalButton);
        bool began = BeginPending(main, checkBox);
        bool entered = main.EnterModal(modal) && !checkBox.IsFocused &&
            main.ActiveControlId == 0;
        bool modalInput = main.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !checkBox.Checked;
        modal.TryFocus(9);
        bool exited = main.ExitModal() && main.ActiveControlId == 1 &&
            checkBox.IsFocused;
        bool resumed = main.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored &&
            main.HandleCharacter(' ') == GuideXosControlHostResult.Toggled &&
            checkBox.Checked;
        return Check(began) && Check(entered) && Check(modalInput) &&
            Check(exited) && Check(resumed);
    }

    private static bool ResetAndRelaunchAccounting()
    {
        GuideXosCheckBox first =
            new(10, 20, 160, 24, "Show status", true);
        GuideXosControlHost host = new(2);
        GuideXosButton button = new(10, 60, 80, 24, "Fallback");
        host.TryRegisterCheckBox(1, first);
        host.TryRegisterButton(2, button);
        bool initial = Check(host.RegistrationCount == 2 &&
            host.ActiveIndex == -1 && first.Checked);
        host.TryFocus(1);
        host.Reset();
        bool reset = Check(host.RegistrationCount == 0 && host.ActiveIndex == -1 &&
            !first.IsFocused);
        host.TryRegisterCheckBox(1, first);
        host.TryRegisterButton(2, button);
        bool relaunched = Check(host.RegistrationCount == 2 &&
            host.ActiveIndex == -1 && first.Checked);
        return initial && reset && relaunched;
    }
}
