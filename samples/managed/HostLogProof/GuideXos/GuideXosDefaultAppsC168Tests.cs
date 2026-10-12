#if HOSTLOGPROOF_C167_DEFAULT_APPS_PROOF
using System;
using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>Visual layout, focus, feedback, and bounded lifecycle coverage for C168.</summary>
public static unsafe class GuideXosDefaultAppsC168Tests
{
    private const string NotesId = "com.guidexos.apps.managed.notes";
    private static int _firstFailedCase;

    public static int CasesExecuted { get; private set; }
    public static int FirstFailedCase => _firstFailedCase;
    public static string FailureDetail { get; private set; } = "";

    public static bool Run(GuideXosHost host, ManagedSettingsCenter app)
    {
        int cases = 0;
        bool passed = true;
        _firstFailedCase = 0;
        CasesExecuted = 0;
        FailureDetail = "";
        FakeAssociationService service = new();
        ManagedDefaultAppsPageC167 page = app.DefaultAppsPage;

        passed &= Case(ref cases, Start(app, service,
            GuideXosAssociationOverrideState.NoOverride) &&
            page.ControlCount == 8 && app.ScrollViewMemberCount == 8 &&
            app.View.Width == 504 && app.View.Height == 200 &&
            app.View.MaximumMemberCount == 24 && app.View.MaximumOffset == 0 &&
            !app.ScrollBar.Visible && page.Group.MemberCount == 8 &&
            page.Stack.MemberCount == 8 && !app.View.ContainsMember(page.Group));

        bool heading = app.CurrentPageHeading.SequenceEqual(
            "Apps / Default Apps  |  .txt"u8);
        bool settingsHeading = app.ReturnToSettingsForTests() &&
            app.CurrentPageHeading.SequenceEqual("Settings"u8);
        bool appsHeading = app.EnterDefaultAppsForTests(service) &&
            app.CurrentPageHeading.SequenceEqual("Apps / Default Apps  |  .txt"u8);
        passed &= Case(ref cases, heading && settingsHeading && appsHeading);

        passed &= Case(ref cases, Start(app, service,
            GuideXosAssociationOverrideState.NoOverride) &&
            page.CurrentText == "Effective application: Managed Notes" &&
            page.OverrideText == "Association state: System default" &&
            page.FeedbackText == "Pending: none");

        passed &= Case(ref cases, Start(app, service,
            GuideXosAssociationOverrideState.ApplicationOverride) &&
            page.CurrentText == "Effective application: Managed Notes" &&
            page.OverrideText == "Association state: Explicitly selected" &&
            page.FeedbackText == "Pending: none");

        passed &= Case(ref cases, Start(app, service,
            GuideXosAssociationOverrideState.Disabled) &&
            page.CurrentText == "Effective application: None" &&
            page.OverrideText == "Association state: Disabled" &&
            page.Choice.SelectedIndex == 1);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        page.Choice.TrySetSelectedIndex(1);
        passed &= Case(ref cases, page.HasPendingChange && page.CanApply &&
            page.FeedbackText == "Pending: No default application" &&
            page.CurrentText == "Effective application: Managed Notes" &&
            page.OverrideText == "Association state: System default");

        passed &= Case(ref cases, service.MutationCount == 0 &&
            service.State == GuideXosAssociationOverrideState.NoOverride &&
            page.PendingChoice == 1);

        passed &= Case(ref cases, page.Apply() &&
            page.CommittedState == GuideXosAssociationOverrideState.Disabled &&
            !page.HasPendingChange && !page.ApplyButton.Enabled);

        passed &= Case(ref cases, page.FeedbackText ==
            "No default application selected." &&
            page.OverrideText == "Association state: Disabled");

        passed &= Case(ref cases, service.QueryCount == 2 &&
            service.LastOperation == "Query" &&
            page.HasCommittedState &&
            page.CommittedState == GuideXosAssociationOverrideState.Disabled);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        page.Choice.TrySetSelectedIndex(1);
        service.FailNextMutation = true;
        passed &= Case(ref cases, !page.Apply() &&
            page.CommittedState == GuideXosAssociationOverrideState.NoOverride &&
            page.CurrentText == "Effective application: Managed Notes" &&
            page.HasPendingChange && page.FeedbackText ==
                "Could not save default. Pending: No default app");

        Start(app, service, GuideXosAssociationOverrideState.Disabled);
        passed &= Case(ref cases, page.ResetToSystemDefault() &&
            service.LastMutation == "Reset" &&
            page.CommittedState == GuideXosAssociationOverrideState.NoOverride &&
            page.CurrentText == "Effective application: Managed Notes" &&
            page.OverrideText == "Association state: System default" &&
            page.FeedbackText == "System default restored.");

        passed &= Case(ref cases, service.QueryCount == 2 &&
            service.LastOperation == "Query");

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        service.SetExternalState(GuideXosAssociationOverrideState.Disabled);
        app.ReturnToSettingsForTests();
        int queriesBeforeEntry = service.QueryCount;
        passed &= Case(ref cases, app.EnterDefaultAppsForTests(service) &&
            service.QueryCount == queriesBeforeEntry + 1 &&
            page.CommittedState == GuideXosAssociationOverrideState.Disabled &&
            page.Choice.SelectedIndex == 1);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        page.Choice.TrySetSelectedIndex(1);
        int mutationsBeforeNavigation = service.MutationCount;
        bool returned = app.ReturnToSettingsForTests();
        passed &= Case(ref cases, returned && service.MutationCount ==
            mutationsBeforeNavigation &&
            service.State == GuideXosAssociationOverrideState.NoOverride &&
            app.CurrentPageHeading.SequenceEqual("Settings"u8));

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        GuideXosControlHost controlHost = app.ControlHost;
        GuideXosControlHostResult focusStart = controlHost.TryFocus(11);
        GuideXosControlHostResult tabFirst = controlHost.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab));
        bool tab = focusStart == GuideXosControlHostResult.Focused &&
            tabFirst == GuideXosControlHostResult.Traversed &&
            controlHost.ActiveControlId == 1 && app.View.HasFocus &&
            ReferenceEquals(app.View.FocusedMember, page.Choice);
        GuideXosControlHostResult tabSecond = controlHost.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab));
        tab &= tabSecond == GuideXosControlHostResult.Traversed &&
            ReferenceEquals(app.View.FocusedMember, page.ApplyButton);
        GuideXosControlHostResult tabThird = controlHost.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab));
        tab &= tabThird == GuideXosControlHostResult.Traversed &&
            ReferenceEquals(app.View.FocusedMember, page.ResetButton);
        if (!tab) FailureDetail = $"focus={focusStart} tab={tabFirst}/{tabSecond}/{tabThird} active={controlHost.ActiveControlId} viewFocus={app.View.HasFocus} member={app.View.FocusedMember?.GetType().Name}";
        passed &= Case(ref cases, tab);

        bool reverse = controlHost.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Tab, true)) == GuideXosControlHostResult.Traversed &&
            ReferenceEquals(app.View.FocusedMember, page.ApplyButton);
        reverse &= controlHost.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Tab, true)) == GuideXosControlHostResult.Traversed &&
            ReferenceEquals(app.View.FocusedMember, page.Choice);
        passed &= Case(ref cases, reverse);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        GuideXosComboBox choice = page.Choice;
        bool pointerOpen = choice.HandlePointerDown(choice.X + 4,
            choice.Y + 4) == GuideXosComboBoxResult.Opened;
        bool pointerSelect = choice.HandlePointerDown(choice.X + 4,
            choice.PopupY + GuideXosComboBox.PopupRowHeight + 4) ==
                GuideXosComboBoxResult.SelectionChanged;
        passed &= Case(ref cases, pointerOpen && pointerSelect &&
            page.PendingChoice == 1 && page.HasPendingChange &&
            service.MutationCount == 0);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        choice = page.Choice;
        bool keyboard = app.ControlHost.TryFocus(1) ==
            GuideXosControlHostResult.Focused &&
            choice.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosComboBoxResult.Opened &&
            choice.HandleKey(GuideXosTextInputKey.Down) ==
                GuideXosComboBoxResult.Moved &&
            choice.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosComboBoxResult.SelectionChanged;
        passed &= Case(ref cases, keyboard && page.PendingChoice == 1 &&
            service.MutationCount == 0);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        choice = page.Choice;
        controlHost = app.ControlHost;
        bool escape = controlHost.TryFocus(1) ==
                GuideXosControlHostResult.Focused &&
            choice.Open() == GuideXosComboBoxResult.Opened &&
            controlHost.TryAcquireTransientInputCapture(12);
        controlHost.HandleInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Escape));
        passed &= Case(ref cases, escape && !choice.IsOpen &&
            !controlHost.HasTransientInputCapture &&
            !controlHost.HasPointerDragCapture && choice.SelectedIndex == 0 &&
            service.MutationCount == 0);

        passed &= Case(ref cases, choice.PopupY +
            choice.VisibleRowCount * GuideXosComboBox.PopupRowHeight <=
                app.View.Y + app.View.Height - GuideXosScrollView.BorderPixels &&
            choice.X + choice.Width <=
                app.View.X + app.View.Width - GuideXosScrollView.BorderPixels);

        passed &= Case(ref cases, app.View.MaximumOffset == 0 &&
            app.View.Offset == 0 && !app.ScrollBar.Visible &&
            !app.View.SetOffset(1) && app.View.Offset == 0);

        passed &= Case(ref cases, app.RegistrationCount == 12 &&
            app.HostCapacity == 12 && app.ScrollViewMemberCount == 8 &&
            app.View.MaximumMemberCount == 24 && page.ControlCount == 8 &&
            page.Group.MemberCount == GuideXosGroupBox.MaximumSupportedMemberCount);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        controlHost = app.ControlHost;
        bool singleFocus = controlHost.TryFocus(1) ==
                GuideXosControlHostResult.Focused &&
            controlHost.ActiveControlId == 1 && app.View.HasFocus &&
            ReferenceEquals(app.View.FocusedMember, page.Choice) &&
            page.Choice.IsFocused && !page.ApplyButton.IsFocused &&
            !page.ResetButton.IsFocused &&
            controlHost.TryFocus(12) == GuideXosControlHostResult.Rejected &&
            controlHost.ActiveControlId == 1 && app.View.HasFocus;
        passed &= Case(ref cases, singleFocus);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        page.Choice.TrySetSelectedIndex(1);
        int mutationCountBeforeClose = service.MutationCount;
        bool closed = app.CloseDefaultAppsForTests() &&
            service.MutationCount == mutationCountBeforeClose &&
            !app.HasCapture && !app.HasDragOwner;
        bool reopened = app.InitializeForTests() &&
            app.EnterDefaultAppsForTests(service) &&
            page.CommittedState == GuideXosAssociationOverrideState.NoOverride &&
            page.Choice.SelectedIndex == 0 && !page.HasPendingChange &&
            app.RegistrationCount == 12;
        passed &= Case(ref cases, closed && reopened);

        passed &= Case(ref cases, ManagedSettingsStore.FormatVersion == 2 &&
            ManagedSettingsStore.EncodedFileBytes == 26);

        NativeHostCallTable table = default;
        NativeHostCallTable* tablePointer = &table;
        passed &= Case(ref cases, sizeof(NativeHostCallTable) == 128 &&
            sizeof(NativeAssociationServiceResponse) == 328 &&
            (byte*)&tablePointer->associationService - (byte*)tablePointer == 120);

        bool pageCycles = true;
        for (int index = 0; index < 250; index++)
        {
            pageCycles &= app.InitializeForTests() &&
                app.EnterDefaultAppsForTests(service) &&
                app.ReturnToSettingsForTests() &&
                !app.HasCapture && !app.HasDragOwner &&
                app.RegistrationCount == 12;
        }
        passed &= Case(ref cases, pageCycles);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        bool selectionStress = true;
        for (int index = 0; index < 250; index++)
        {
            int next = index % 2 == 0 ? 1 : 0;
            selectionStress &= page.Choice.TrySetSelectedIndex(next) &&
                page.PendingChoice == next &&
                page.HasPendingChange == (next == 1) &&
                page.CurrentText == "Effective application: Managed Notes";
        }
        passed &= Case(ref cases, selectionStress && service.MutationCount == 0 &&
            service.State == GuideXosAssociationOverrideState.NoOverride);

        bool applyResetStress = true;
        for (int index = 0; index < 100; index++)
        {
            int next = index % 2;
            page.Choice.TrySetSelectedIndex(next);
            applyResetStress &= page.Apply() &&
                page.CommittedState == (next == 0
                    ? GuideXosAssociationOverrideState.ApplicationOverride
                    : GuideXosAssociationOverrideState.Disabled) &&
                page.ResetToSystemDefault() &&
                page.CommittedState == GuideXosAssociationOverrideState.NoOverride &&
                !page.HasPendingChange;
        }
        passed &= Case(ref cases, applyResetStress &&
            service.MutationCount == 200 &&
            page.OverrideText == "Association state: System default");

        controlHost = app.ControlHost;
        bool popupStress = controlHost.TryFocus(1) ==
            GuideXosControlHostResult.Focused;
        for (int index = 0; popupStress && index < 250; index++)
        {
            popupStress &= page.Choice.Open() == GuideXosComboBoxResult.Opened &&
                controlHost.TryAcquireTransientInputCapture(12);
            controlHost.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Escape));
            popupStress &= !page.Choice.IsOpen &&
                !controlHost.HasTransientInputCapture &&
                !controlHost.HasPointerDragCapture;
        }
        passed &= Case(ref cases, popupStress && service.MutationCount == 200);

        host?.TryLog(passed && cases == 31
            ? "C168-FOCUSED-TESTS cases=31 pageCycles=250 selectionChanges=250 applyResetCycles=100 popupCycles=250 result=PASS"u8
            : "C168-FOCUSED-TESTS result=FAIL"u8);
        if (!passed || cases != 31)
        {
            Span<byte> line = stackalloc byte[64];
            int length = 0;
            GuideXosText.Append(line, ref length, "C168-TEST-FAIL first="u8);
            GuideXosText.AppendUnsigned(line, ref length,
                (uint)_firstFailedCase);
            GuideXosText.Append(line, ref length, " cases="u8);
            GuideXosText.AppendUnsigned(line, ref length, (uint)cases);
            host?.TryLog(line[..length]);
        }
        CasesExecuted = cases;
        return passed && cases == 31;
    }

    private static bool Start(ManagedSettingsCenter app,
        FakeAssociationService service, GuideXosAssociationOverrideState state)
    {
        service.ResetCounters();
        service.SetExternalState(state);
        return app.InitializeForTests() &&
            app.EnterDefaultAppsForTests(service);
    }

    private static bool Case(ref int count, bool value)
    {
        count++;
        if (!value && _firstFailedCase == 0) _firstFailedCase = count;
        return value;
    }

    private sealed class FakeAssociationService : IManagedDefaultAppsAssociationService
    {
        public GuideXosAssociationOverrideState State { get; private set; }
        public int QueryCount { get; private set; }
        public int MutationCount { get; private set; }
        public string LastOperation { get; private set; } = "";
        public string LastMutation { get; private set; } = "";
        public bool FailNextMutation { get; set; }

        public void ResetCounters()
        {
            QueryCount = 0;
            MutationCount = 0;
            LastOperation = "";
            LastMutation = "";
            FailNextMutation = false;
        }

        public void SetExternalState(GuideXosAssociationOverrideState state) => State = state;

        public GuideXosAssociationResult QueryTxt()
        {
            QueryCount++;
            LastOperation = "Query";
            return MakeResult(GuideXosAssociationStatus.Success, State);
        }

        public GuideXosAssociationResult SetManagedNotes() => Mutate(
            "SetOverride", GuideXosAssociationOverrideState.ApplicationOverride);

        public GuideXosAssociationResult DisableTxt() => Mutate(
            "Disable", GuideXosAssociationOverrideState.Disabled);

        public GuideXosAssociationResult ResetTxt() => Mutate(
            "Reset", GuideXosAssociationOverrideState.NoOverride);

        private GuideXosAssociationResult Mutate(string operation,
            GuideXosAssociationOverrideState next)
        {
            MutationCount++;
            LastOperation = operation;
            LastMutation = operation;
            if (FailNextMutation)
            {
                FailNextMutation = false;
                return MakeResult(GuideXosAssociationStatus.PersistenceFailed, State);
            }
            State = next;
            return MakeResult(GuideXosAssociationStatus.Success, State);
        }

        private static GuideXosAssociationResult MakeResult(
            GuideXosAssociationStatus status,
            GuideXosAssociationOverrideState state)
        {
            NativeAssociationServiceResponse response = default;
            NativeAssociationServiceResponse* pointer = &response;
            pointer->status = (uint)status;
            pointer->overrideState = (uint)state;
            pointer->hasEffectiveAssociation = state ==
                GuideXosAssociationOverrideState.Disabled ? 0u : 1u;
            NotesIdBytes.CopyTo(new Span<byte>(pointer->compiledDefaultAppId, 96));
            if (state == GuideXosAssociationOverrideState.NoOverride)
                NotesIdBytes.CopyTo(new Span<byte>(pointer->effectiveAppId, 96));
            else if (state == GuideXosAssociationOverrideState.ApplicationOverride)
            {
                NotesIdBytes.CopyTo(new Span<byte>(pointer->overrideAppId, 96));
                NotesIdBytes.CopyTo(new Span<byte>(pointer->effectiveAppId, 96));
            }
            ".txt"u8.CopyTo(new Span<byte>(pointer->normalizedExtension, 16));
            return new GuideXosAssociationResult(status, response);
        }

        private static ReadOnlySpan<byte> NotesIdBytes =>
            "com.guidexos.apps.managed.notes"u8;
    }
}
#endif
