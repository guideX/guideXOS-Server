using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>Settings Center confirmation and lifecycle cases for C145.</summary>
public static class GuideXosSettingsCenterC145Tests
{
    public static bool Run(GuideXosHost host,
        Applications.ManagedSettingsCenter app)
    {
        int cases = 0;
        bool initialized = app.InitializeForTests();
        bool result = initialized;
        result &= Case(ref cases, initialized && app.RegistrationCount == 9 &&
            app.HostCapacity == 10 && app.DialogCapacity == 8 &&
            app.DialogRegistrationCount == 2 && app.ControlHost.ActiveScopeHost == app.ControlHost);

        result &= Case(ref cases, app.RequestCloseForTests() && !app.IsDirty &&
            app.ActiveDialog == null && !app.ControlHost.IsModalActive);

        app.AdvancedCheckBox.SetChecked(true);
        app.ControlHost.TryFocus(1);
        app.View.TryFocusMember(app.DefaultsButton);
        app.StatusCheckBox.SetChecked(false);
        ManagedSettingsSnapshot beforeReset = app.Working;
        ManagedSettingsSnapshot appliedBeforeReset = app.Applied;
        int resetOffset = app.View.Offset;
        result &= Case(ref cases, app.IsDirty && app.OpenResetForTests() &&
            app.ActiveDialog != null && app.ActiveDialog.IsOpen &&
            app.ControlHost.IsModalActive && app.ControlHost.ActiveIndex == -1 &&
            app.ActiveDialog.ControlHost.ActiveControlId ==
                app.ActiveDialog.DefaultButtonId && app.View.Offset == resetOffset);
        result &= Case(ref cases, app.ActiveDialog.HandleInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab)) ==
                GuideXosControlHostResult.Traversed &&
            app.ActiveDialog.ControlHost.ActiveControlId == 1);
        result &= Case(ref cases, app.ActiveDialog.HandleInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab, true)) ==
                GuideXosControlHostResult.Traversed &&
            app.ActiveDialog.ControlHost.ActiveControlId ==
                app.ActiveDialog.DefaultButtonId);
        result &= Case(ref cases, app.ActiveDialog.HandleInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape)) ==
                GuideXosControlHostResult.Cancelled && app.ActiveDialog == null &&
            app.Working.Equals(beforeReset) && app.Applied.Equals(appliedBeforeReset) &&
            app.View.Offset == resetOffset && !app.ControlHost.IsModalActive &&
            app.ControlHost.ActiveControlId == 1);

        result &= Case(ref cases, app.OpenResetForTests() &&
            app.ActiveDialog.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Enter)) == GuideXosControlHostResult.Activated &&
            app.ActiveDialog == null);
        result &= Case(ref cases, app.Working.Equals(ManagedSettingsSnapshot.Defaults) &&
            app.IsDirty == !app.Applied.Equals(ManagedSettingsSnapshot.Defaults));
        result &= Case(ref cases, !app.AdvancedCheckBox.Checked &&
            app.DensityCombo.SelectedIndex == 0 && app.SpeedCombo.SelectedIndex == 1 &&
            app.View.Offset >= 0 && app.View.Offset <= app.View.MaximumOffset &&
            app.ScrollBar.Value == app.View.Offset);

        app.StatusCheckBox.SetChecked(false);
        app.ControlHost.TryFocus(7);
        ManagedSettingsSnapshot closeWorking = app.Working;
        ManagedSettingsSnapshot closeApplied = app.Applied;
        int closeOffset = app.View.Offset;
        result &= Case(ref cases, app.IsDirty && app.RequestCloseForTests() &&
            app.ActiveDialog != null && app.ActiveDialog.IsOpen &&
            app.ActiveDialog.RegistrationCount == 3 && app.ControlHost.ActiveIndex == -1);
        int offsetBeforeWheel = app.View.Offset;
        app.HandleInput(host, GuideXosInputEvent.ForWheel(320, 112, 1));
        result &= Case(ref cases, app.View.Offset == offsetBeforeWheel &&
            app.ActiveDialog != null && app.ActiveDialog.IsOpen);
        result &= Case(ref cases, app.ActiveDialog.HandleInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape)) ==
                GuideXosControlHostResult.Cancelled && app.ActiveDialog == null &&
            app.Working.Equals(closeWorking) && app.Applied.Equals(closeApplied) &&
            app.IsDirty && app.View.Offset == closeOffset &&
            !app.ControlHost.IsModalActive && app.ControlHost.ActiveControlId == 7);

        result &= Case(ref cases, app.RequestCloseForTests() &&
            app.ActiveDialog != null && app.ActiveDialog.HandleInput(
                GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                    GuideXosPointerButton.Primary, app.CloseDiscardButton.X + 4,
                    app.CloseDiscardButton.Y + 4)) == GuideXosControlHostResult.Activated &&
            app.ActiveDialog == null && app.IsDirty &&
            app.Applied.Equals(closeApplied) && !app.ControlHost.IsModalActive);

        app.InitializeForTests();
        app.StatusCheckBox.SetChecked(false);
        result &= Case(ref cases, app.IsDirty && app.RequestCloseForTests() &&
            app.ActiveDialog.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Enter)) == GuideXosControlHostResult.Activated &&
            app.ActiveDialog == null && !app.IsDirty &&
            app.Applied.Equals(app.Working) && !app.ControlHost.IsModalActive);

        bool relaunched = app.InitializeForTests() && app.RegistrationCount == 9 &&
            app.HostCapacity == 10 && app.ControlHost.ActiveScopeHost == app.ControlHost &&
            !app.ControlHost.HasTransientInputCapture &&
            !app.ControlHost.HasPointerDragCapture && app.View.Offset == 0;
        result &= Case(ref cases, relaunched);
        result &= Case(ref cases, app.RequestCloseForTests() &&
            app.ActiveDialog == null && !app.ControlHost.IsModalActive);

        bool popupInvalidation = app.InitializeForTests() &&
            app.ControlHost.TryFocus(3) == GuideXosControlHostResult.Focused &&
            app.DensityCombo.Open() == GuideXosComboBoxResult.Opened &&
            app.ControlHost.TryAcquireTransientInputCapture(3) &&
            app.OpenResetForTests() && !app.DensityCombo.IsOpen && !app.HasCapture &&
            app.ActiveDialog != null && app.ControlHost.IsModalActive &&
            app.ActiveDialog.Close(GuideXosDialogResult.Cancel) &&
            !app.ControlHost.IsModalActive && app.ControlHost.ActiveControlId == 3 &&
            app.DensityCombo.Open() == GuideXosComboBoxResult.Opened &&
            app.ControlHost.TryAcquireTransientInputCapture(3);
        app.DensityCombo.Close();
        app.ControlHost.RefreshVisibility();
        result &= Case(ref cases, popupInvalidation && !app.HasCapture &&
            !app.ControlHost.IsModalActive && app.RegistrationCount == 9 &&
            app.DialogRegistrationCount == 2);

        GuideXosControlHost reusedHost = app.ControlHost;
        bool stress = true;
        for (int index = 0; index < 25; index++)
        {
            stress &= app.InitializeForTests() &&
                ReferenceEquals(reusedHost, app.ControlHost);
            app.StatusCheckBox.SetChecked(false);
            stress &= app.OpenResetForTests();
            GuideXosInputEvent resetDecision = GuideXosInputEvent.ForKeyDown(
                (index & 1) == 0 ? GuideXosTextInputKey.Escape : GuideXosTextInputKey.Enter);
            GuideXosControlHostResult resetResult = app.ActiveDialog?.HandleInput(resetDecision) ??
                GuideXosControlHostResult.Rejected;
            stress &= resetResult == ((index & 1) == 0
                ? GuideXosControlHostResult.Cancelled
                : GuideXosControlHostResult.Activated) && app.ActiveDialog == null &&
                !app.ControlHost.IsModalActive && app.RegistrationCount == 9 &&
                app.DialogRegistrationCount == 2 && !app.HasCapture && !app.HasDragOwner &&
                app.View.Offset >= 0 && app.View.Offset <= app.View.MaximumOffset;
        }
        for (int index = 0; index < 25; index++)
        {
            stress &= app.InitializeForTests() &&
                ReferenceEquals(reusedHost, app.ControlHost);
            app.StatusCheckBox.SetChecked(false);
            stress &= app.RequestCloseForTests();
            if (app.ActiveDialog == null) { stress = false; break; }
            GuideXosControlHostResult closeResult;
            if (index % 3 == 0)
            {
                closeResult = app.ActiveDialog.HandleInput(
                    GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape));
            }
            else if (index % 3 == 1)
            {
                closeResult = app.ActiveDialog.HandleInput(
                    GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                        GuideXosPointerButton.Primary, app.CloseDiscardButton.X + 4,
                        app.CloseDiscardButton.Y + 4));
            }
            else
            {
                closeResult = app.ActiveDialog.HandleInput(
                    GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Enter));
            }
            stress &= (closeResult is GuideXosControlHostResult.Cancelled or
                    GuideXosControlHostResult.Activated) && app.ActiveDialog == null &&
                !app.ControlHost.IsModalActive && app.RegistrationCount == 9 &&
                app.DialogRegistrationCount == 2 && !app.HasCapture && !app.HasDragOwner;
        }
        stress &= app.InitializeForTests() && app.RegistrationCount == 9 &&
            !app.ControlHost.IsModalActive && !app.HasCapture && !app.HasDragOwner;
        result &= Case(ref cases, stress);

        host?.TryLog(result && cases == 18
            ? "C145-SETTINGS-TESTS cases=18 result=PASS"u8
            : "C145-SETTINGS-TESTS result=FAIL"u8);
        host?.TryLog(result
            ? "C145-SETTINGS-INTEGRATION reset=cancel-confirm dirtyClose=cancel-discard-apply cleanClose=direct relaunch=valid result=PASS"u8
            : "C145-SETTINGS-INTEGRATION result=FAIL"u8);
        return result && cases == 18;
    }

    private static bool Case(ref int count, bool value)
    {
        count++;
        return value;
    }
}
