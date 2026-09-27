using System;

namespace HostLogProof;

/// <summary>Bounded application-state, direct composition, and relaunch proofs for C144.</summary>
public static class GuideXosSettingsCenterC144Tests
{
    private static int _firstFailedCase;

    public static bool Run(GuideXosHost host,
        Applications.ManagedSettingsCenter app)
    {
        int stateCases = 0;
        int compositionCases = 0;
        int lifecycleCases = 0;
        ResetCaseDiagnostics();
        bool stateReset = app.InitializeForTests();
        bool state = stateReset && ApplicationState(app, ref stateCases);
        int stateFirstFailure = _firstFailedCase;
        ResetCaseDiagnostics();
        bool compositionReset = app.InitializeForTests();
        bool composition = compositionReset && Composition(app, ref compositionCases);
        int compositionFirstFailure = _firstFailedCase;
        ResetCaseDiagnostics();
        bool lifecycleReset = app.InitializeForTests();
        bool lifecycle = lifecycleReset && Lifecycle(app, ref lifecycleCases);
        int lifecycleFirstFailure = _firstFailedCase;
        LogSuiteDetail(host, "state", stateReset, stateCases, stateFirstFailure);
        LogSuiteDetail(host, "composition", compositionReset, compositionCases,
            compositionFirstFailure);
        LogSuiteDetail(host, "lifecycle", lifecycleReset, lifecycleCases,
            lifecycleFirstFailure);
        host?.TryLog(state && stateCases == 22
            ? "C144-STATE-TESTS cases=22 result=PASS"u8
            : "C144-STATE-TESTS result=FAIL"u8);
        host?.TryLog(composition && compositionCases == 21
            ? "C144-COMPOSITION-TESTS cases=21 result=PASS"u8
            : "C144-COMPOSITION-TESTS result=FAIL"u8);
        host?.TryLog(lifecycle && lifecycleCases == 13
            ? "C144-LIFECYCLE-TESTS cases=13 result=PASS"u8
            : "C144-LIFECYCLE-TESTS result=FAIL"u8);
        bool result = state && composition && lifecycle && stateCases == 22 &&
            compositionCases == 21 && lifecycleCases == 13;
        host?.TryLog(result
            ? "C144-TESTS state=22 composition=21 lifecycle=13 total=56 result=PASS"u8
            : "C144-TESTS result=FAIL"u8);
        return result;
    }

    private static bool ApplicationState(Applications.ManagedSettingsCenter app,
        ref int count)
    {
        bool result = true;
        result &= Case(ref count, app.Working.Equals(Applications.ManagedSettingsSnapshot.Defaults));
        result &= Case(ref count, app.Applied.Equals(Applications.ManagedSettingsSnapshot.Defaults));
        result &= Case(ref count, !app.IsDirty);
        app.StatusCheckBox.SetChecked(false);
        result &= Case(ref count, !app.Working.ShowStatus && app.Applied.ShowStatus);
        result &= Case(ref count, app.IsDirty);
        app.StatusCheckBox.SetChecked(false);
        result &= Case(ref count, app.IsDirty && !app.Working.ShowStatus);
        app.DensityCombo.TrySetSelectedIndex(1);
        result &= Case(ref count, app.Working.Density == 1 && app.Applied.Density == 0);
        app.SpeedCombo.TrySetSelectedIndex(2);
        result &= Case(ref count, app.Working.ScrollSpeed == 2);
        app.StatusCombo.TrySetSelectedIndex(1);
        result &= Case(ref count, app.Working.ReportFormat == 1);
        app.NaturalWheel.TrySelect();
        result &= Case(ref count, app.WheelGroup.SelectedMember == app.NaturalWheel && app.Working.NaturalScroll);
        app.DetailMode.TrySelect();
        result &= Case(ref count, app.StatusGroup.SelectedMember == app.DetailMode && app.Working.StatusDetail == 1);
        result &= Case(ref count, app.WheelGroup.SelectedMember == app.NaturalWheel && app.StatusGroup.SelectedMember == app.DetailMode);
        app.InputEnableCheckBox.SetChecked(false);
        result &= Case(ref count, !app.Working.InputEnabled && !app.Group(1).Enabled);
        app.AdvancedCheckBox.SetChecked(true);
        app.KeyboardTips.SetChecked(false);
        result &= Case(ref count, app.Working.ShowAdvanced && !app.Working.ShowKeyboardTips);
        app.ApplyWorking();
        result &= Case(ref count, app.Applied.Equals(app.Working));
        result &= Case(ref count, !app.IsDirty && !app.ApplyButton.Enabled);
        app.StatusCheckBox.SetChecked(true);
        result &= Case(ref count, app.IsDirty && !app.Applied.ShowStatus);
        int stableOffset = 80;
        app.View.SetOffset(stableOffset);
        app.RestoreDefaults();
        result &= Case(ref count, app.Working.Equals(Applications.ManagedSettingsSnapshot.Defaults));
        result &= Case(ref count, app.Working.ShowStatus && app.Working.InputEnabled &&
            !app.Working.ShowAdvanced && app.Working.Density == 0 && app.Working.ScrollSpeed == 1);
        result &= Case(ref count, app.View.Offset == stableOffset && app.IsDirty);
        result &= Case(ref count, app.DensityCombo.SelectedIndex == 0 &&
            app.SpeedCombo.SelectedIndex == 1 && app.StatusCombo.SelectedIndex == 0 &&
            app.WheelGroup.SelectedMember != app.NaturalWheel && app.StatusGroup.SelectedMember != app.DetailMode);
        result &= Case(ref count, StressInteractionSequence(app));
        return result && count == 22;
    }

    private static bool Composition(Applications.ManagedSettingsCenter app,
        ref int count)
    {
        bool result = true;
        result &= Case(ref count, app.SectionGroupCount == 4 && app.LeafControlCount == 17);
        result &= Case(ref count, app.View.MemberCount == 21 && app.View.MaximumMemberCount == 24);
        result &= Case(ref count, app.RegistrationCount == 9 && app.ControlHost.MaximumControlCount == 10);
        result &= Case(ref count, app.Stack(0).MemberCount == 3 && app.Stack(1).MemberCount == 5 &&
            app.Stack(2).MemberCount == 6 && app.Stack(3).MemberCount == 3);
        result &= Case(ref count, app.Group(0).MemberCount == 3 && app.Group(1).MemberCount == 5 &&
            app.Group(2).MemberCount == 6 && app.Group(3).MemberCount == 3);
        result &= Case(ref count, app.Group(0).GetMember(0) == app.Leaf(0, 0) &&
            app.Group(1).GetMember(0) == app.Leaf(1, 0));
        result &= Case(ref count, app.DensityCombo.ParentGroupBox == app.Group(0) &&
            app.SpeedCombo.ParentGroupBox == app.Group(1) &&
            app.View.ContainsMember(app.Group(0)) && app.View.ContainsMember(app.Group(3)));

        int initialExtent = app.View.ContentExtent;
        app.AdvancedCheckBox.SetChecked(true);
        int expandedExtent = app.View.ContentExtent;
        result &= Case(ref count, expandedExtent > initialExtent && app.ScrollBar.Maximum == app.View.MaximumOffset);
        result &= Case(ref count, app.Group(3).Visible && app.Group(1).Visible && app.Group(1).Enabled);
        app.View.SetOffset(app.View.MaximumOffset);
        app.AdvancedCheckBox.SetChecked(false);
        result &= Case(ref count, app.View.ContentExtent == initialExtent && app.View.Offset <= app.View.MaximumOffset);
        result &= Case(ref count, app.ScrollBar.Maximum == app.View.MaximumOffset && app.ScrollBar.Value == app.View.Offset);
        app.AdvancedCheckBox.SetChecked(true);
        result &= Case(ref count, app.View.ContentExtent == expandedExtent && app.View.Offset <= app.View.MaximumOffset);
        result &= Case(ref count, app.View.TryFocusMember(app.DefaultsButton) && app.View.HasFocus && app.View.Offset > 0);
        GuideXosButton lower = app.DefaultsButton;
        int screenY = lower.Y - app.View.Offset + lower.Height / 2;
        result &= Case(ref count, app.View.TryHitTest(lower.X + lower.Width / 2, screenY, out object hit) &&
            ReferenceEquals(hit, lower));
        result &= Case(ref count, app.WheelGroup.SelectedMember == app.StandardWheel &&
            app.StatusGroup.SelectedMember == app.SummaryMode);
        result &= Case(ref count, app.Group(0).TryGetContentRectangle(out _, out _, out int contentWidth, out _) &&
            app.Stack(0).Width == contentWidth && app.Group(0).ValidateMembershipGeometry(out int outside) && outside == 0);
        int screenX = app.DensityCombo.X + 8;
        int translatedScreenY = app.DensityCombo.Y + app.DensityCombo.Height / 2;
        int translatedOffset = app.StandardWheel.Y - app.DensityCombo.Y;
        bool topHit = app.View.SetOffset(0) &&
            app.View.TryHitTest(screenX, translatedScreenY, out object topMember) &&
            ReferenceEquals(topMember, app.DensityCombo);
        bool translatedHit = translatedOffset > 0 && app.View.SetOffset(translatedOffset) &&
            app.View.TryHitTest(screenX, translatedScreenY, out object translatedMember) &&
            ReferenceEquals(translatedMember, app.StandardWheel);
        result &= Case(ref count, topHit && translatedHit &&
            app.View.Offset >= 0 && app.View.Offset <= app.View.MaximumOffset &&
            app.ScrollBar.Value == app.View.Offset);

        GuideXosScrollView legacy = new(20, 20, 240, 80);
        bool oldEight = true;
        for (int index = 0; index < 8; index++)
            oldEight &= legacy.TryAddMember(new GuideXosLabel(0, 0, 80, "old"), 0, index * 18) == GuideXosScrollViewResult.Added;
        result &= Case(ref count, oldEight && legacy.MaximumMemberCount == 8 &&
            legacy.TryAddMember(new GuideXosLabel(0, 0, 80, "ninth"), 0, 144) == GuideXosScrollViewResult.CapacityReached);
        result &= Case(ref count, CapacityBoundaries(23) && CapacityBoundaries(24));
        result &= Case(ref count, HostCapacityBoundary(9) && HostCapacityBoundary(10) &&
            app.RegistrationCount == 9 && app.ControlHost.MaximumControlCount == 10);
        result &= Case(ref count, app.View.MemberCount == 21 && app.RegistrationCount == 9);
        return result && count == 21;
    }

    private static bool CapacityBoundaries(int capacity)
    {
        GuideXosScrollView view = new(20, 20, 240, 80, capacity);
        for (int index = 0; index < capacity - 1; index++)
        {
            if (view.TryAddMember(new GuideXosLabel(0, 0, 80, "bounded"), 0,
                    index * 18) != GuideXosScrollViewResult.Added) return false;
        }
        if (view.MemberCount != capacity - 1 || view.MaximumMemberCount != capacity ||
            view.TryAddMember(new GuideXosLabel(0, 0, 80, "at-capacity"), 0,
                (capacity - 1) * 18) != GuideXosScrollViewResult.Added) return false;
        return view.MemberCount == capacity &&
            view.TryAddMember(new GuideXosLabel(0, 0, 80, "overflow"), 0,
                capacity * 18) == GuideXosScrollViewResult.CapacityReached;
    }

    private static bool HostCapacityBoundary(int capacity)
    {
        GuideXosControlHost host = new(capacity);
        for (int index = 0; index < capacity - 1; index++)
        {
            if (host.TryRegisterButton(index + 1,
                    new GuideXosButton(10, 20, 64, 24, "B")) !=
                GuideXosControlHostResult.Registered) return false;
        }
        if (host.RegistrationCount != capacity - 1 ||
            host.TryRegisterButton(capacity,
                new GuideXosButton(10, 20, 64, 24, "B")) != GuideXosControlHostResult.Registered)
            return false;
        return host.RegistrationCount == capacity &&
            host.TryRegisterButton(capacity + 1,
                new GuideXosButton(10, 20, 64, 24, "B")) == GuideXosControlHostResult.Rejected &&
            host.RegistrationCount == capacity;
    }

    private static bool StressInteractionSequence(Applications.ManagedSettingsCenter app)
    {
        for (int index = 0; index < 20; index++)
            app.AdvancedCheckBox.SetChecked((index & 1) == 0);

        GuideXosComboBox[] combos = { app.DensityCombo, app.SpeedCombo, app.StatusCombo };
        int[] ids = { 3, 4, 5 };
        for (int index = 0; index < 10; index++)
        {
            GuideXosComboBox combo = combos[index % combos.Length];
            int id = ids[index % ids.Length];
            if (app.ControlHost.TryFocus(id) != GuideXosControlHostResult.Focused ||
                combo.Open() != GuideXosComboBoxResult.Opened ||
                !app.ControlHost.TryAcquireTransientInputCapture(id)) return false;
            if ((index & 1) == 0)
            {
                combo.HandleKey(GuideXosTextInputKey.Down);
                combo.HandleKey(GuideXosTextInputKey.Enter);
            }
            else combo.Close();
            app.ControlHost.RefreshVisibility();
            if (app.HasCapture) return false;
        }

        for (int index = 0; index < 10; index++)
        {
            if ((index & 1) == 0) app.NaturalWheel.TrySelect();
            else app.StandardWheel.TrySelect();
        }
        for (int index = 0; index < 20; index++)
            app.ControlHost.HandleWheel(1, app.View.X + 30, app.View.Y + 40, -1);
        for (int index = 0; index < 4; index++)
            app.ScrollBar.Value = index & 1;
        for (int index = 0; index < 4; index++)
        {
            app.RestoreDefaults();
            app.ApplyWorking();
        }
        return app.RegistrationCount == 9 && app.View.MemberCount == 21 &&
            app.View.Offset >= 0 && app.View.Offset <= app.View.MaximumOffset &&
            app.ScrollBar.Value == app.View.Offset && !app.HasCapture && !app.HasDragOwner &&
            !app.IsDirty;
    }

    private static bool Lifecycle(Applications.ManagedSettingsCenter app,
        ref int count)
    {
        bool result = true;
        GuideXosCheckBox member = app.KeyboardTips;
        member.SetEnabled(false);
        app.InputEnableCheckBox.SetChecked(false);
        result &= Case(ref count, !member.EffectiveEnabled && !app.Group(1).Enabled);
        app.InputEnableCheckBox.SetChecked(true);
        result &= Case(ref count, app.Group(1).Enabled && !member.Enabled && !member.EffectiveEnabled);
        member.SetEnabled(true);
        app.InputEnableCheckBox.SetChecked(false);
        app.InputEnableCheckBox.SetChecked(true);
        result &= Case(ref count, member.Enabled && member.EffectiveEnabled);
        app.AdvancedCheckBox.SetChecked(true);
        result &= Case(ref count, app.Group(3).Visible && app.View.ContentExtent > app.InitialContentHeight);
        int thumbX = app.ScrollBar.X + 2;
        int thumbY = app.ScrollBar.ThumbTop + Math.Max(1, app.ScrollBar.ThumbHeight / 2);
        GuideXosControlHostResult dragStart = app.ControlHost.FocusAndRoutePointer(
            2, thumbX, thumbY);
        GuideXosControlHostResult dragEnd = app.ControlHost.HandlePointerUp(
            thumbX, thumbY);
        result &= Case(ref count, dragStart == GuideXosControlHostResult.DragStarted &&
            dragEnd == GuideXosControlHostResult.DragEnded && !app.HasDragOwner &&
            app.ControlHost.TryFocus(2) == GuideXosControlHostResult.Rejected);
        bool comboFocused = app.ControlHost.TryFocus(4) == GuideXosControlHostResult.Focused;
        bool comboOpened = comboFocused && app.SpeedCombo.Open() == GuideXosComboBoxResult.Opened;
        bool capture = comboOpened && app.ControlHost.TryAcquireTransientInputCapture(4);
        app.InputEnableCheckBox.SetChecked(false);
        result &= Case(ref count, capture && !app.SpeedCombo.IsOpen && !app.HasCapture);
        result &= Case(ref count, app.DensityCombo.IsOpen == false && app.StatusCombo.IsOpen == false);
        app.InputEnableCheckBox.SetChecked(true);
        app.View.TryFocusMember(app.DefaultsButton);
        app.AdvancedCheckBox.SetChecked(false);
        result &= Case(ref count, !app.View.HasFocus || app.View.FocusedMember != app.DefaultsButton);
        int registrations = app.RegistrationCount;
        int viewMembers = app.View.MemberCount;
        result &= Case(ref count, app.InitializeForTests() && app.RegistrationCount == registrations &&
            app.View.MemberCount == viewMembers);
        result &= Case(ref count, app.Working.Equals(Applications.ManagedSettingsSnapshot.Defaults) &&
            app.Applied.Equals(Applications.ManagedSettingsSnapshot.Defaults) && !app.IsDirty);
        result &= Case(ref count, app.View.Offset == 0 && app.ScrollBar.Value == 0);
        result &= Case(ref count, !app.PopupOpen && !app.HasCapture && !app.HasDragOwner);
        result &= Case(ref count, app.Group(0).ValidateMembershipGeometry(out int outside) && outside == 0 &&
            app.RegistrationCount == 9 && app.View.MemberCount == 21);
        return result && count == 13;
    }

    private static bool Case(ref int count, bool passed)
    {
        ++count;
        if (!passed && _firstFailedCase == 0) _firstFailedCase = count;
        return passed;
    }

    private static void ResetCaseDiagnostics()
    {
        _firstFailedCase = 0;
    }

    private static void LogSuiteDetail(GuideXosHost host, string suite,
        bool reset, int count, int firstFailure)
    {
        Span<byte> line = stackalloc byte[96];
        int position = 0;
        GuideXosText.Append(line, ref position, "C144-TEST-DIAG suite="u8);
        for (int index = 0; index < suite.Length; index++)
            line[position++] = (byte)suite[index];
        GuideXosText.Append(line, ref position, reset ? " reset=PASS"u8 : " reset=FAIL"u8);
        GuideXosText.Append(line, ref position, " cases="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)count);
        GuideXosText.Append(line, ref position, " firstFail="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)firstFailure);
        host?.TryLog(line[..position]);
    }
}
