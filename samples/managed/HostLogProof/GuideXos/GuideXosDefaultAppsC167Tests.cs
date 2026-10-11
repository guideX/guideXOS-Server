#if HOSTLOGPROOF_C167_DEFAULT_APPS_PROOF
using System;
using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>Focused state, service-routing, input, and lifecycle coverage for C167.</summary>
public static unsafe class GuideXosDefaultAppsC167Tests
{
    private const string NotesId = "com.guidexos.apps.managed.notes";
    private static int _firstFailedCase;
    public static int FirstFailedCase => _firstFailedCase;
    public static int CasesExecuted { get; private set; }
    public static string FailureDetail { get; private set; } = "";

    public static bool Run(GuideXosHost host, ManagedSettingsCenter app)
    {
        int cases = 0;
        bool passed = true;
        _firstFailedCase = 0;
        CasesExecuted = 0;
        FailureDetail = "";
        FakeAssociationService service = new();

        passed &= Case(ref cases, Start(app, service,
            GuideXosAssociationOverrideState.NoOverride) &&
            app.DefaultAppsPage.CurrentText ==
                "System default: Managed Notes" &&
            app.DefaultAppsPage.Choice.SelectedIndex == 0);

        bool explicitStart = Start(app, service,
            GuideXosAssociationOverrideState.ApplicationOverride);
        bool explicitUi = app.DefaultAppsPage.CurrentText ==
                "Explicit default: Managed Notes" &&
            app.DefaultAppsPage.Choice.SelectedIndex == 0;
        if (explicitStart && !explicitUi)
            FailureDetail = $"explicit state={app.DefaultAppsPage.CommittedState} text={app.DefaultAppsPage.CurrentText} choice={app.DefaultAppsPage.Choice.SelectedIndex}";
        passed &= Case(ref cases, explicitStart && explicitUi);

        bool disabledStart = Start(app, service,
            GuideXosAssociationOverrideState.Disabled);
        bool disabledUi = app.DefaultAppsPage.CurrentText ==
                "Disabled: no default app" &&
            app.DefaultAppsPage.Choice.SelectedIndex == 1;
        if (disabledStart && !disabledUi)
            FailureDetail = $"disabled state={app.DefaultAppsPage.CommittedState} text={app.DefaultAppsPage.CurrentText} choice={app.DefaultAppsPage.Choice.SelectedIndex}";
        passed &= Case(ref cases, disabledStart && disabledUi);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        int beforeSelection = service.MutationCount;
        app.DefaultAppsPage.Choice.TrySetSelectedIndex(1);
        passed &= Case(ref cases, service.MutationCount == beforeSelection &&
            app.DefaultAppsPage.PendingChoice == 1 &&
            app.DefaultAppsPage.FeedbackText ==
                "Pending: No default application" &&
            app.DefaultAppsPage.CommittedState ==
                GuideXosAssociationOverrideState.NoOverride);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        passed &= Case(ref cases, app.DefaultAppsPage.Apply() &&
            service.LastMutation == "SetOverride" &&
            app.DefaultAppsPage.CommittedState ==
                GuideXosAssociationOverrideState.ApplicationOverride);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        app.DefaultAppsPage.Choice.TrySetSelectedIndex(1);
        passed &= Case(ref cases, app.DefaultAppsPage.Apply() &&
            service.LastMutation == "Disable" &&
            app.DefaultAppsPage.CommittedState ==
                GuideXosAssociationOverrideState.Disabled);

        Start(app, service, GuideXosAssociationOverrideState.Disabled);
        passed &= Case(ref cases, app.DefaultAppsPage.ResetToSystemDefault() &&
            service.LastMutation == "Reset" &&
            app.DefaultAppsPage.CommittedState ==
                GuideXosAssociationOverrideState.NoOverride &&
            app.DefaultAppsPage.CurrentText ==
                "System default: Managed Notes");

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        int queryBeforeApply = service.QueryCount;
        app.DefaultAppsPage.Apply();
        passed &= Case(ref cases, service.QueryCount == queryBeforeApply + 1 &&
            service.LastOperation == "Query" &&
            app.DefaultAppsPage.CommittedState ==
                GuideXosAssociationOverrideState.ApplicationOverride);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        string priorDisplay = app.DefaultAppsPage.CurrentText;
        app.DefaultAppsPage.Choice.TrySetSelectedIndex(1);
        service.FailNextMutation = true;
        passed &= Case(ref cases, !app.DefaultAppsPage.Apply() &&
            app.DefaultAppsPage.CurrentText == priorDisplay &&
            app.DefaultAppsPage.FeedbackText ==
                "Could not save .txt default." &&
            app.DefaultAppsPage.CommittedState ==
                GuideXosAssociationOverrideState.NoOverride);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        passed &= Case(ref cases, app.DefaultAppsPage.Choice.ItemCount == 2 &&
            app.DefaultAppsPage.Choice.GetItemText(0) == "Managed Notes" &&
            app.DefaultAppsPage.Choice.GetItemText(1) == "No default application" &&
            !app.DefaultAppsPage.Choice.TrySetSelectedIndex(2));

        passed &= Case(ref cases, ManagedDefaultAppsHostService.Extension
            .SequenceEqual(".txt"u8));

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        service.SetExternalState(GuideXosAssociationOverrideState.Disabled);
        app.ReturnToSettingsForTests();
        bool reentered = app.EnterDefaultAppsForTests(service);
        passed &= Case(ref cases, reentered && service.QueryCount >= 2 &&
            app.DefaultAppsPage.CurrentText ==
                "Disabled: no default app" &&
            app.DefaultAppsPage.Choice.SelectedIndex == 1);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        app.DefaultAppsPage.Choice.TrySetSelectedIndex(1);
        int beforeClose = service.MutationCount;
        bool close = app.CloseDefaultAppsForTests();
        passed &= Case(ref cases, close && service.MutationCount == beforeClose &&
            service.State == GuideXosAssociationOverrideState.NoOverride);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        app.DefaultAppsPage.Choice.TrySetSelectedIndex(1);
        bool repeated = true;
        for (int index = 0; index < 3; index++)
            repeated &= app.DefaultAppsPage.Apply() &&
                app.DefaultAppsPage.ResetToSystemDefault();
        passed &= Case(ref cases, repeated && service.MutationCount == 6 &&
            app.DefaultAppsPage.CommittedState ==
                GuideXosAssociationOverrideState.NoOverride);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        app.DefaultAppsPage.Choice.TrySetSelectedIndex(0);
        GuideXosComboBox choice = app.DefaultAppsPage.Choice;
        bool pointerOpen = choice.HandlePointerDown(choice.X + 2, choice.Y + 2) ==
            GuideXosComboBoxResult.Opened;
        bool pointerCommit = choice.HandlePointerDown(choice.X + 2,
            choice.PopupY + GuideXosComboBox.PopupRowHeight + 2) ==
            GuideXosComboBoxResult.SelectionChanged;
        int pointerChoice = app.DefaultAppsPage.PendingChoice;
        choice.TrySetSelectedIndex(0);
        app.ControlHost.TryFocus(12);
        bool keyboardOpen = choice.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosComboBoxResult.Opened;
        bool keyboardMove = choice.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosComboBoxResult.Moved;
        bool keyboardCommit = choice.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosComboBoxResult.SelectionChanged;
        passed &= Case(ref cases, pointerOpen && pointerCommit &&
            keyboardOpen && keyboardMove && keyboardCommit && pointerChoice == 1 &&
            app.DefaultAppsPage.PendingChoice == pointerChoice &&
            service.MutationCount == 0);

        Start(app, service, GuideXosAssociationOverrideState.NoOverride);
        GuideXosControlHost controlHost = app.ControlHost;
        bool tabBounded = controlHost.TryFocus(11) ==
            GuideXosControlHostResult.Focused;
        for (int index = 0; tabBounded && index < 18; index++)
        {
            controlHost.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Tab));
            tabBounded &= controlHost.ActiveControlId is >= 1 and <= 12;
        }
        for (int index = 0; tabBounded && index < 18; index++)
        {
            controlHost.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Tab, true));
            tabBounded &= controlHost.ActiveControlId is >= 1 and <= 12;
        }
        passed &= Case(ref cases, tabBounded && !controlHost.IsModalActive);

        bool popup = controlHost.TryFocus(12) == GuideXosControlHostResult.Focused &&
            app.DefaultAppsPage.Choice.Open() == GuideXosComboBoxResult.Opened &&
            controlHost.TryAcquireTransientInputCapture(12);
        controlHost.HandleInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Escape));
        passed &= Case(ref cases, popup && !app.DefaultAppsPage.Choice.IsOpen &&
            !controlHost.HasTransientInputCapture && !controlHost.HasPointerDragCapture);

        passed &= Case(ref cases, ManagedSettingsStore.FormatVersion == 2 &&
            ManagedSettingsStore.EncodedFileBytes == 26);

        NativeHostCallTable table = default;
        NativeHostCallTable* tablePointer = &table;
        passed &= Case(ref cases, sizeof(NativeHostCallTable) == 128 &&
            sizeof(NativeAssociationServiceResponse) == 328 &&
            (byte*)&tablePointer->associationService - (byte*)tablePointer == 120);

        bool stress = true;
        for (int index = 0; index < 60; index++)
        {
            service.SetExternalState(GuideXosAssociationOverrideState.NoOverride);
            stress &= app.InitializeForTests() &&
                app.EnterDefaultAppsForTests(service);
            app.DefaultAppsPage.Choice.TrySetSelectedIndex(index % 2);
            if ((index % 3) == 0)
                stress &= app.DefaultAppsPage.Apply();
            if ((index % 4) == 0)
                stress &= app.DefaultAppsPage.ResetToSystemDefault();
            if ((index % 2) == 0)
                stress &= app.ReturnToSettingsForTests();
            else
                stress &= app.CloseDefaultAppsForTests();
            stress &= !app.HasCapture && !app.HasDragOwner &&
                app.RegistrationCount == 12;
        }
        bool clean = app.InitializeForTests() && app.RegistrationCount == 12 &&
            !app.IsDefaultAppsPage && !app.HasCapture && !app.HasDragOwner;
        passed &= Case(ref cases, stress && clean);

        host?.TryLog(passed && cases == 20
            ? "C167-FOCUSED-TESTS cases=20 stress=60 result=PASS"u8
            : "C167-FOCUSED-TESTS result=FAIL"u8);
        if (!passed || cases != 20)
        {
            Span<byte> diagnostic = stackalloc byte[64];
            int position = 0;
            GuideXosText.Append(diagnostic, ref position,
                "C167-TEST-FAIL first="u8);
            GuideXosText.AppendUnsigned(diagnostic, ref position,
                (uint)_firstFailedCase);
            GuideXosText.Append(diagnostic, ref position, " cases="u8);
            GuideXosText.AppendUnsigned(diagnostic, ref position, (uint)cases);
            host?.TryLog(diagnostic[..position]);
        }
        CasesExecuted = cases;
        return passed && cases == 20;
    }

    private static bool Start(ManagedSettingsCenter app,
        FakeAssociationService service,
        GuideXosAssociationOverrideState state)
    {
        service.ResetCounters();
        service.SetExternalState(state);
        if (!app.InitializeForTests())
        {
            FailureDetail = $"initialize reg={app.RegistrationCount} cap={app.HostCapacity} view={app.View.MemberCount}";
            return false;
        }
        if (!app.EnterDefaultAppsForTests(service))
        {
            FailureDetail = $"enter reg={app.RegistrationCount} cap={app.HostCapacity} view={app.View.MemberCount} page={app.IsDefaultAppsPage} attach={app.DefaultAppsPage.AttachDiagnostics}";
            return false;
        }
        return true;
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
            byte* compiled = pointer->compiledDefaultAppId;
            NotesIdBytes.CopyTo(new Span<byte>(compiled, 96));
            if (state == GuideXosAssociationOverrideState.NoOverride)
            {
                byte* effective = pointer->effectiveAppId;
                NotesIdBytes.CopyTo(new Span<byte>(effective, 96));
            }
            else if (state == GuideXosAssociationOverrideState.ApplicationOverride)
            {
                byte* overridden = pointer->overrideAppId;
                NotesIdBytes.CopyTo(new Span<byte>(overridden, 96));
                byte* effective = pointer->effectiveAppId;
                NotesIdBytes.CopyTo(new Span<byte>(effective, 96));
            }
            byte* normalized = pointer->normalizedExtension;
            ".txt"u8.CopyTo(new Span<byte>(normalized, 16));
            return new GuideXosAssociationResult(status, response);
        }

        private static ReadOnlySpan<byte> NotesIdBytes =>
            "com.guidexos.apps.managed.notes"u8;
    }
}
#endif
