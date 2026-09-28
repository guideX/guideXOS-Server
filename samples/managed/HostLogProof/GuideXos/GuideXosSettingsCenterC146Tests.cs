using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>Working/applied/persisted and persistence-error modal integration.</summary>
public static class GuideXosSettingsCenterC146Tests
{
    public static bool Run(GuideXosHost host, ManagedSettingsCenter app)
    {
        int cases = 0;
        ManagedSettingsSnapshot defaults = ManagedSettingsSnapshot.Defaults;
        bool result = Case(ref cases, app.Working.Equals(defaults) &&
            app.Applied.Equals(defaults) && app.Persisted.Equals(defaults) &&
            !app.IsDirty && app.View.Offset == 0 && !app.PersistedFilePresent);

        app.StatusCheckBox.SetChecked(false);
        app.NaturalWheel.SetChecked(true);
        app.DensityCombo.TrySetSelectedIndex(1);
        app.ScrollLinesCombo.TrySetSelectedIndex(4);
        app.AdvancedCheckBox.SetChecked(true);
        ManagedSettingsSnapshot edited = app.Working;
        result &= Case(ref cases, app.IsDirty && edited.ShowStatus == false &&
            edited.NaturalScroll && edited.Density == 1 && edited.ShowAdvanced &&
            edited.ScrollLinesPerNotch == 5 &&
            app.ScrollLinesCombo.SelectedIndex == 4 &&
            app.Applied.ScrollLinesPerNotch == 3 &&
            app.Persisted.ScrollLinesPerNotch == 3 &&
            GuideXosRuntimeSettings.Current.ScrollLinesPerNotch == 3 &&
            app.Applied.Equals(defaults) && app.Persisted.Equals(defaults));

        ManagedSettingsSnapshot appliedBeforeReset = app.Applied;
        ManagedSettingsSnapshot persistedBeforeReset = app.Persisted;
        bool reset = app.OpenResetForTests() && app.ActiveDialog != null &&
            app.ActiveDialog.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Enter)) == GuideXosControlHostResult.Activated;
        result &= Case(ref cases, reset && app.Working.Equals(defaults) &&
            app.ScrollLinesCombo.SelectedIndex == 2 &&
            GuideXosRuntimeSettings.Current.ScrollLinesPerNotch == 3 &&
            app.Applied.Equals(appliedBeforeReset) &&
            app.Persisted.Equals(persistedBeforeReset) &&
            app.IsDirty == !defaults.Equals(appliedBeforeReset));

        app.StatusCheckBox.SetChecked(false);
        app.ScrollLinesCombo.TrySetSelectedIndex(4);
        ManagedSettingsSnapshot closeWorking = app.Working;
        ManagedSettingsSnapshot closeApplied = app.Applied;
        ManagedSettingsSnapshot closePersisted = app.Persisted;
        bool cancel = app.RequestCloseForTests() && app.ActiveDialog != null &&
            app.ActiveDialog.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Escape)) == GuideXosControlHostResult.Cancelled;
        result &= Case(ref cases, cancel && app.Working.Equals(closeWorking) &&
            app.Applied.Equals(closeApplied) && app.Persisted.Equals(closePersisted) &&
            app.IsDirty && !app.ControlHost.IsModalActive);

        bool discard = app.RequestCloseForTests() && app.ActiveDialog != null &&
            app.ActiveDialog.HandleInput(GuideXosInputEvent.ForPointer(
                GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary,
                app.CloseDiscardButton.X + 4, app.CloseDiscardButton.Y + 4)) ==
                GuideXosControlHostResult.Activated;
        result &= Case(ref cases, discard && app.Working.Equals(closeWorking) &&
            app.Applied.Equals(closeApplied) && app.Persisted.Equals(closePersisted) &&
            app.IsDirty && !app.ControlHost.IsModalActive);

        app.StatusCheckBox.SetChecked(false);
        app.ScrollLinesCombo.TrySetSelectedIndex(4);
        ManagedSettingsSnapshot failureApplied = app.Applied;
        ManagedSettingsSnapshot failurePersisted = app.Persisted;
        app.InjectNextSaveFailureForTests();
        bool failedApply = app.RequestCloseForTests() && app.ActiveDialog != null &&
            app.ActiveDialog.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Enter)) == GuideXosControlHostResult.Activated;
        bool errorModal = failedApply && app.ActiveDialog == app.PersistenceDialog &&
            app.PersistenceDialog.IsOpen && app.ControlHost.IsModalActive &&
            app.IsDirty && app.Applied.Equals(failureApplied) &&
            app.Persisted.Equals(failurePersisted) &&
            app.Working.ScrollLinesPerNotch == 5 &&
            GuideXosRuntimeSettings.Current.ScrollLinesPerNotch ==
                failureApplied.ScrollLinesPerNotch && !app.SurfaceClosingForTests;
        result &= Case(ref cases, errorModal);

        bool dismissed = app.PersistenceDialog.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Enter)) ==
                GuideXosControlHostResult.Activated && app.ActiveDialog == null &&
            !app.ControlHost.IsModalActive && app.IsDirty &&
            app.Applied.Equals(failureApplied) && app.Persisted.Equals(failurePersisted);
        result &= Case(ref cases, dismissed);

        bool retry = app.RequestCloseForTests() && app.ActiveDialog != null &&
            app.ActiveDialog.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Enter)) == GuideXosControlHostResult.Activated &&
            !app.IsDirty && app.Working.Equals(app.Applied) &&
            app.Applied.Equals(app.Persisted) && !app.ControlHost.IsModalActive;
        result &= Case(ref cases, retry);

        bool noOpApply = app.ApplyForTests() && !app.IsDirty &&
            app.Working.Equals(app.Applied) && app.Applied.Equals(app.Persisted);
        result &= Case(ref cases, noOpApply);

        app.StatusCheckBox.SetChecked(false);
        ManagedSettingsSnapshot persistedBeforeDefaults = app.Persisted;
        bool resetForApply = app.OpenResetForTests() && app.ActiveDialog != null &&
            app.ActiveDialog.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Enter)) == GuideXosControlHostResult.Activated &&
            app.Working.Equals(defaults) && app.IsDirty &&
            app.Persisted.Equals(persistedBeforeDefaults);
        bool defaultsApplied = resetForApply && app.ApplyForTests() &&
            app.Working.Equals(defaults) && app.Applied.Equals(defaults) &&
            app.Persisted.Equals(defaults) && !app.IsDirty;
        result &= Case(ref cases, defaultsApplied);

        bool clean = app.RegistrationCount == 10 &&
            app.View.MemberCount == 22 && app.ScrollViewMemberCount == 22 &&
            app.InputGroupBoxMemberCount == 6 &&
            app.View.Offset == 0 && !app.ControlHost.IsModalActive &&
            !app.HasCapture && !app.HasDragOwner &&
            app.Working.Equals(ManagedSettingsSnapshot.Defaults) &&
            app.ScrollLinesCombo.SelectedIndex == 2 && !app.IsDirty;
        result &= Case(ref cases, clean);

        host?.TryLog(result && cases == 11
            ? "C146-SETTINGS-TESTS cases=11 result=PASS"u8
            : "C146-SETTINGS-TESTS result=FAIL"u8);
        return result && cases == 11;
    }

    private static bool Case(ref int count, bool value)
    {
        count++;
        return value;
    }
}
