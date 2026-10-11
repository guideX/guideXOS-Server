using System;

namespace HostLogProof.Applications;

internal interface IManagedDefaultAppsAssociationService
{
    GuideXosAssociationResult QueryTxt();
    GuideXosAssociationResult SetManagedNotes();
    GuideXosAssociationResult DisableTxt();
    GuideXosAssociationResult ResetTxt();
}

internal sealed class ManagedDefaultAppsHostService : IManagedDefaultAppsAssociationService
{
    internal static ReadOnlySpan<byte> Extension => ".txt"u8;
    private readonly GuideXosHost _host;

    public ManagedDefaultAppsHostService(GuideXosHost host) => _host = host;

    public GuideXosAssociationResult QueryTxt() => _host.QueryFileAssociation(Extension);
    public GuideXosAssociationResult SetManagedNotes() => _host.SetFileAssociation(
        Extension, "com.guidexos.apps.managed.notes"u8);
    public GuideXosAssociationResult DisableTxt() => _host.DisableFileAssociation(Extension);
    public GuideXosAssociationResult ResetTxt() => _host.ResetFileAssociation(Extension);
}

/// <summary>
/// One bounded Apps page within the existing Settings surface. Its controls
/// are owned by Settings and attached to the same scroll view and focus host.
/// </summary>
internal sealed class ManagedDefaultAppsPageC167
{
    private const string NotesId = "com.guidexos.apps.managed.notes";
    private readonly GuideXosGroupBox _group = new(0, 0, 272, 216, "Default Apps");
    private readonly GuideXosVerticalStack _stack = new(0, 0, 240, 8);
    private readonly GuideXosLabel _fileType = new(0, 0, 216, "File type: .txt");
    private readonly GuideXosLabel _current = new(0, 0, 216, "Default: not loaded");
    private readonly GuideXosLabel _choiceHeading = new(0, 0, 216, "Choose default application");
    private readonly GuideXosComboBox _choice = new(0, 0, 216, 18, 4, 32, 2);
    private readonly GuideXosLabel _feedback = new(0, 0, 216, "Pending: none");
    private readonly GuideXosButton _apply = new(0, 0, 216, 18, "Apply");
    private readonly GuideXosButton _reset = new(0, 0, 216, 18, "Reset to System Default");
    private readonly GuideXosLabel _audience = new(0, 0, 216, "Changes affect all users.");
    private readonly object[] _members;
    private IManagedDefaultAppsAssociationService _service;
    private NativeAssociationServiceResponse _committed;
    private bool _hasCommitted;
    private bool _syncing;
    private bool _errorShown;
    private int _pendingChoice;
    public string AttachDiagnostics { get; private set; } = "not-run";

    public ManagedDefaultAppsPageC167()
    {
        _members = new object[] { _fileType, _current, _choiceHeading, _choice,
            _feedback, _apply, _reset, _audience };
        foreach (object member in _members)
        {
            _stack.TryAddMember(member);
            _group.TryAddMember(member);
        }
        _stack.TrySetPadding(4, 2, 4, 2);
        _stack.TrySetSpacing(1);
        _choice.TryAddItem("Managed Notes");
        _choice.TryAddItem("No default application");
        _choice.Changed = OnChoiceChanged;
        SetTextFromState();
    }

    public GuideXosComboBox Choice => _choice;
    public GuideXosButton ApplyButton => _apply;
    public GuideXosButton ResetButton => _reset;
    public GuideXosGroupBox Group => _group;
    public GuideXosVerticalStack Stack => _stack;
    public string CurrentText => _current.Text;
    public string FeedbackText => _feedback.Text;
    public int PendingChoice => _pendingChoice;
    public bool HasCommittedState => _hasCommitted;
    public bool HasLoadedState => _hasCommitted;
    public GuideXosAssociationOverrideState CommittedState =>
        _hasCommitted
            ? (GuideXosAssociationOverrideState)_committed.overrideState
            : GuideXosAssociationOverrideState.NoOverride;

    public void SetService(IManagedDefaultAppsAssociationService service) => _service = service;

    public bool Refresh()
    {
        GuideXosAssociationResult query = _service?.QueryTxt() ?? default;
        if (!IsValidQuery(query))
        {
            _hasCommitted = false;
            _pendingChoice = 0;
            _current.SetText("Default: unavailable");
            SyncChoice();
            Fail("Could not read .txt default.");
            return false;
        }
        _committed = query.Response;
        _hasCommitted = true;
        _errorShown = false;
        _pendingChoice = _committed.overrideState ==
            (uint)GuideXosAssociationOverrideState.Disabled ? 1 : 0;
        SyncChoice();
        SetTextFromState();
        return true;
    }

    public bool Attach(GuideXosScrollView view)
    {
        AttachDiagnostics = "start";
        if (view == null) { AttachDiagnostics = "null-view"; return false; }
        view.Clear();
        if (!_group.TrySetBounds(0, 0, 272, 216))
        { AttachDiagnostics = "group-bounds"; return false; }
        if (!_group.TryGetContentRectangle(out int left, out int top,
                out int width, out _))
        { AttachDiagnostics = "content-rect"; return false; }
        if (!_stack.TrySetFrame(left, top, width))
        { AttachDiagnostics = $"stack-frame:{left},{top},{width}"; return false; }
        GuideXosVerticalStackResult layout = _stack.PerformLayout();
        if (layout != GuideXosVerticalStackResult.LaidOut)
        { AttachDiagnostics = $"stack-layout:{layout}:{_stack.MemberCount}"; return false; }
        bool ok = view.TryAddMember(_group, 4, 4) == GuideXosScrollViewResult.Added;
        if (!ok) AttachDiagnostics = "add-group";
        for (int index = 0; index < _members.Length; index++)
        {
            object member = _members[index];
            GuideXosScrollViewResult add = view.TryAddMember(member, 20,
                44 + index * 18);
            if (add != GuideXosScrollViewResult.Added)
            { AttachDiagnostics = $"add-member:{index}:{add}"; ok = false; }
        }
        view.RecalculateContentExtent();
        bool geometry = _group.ValidateMembershipGeometry(out int outside);
        bool valid = ok && view.MemberCount == _members.Length + 1 &&
            view.ContainsMember(_group) && view.ContainsMember(_choice) &&
            geometry && outside == 0;
        if (valid) AttachDiagnostics = "valid";
        else if (ok) AttachDiagnostics = $"validate:view={view.MemberCount}:group={_group.MemberCount}:outside={outside}";
        return valid;
    }

    public bool Validate(GuideXosScrollView view)
    {
        if (view == null || view.MemberCount != _members.Length + 1 ||
            view.MaximumMemberCount < _members.Length + 1 ||
            _group.MemberCount != _members.Length ||
            _stack.MemberCount != _members.Length || _choice.ItemCount != 2 ||
            _choice.GetItemText(0) != "Managed Notes" ||
            _choice.GetItemText(1) != "No default application" ||
            !view.ContainsMember(_group) ||
            !_group.ValidateMembershipGeometry(out int outside) || outside != 0)
            return false;
        foreach (object member in _members)
            if (!view.ContainsMember(member) || !_stack.ContainsMember(member))
                return false;
        return true;
    }

    public bool Apply()
    {
        if (_service == null) return Fail("Could not save .txt default.");
        GuideXosAssociationResult mutation = _pendingChoice == 0
            ? _service.SetManagedNotes()
            : _service.DisableTxt();
        if (mutation.Status != GuideXosAssociationStatus.Success)
            return Fail("Could not save .txt default.");

        GuideXosAssociationResult query = _service.QueryTxt();
        if (!IsValidQuery(query) || !MatchesChoice(query.Response, _pendingChoice))
            return Fail("Could not verify .txt default.");
        _committed = query.Response;
        _hasCommitted = true;
        _errorShown = false;
        SetTextFromState();
        return true;
    }

    public bool ResetToSystemDefault()
    {
        if (_service == null || _service.ResetTxt().Status != GuideXosAssociationStatus.Success)
            return Fail("Could not reset .txt default.");
        GuideXosAssociationResult query = _service.QueryTxt();
        if (!IsValidQuery(query) || query.Response.overrideState !=
                (uint)GuideXosAssociationOverrideState.NoOverride ||
            !HasCompiledId(query.Response, NotesId) ||
            !HasEffectiveId(query.Response, NotesId))
            return Fail("Could not verify system default.");
        _committed = query.Response;
        _hasCommitted = true;
        _errorShown = false;
        _pendingChoice = 0;
        SyncChoice();
        SetTextFromState();
        return true;
    }

    public void ResetUi()
    {
        _choice.Close();
        _committed = default;
        _hasCommitted = false;
        _pendingChoice = 0;
        _group.Reset();
        _group.ClearMembers();
        _stack.Clear();
        _apply.Reset();
        _reset.Reset();
        _reset.SetLabel("Reset to System Default");
        _choice.Reset();
        _choice.ClearItems();
        _choice.TryAddItem("Managed Notes");
        _choice.TryAddItem("No default application");
        _choice.Changed = OnChoiceChanged;
        foreach (object member in _members)
        {
            _stack.TryAddMember(member);
            _group.TryAddMember(member);
        }
        _stack.TrySetPadding(4, 2, 4, 2);
        _stack.TrySetSpacing(1);
        _errorShown = false;
        _current.SetText("Default: not loaded");
        _feedback.SetText("Pending: none");
        _choice.SetVisible(false);
        _syncing = false;
        SetTextFromState();
    }

    private void OnChoiceChanged(int index)
    {
        if (_syncing || index is < 0 or > 1) return;
        _pendingChoice = index;
        _errorShown = false;
        SetTextFromState();
    }

    private void SyncChoice()
    {
        _syncing = true;
        _choice.TrySetSelectedIndex(_pendingChoice);
        _syncing = false;
    }

    private void SetTextFromState()
    {
        if (!_hasCommitted)
        {
            if (!_errorShown) _current.SetText("Default: not loaded");
        }
        else if (_committed.overrideState ==
                 (uint)GuideXosAssociationOverrideState.Disabled)
            _current.SetText("Disabled: no default app");
        else if (_committed.overrideState ==
                 (uint)GuideXosAssociationOverrideState.ApplicationOverride)
            _current.SetText("Explicit default: Managed Notes");
        else
            _current.SetText("System default: Managed Notes");

        if (!_errorShown)
        {
            bool pending = _hasCommitted &&
                ((_committed.overrideState ==
                    (uint)GuideXosAssociationOverrideState.Disabled) !=
                 (_pendingChoice == 1));
            _feedback.SetText(pending
                ? _pendingChoice == 0
                    ? "Pending: Managed Notes"
                    : "Pending: No default application"
                : "Pending: none");
        }
    }

    private bool Fail(string message)
    {
        _errorShown = true;
        _feedback.SetText(message);
        return false;
    }

    private static bool IsValidQuery(GuideXosAssociationResult result)
    {
        if (result.Status != GuideXosAssociationStatus.Success) return false;
        NativeAssociationServiceResponse response = result.Response;
        return response.overrideState switch
        {
            (uint)GuideXosAssociationOverrideState.NoOverride =>
                HasCompiledId(response, NotesId) &&
                HasEffectiveId(response, NotesId) &&
                response.hasEffectiveAssociation == 1u,
            (uint)GuideXosAssociationOverrideState.ApplicationOverride =>
                HasOverrideId(response, NotesId) &&
                HasEffectiveId(response, NotesId) &&
                response.hasEffectiveAssociation == 1u,
            (uint)GuideXosAssociationOverrideState.Disabled =>
                response.hasEffectiveAssociation == 0u,
            _ => false,
        };
    }

    private static bool MatchesChoice(NativeAssociationServiceResponse response,
        int choice) => choice == 0
        ? response.overrideState == (uint)GuideXosAssociationOverrideState.ApplicationOverride &&
            HasEffectiveId(response, NotesId)
        : response.overrideState == (uint)GuideXosAssociationOverrideState.Disabled &&
            response.hasEffectiveAssociation == 0u;

    private static unsafe bool HasCompiledId(NativeAssociationServiceResponse response,
        string expected)
    {
        return HasId(response.compiledDefaultAppId, expected);
    }

    private static unsafe bool HasOverrideId(NativeAssociationServiceResponse response,
        string expected)
    {
        return HasId(response.overrideAppId, expected);
    }

    private static unsafe bool HasEffectiveId(NativeAssociationServiceResponse response,
        string expected)
    {
        return HasId(response.effectiveAppId, expected);
    }

    private static unsafe bool HasId(byte* id, string expected)
    {
        if (id == null) return false;
        for (int index = 0; index < expected.Length; index++)
            if (id[index] != (byte)expected[index]) return false;
        return id[expected.Length] == 0;
    }
}
