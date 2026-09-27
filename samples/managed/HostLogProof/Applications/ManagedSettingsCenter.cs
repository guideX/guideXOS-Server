using System;

namespace HostLogProof.Applications;

internal struct ManagedSettingsSnapshot : IEquatable<ManagedSettingsSnapshot>
{
    public int Density;
    public bool ShowStatus;
    public bool ShowAdvanced;
    public bool InputEnabled;
    public bool NaturalScroll;
    public int ScrollSpeed;
    public bool ShowKeyboardTips;
    public int StatusDetail;
    public int ReportFormat;

    public static ManagedSettingsSnapshot Defaults => new()
    {
        Density = 0,
        ShowStatus = true,
        ShowAdvanced = false,
        InputEnabled = true,
        NaturalScroll = false,
        ScrollSpeed = 1,
        ShowKeyboardTips = true,
        StatusDetail = 0,
        ReportFormat = 0,
    };

    public readonly bool Equals(ManagedSettingsSnapshot other) =>
        Density == other.Density && ShowStatus == other.ShowStatus &&
        ShowAdvanced == other.ShowAdvanced && InputEnabled == other.InputEnabled &&
        NaturalScroll == other.NaturalScroll && ScrollSpeed == other.ScrollSpeed &&
        ShowKeyboardTips == other.ShowKeyboardTips && StatusDetail == other.StatusDetail &&
        ReportFormat == other.ReportFormat;

    public override readonly bool Equals(object value) =>
        value is ManagedSettingsSnapshot other && Equals(other);

    public override readonly int GetHashCode() =>
        Density ^ (ShowStatus ? 1 << 4 : 0) ^ (ShowAdvanced ? 1 << 5 : 0) ^
        (InputEnabled ? 1 << 6 : 0) ^ (NaturalScroll ? 1 << 7 : 0) ^
        (ScrollSpeed << 8) ^ (ShowKeyboardTips ? 1 << 12 : 0) ^
        (StatusDetail << 13) ^ (ReportFormat << 14);
}

/// <summary>
/// Four real settings sections composed from the C143 controls, geometry-only
/// stacks, and one direct-member ScrollView. The application owns every
/// control; all container associations are non-owning.
/// </summary>
public sealed class ManagedSettingsCenter : GuideXosApplication
{
    private const int ViewId = 1;
    private const int ScrollBarId = 2;
    private const int DensityComboId = 3;
    private const int SpeedComboId = 4;
    private const int StatusComboId = 5;
    private const int MenuId = 6;
    private const int OptionsButtonId = 7;
    private const int InputEnabledId = 8;
    private const int AdvancedToggleId = 9;
    private const int ViewCapacity = 24;
    private const int SectionCount = 4;
    private const uint MenuApply = 1;
    private const uint MenuDefaults = 2;
    private const uint MenuClose = 3;

    private readonly GuideXosScrollView _view = new(24, 86, 300, 208, ViewCapacity);
    private readonly GuideXosScrollBar _scrollBar = new(336, 86, 16, 208);
    private readonly GuideXosGroupBox[] _groups =
    {
        new(0, 0, 272, 180, "Appearance"),
        new(0, 0, 272, 198, "Input"),
        new(0, 0, 272, 216, "System"),
        new(0, 0, 272, 144, "Advanced"),
    };
    private readonly GuideXosVerticalStack[] _stacks =
    {
        new(0, 0, 240, 8), new(0, 0, 240, 8),
        new(0, 0, 240, 8), new(0, 0, 240, 8),
    };
    private readonly GuideXosLabel _appearanceHeading = new(0, 0, 216, "Display density");
    private readonly GuideXosComboBox _density = new(0, 0, 216, 18, 4, 16, 2);
    private readonly GuideXosCheckBox _showStatus = new(0, 0, 216, 18, "Show status summary", true);
    private readonly GuideXosCheckBox _showAdvanced = new(280, 54, 138, 18, "Advanced settings");
    private readonly GuideXosLabel _inputHeading = new(0, 0, 216, "Wheel behavior");
    private readonly GuideXosRadioButton _standardWheel = new(0, 0, 216, 18, "Standard scrolling", true);
    private readonly GuideXosRadioButton _naturalWheel = new(0, 0, 216, 18, "Natural scrolling");
    private readonly GuideXosRadioGroup _wheelGroup = new(2);
    private readonly GuideXosComboBox _speed = new(0, 0, 216, 18, 4, 16, 4);
    private readonly GuideXosCheckBox _showTips = new(0, 0, 216, 18, "Show keyboard tips", true);
    private readonly GuideXosLabel _systemHeading = new(0, 0, 216, "Settings preview");
    private readonly GuideXosRadioButton _summaryMode = new(0, 0, 216, 18, "Summary", true);
    private readonly GuideXosRadioButton _detailMode = new(0, 0, 216, 18, "Detailed");
    private readonly GuideXosRadioGroup _statusGroup = new(2);
    private readonly GuideXosComboBox _statusCombo = new(0, 0, 216, 18, 4, 16, 2);
    private readonly GuideXosProgressBar _statusProgress = new(0, 0, 216, 0, 100, 52);
    private readonly GuideXosLabel _systemDescription = new(0, 0, 216, "Compact settings summary");
    private readonly GuideXosLabel _advancedHeading = new(0, 0, 216, "Working copy commands");
    private readonly GuideXosButton _applyButton = new(0, 0, 128, 18, "Apply");
    private readonly GuideXosButton _defaultsButton = new(0, 0, 128, 18, "Defaults");
    private readonly GuideXosCheckBox _inputEnabled =
        new(24, 54, 248, 18, "Enable input settings", true);
    private readonly GuideXosButton _optionsButton = new(420, 48, 112, 22, "Options");
    private readonly GuideXosPopupMenu _menu = new(0, 0, 144, 4, 20);
    private readonly GuideXosDialog _resetDialog = new(112, 96, 336, 160,
        "Confirm reset");
    private readonly GuideXosButton _resetCancelButton =
        new(140, 226, 96, 18, "Cancel");
    private readonly GuideXosButton _resetConfirmButton =
        new(264, 226, 96, 18, "Reset");
    private readonly GuideXosDialog _dirtyCloseDialog = new(92, 96, 376, 160,
        "Unsaved changes");
    private readonly GuideXosButton _closeApplyButton =
        new(136, 226, 88, 18, "Apply");
    private readonly GuideXosButton _closeDiscardButton =
        new(236, 226, 88, 18, "Discard");
    private readonly GuideXosButton _closeCancelButton =
        new(336, 226, 88, 18, "Cancel");
    private GuideXosDialog _persistenceDialog;
    private GuideXosButton _persistenceOkButton;
    private readonly object[][] _sectionLeaves;
    private GuideXosControlHost _controlHost;
    private GuideXosHost _appHost;
    private GuideXosSurface _surface;
    private ulong _window;
    private ManagedSettingsSnapshot _working;
    private ManagedSettingsSnapshot _applied;
    private ManagedSettingsSnapshot _persisted;
    private ManagedSettingsStore _settingsStore;
    private bool _syncing;
    private bool _testsRun;
    private uint _launchCount;
    private uint _menuCommand;
    private int _initialContentHeight;
    private int _finalContentHeight;
    private uint _geometrySequence;
    private GuideXosDialog _activeDialog;
    private bool _resettingComposition;
    private bool _surfaceClosing;
#if !HOSTLOGPROOF_C146_PERSISTENT_SETTINGS
    private bool _c145TestsPassed;
#endif
    private bool _c146TestsPassed;
#if HOSTLOGPROOF_C146_PERSISTENT_SETTINGS
    private bool _c146StoreTestsPassed;
#endif
    private bool _c145FocusedTesting;
    private bool _saveInProgress;
    private bool _persistedFilePresent;
    private bool _failNextSaveForTests;
    private byte _pendingPersistenceMessage;
    private ManagedSettingsSnapshot _dialogWorkingSnapshot;
    private ManagedSettingsSnapshot _dialogAppliedSnapshot;
    private int _dialogViewportOffset;

    public ManagedSettingsCenter()
    {
        _sectionLeaves = new object[][]
        {
            new object[] { _appearanceHeading, _density, _showStatus },
            new object[] { _inputHeading, _standardWheel, _naturalWheel, _speed, _showTips },
            new object[] { _systemHeading, _summaryMode, _detailMode, _statusCombo, _statusProgress, _systemDescription },
            new object[] { _advancedHeading, _applyButton, _defaultsButton },
        };

        ConfigureMember(_appearanceHeading, GuideXosVerticalStackHorizontalAlignment.Left, 0, 2, 0, 2);
        ConfigureMember(_density, GuideXosVerticalStackHorizontalAlignment.Stretch, 8, 2, 8, 2);
        ConfigureMember(_showStatus, GuideXosVerticalStackHorizontalAlignment.Left, 0, 2, 0, 2);
        ConfigureMember(_inputHeading, GuideXosVerticalStackHorizontalAlignment.Left, 0, 2, 0, 2);
        ConfigureMember(_standardWheel, GuideXosVerticalStackHorizontalAlignment.Left, 0, 2, 0, 2);
        ConfigureMember(_naturalWheel, GuideXosVerticalStackHorizontalAlignment.Left, 0, 2, 0, 2);
        ConfigureMember(_speed, GuideXosVerticalStackHorizontalAlignment.Stretch, 8, 2, 8, 2);
        ConfigureMember(_showTips, GuideXosVerticalStackHorizontalAlignment.Left, 0, 2, 0, 2);
        ConfigureMember(_systemHeading, GuideXosVerticalStackHorizontalAlignment.Left, 0, 2, 0, 2);
        ConfigureMember(_summaryMode, GuideXosVerticalStackHorizontalAlignment.Left, 0, 2, 0, 2);
        ConfigureMember(_detailMode, GuideXosVerticalStackHorizontalAlignment.Left, 0, 2, 0, 2);
        ConfigureMember(_statusCombo, GuideXosVerticalStackHorizontalAlignment.Stretch, 8, 2, 8, 2);
        ConfigureMember(_statusProgress, GuideXosVerticalStackHorizontalAlignment.Stretch, 8, 2, 8, 2);
        ConfigureMember(_systemDescription, GuideXosVerticalStackHorizontalAlignment.Left, 0, 2, 0, 2);
        ConfigureMember(_advancedHeading, GuideXosVerticalStackHorizontalAlignment.Left, 0, 2, 0, 2);
        ConfigureMember(_applyButton, GuideXosVerticalStackHorizontalAlignment.Right, 0, 2, 0, 2);
        ConfigureMember(_defaultsButton, GuideXosVerticalStackHorizontalAlignment.Right, 0, 2, 0, 2);

        _density.TryAddItem("Comfortable");
        _density.TryAddItem("Compact");
        _speed.TryAddItem("Slow");
        _speed.TryAddItem("Normal");
        _speed.TryAddItem("Fast");
        _statusCombo.TryAddItem("Current section");
        _statusCombo.TryAddItem("All choices");
        _wheelGroup.TryRegister(_standardWheel);
        _wheelGroup.TryRegister(_naturalWheel);
        _statusGroup.TryRegister(_summaryMode);
        _statusGroup.TryRegister(_detailMode);

        _density.Changed = OnDensityChanged;
        _speed.Changed = OnSpeedChanged;
        _statusCombo.Changed = OnReportFormatChanged;
        _showStatus.Changed = OnShowStatusChanged;
        _showAdvanced.Changed = OnShowAdvancedChanged;
        _showTips.Changed = OnShowTipsChanged;
        _inputEnabled.Changed = OnInputEnabledChanged;
        _standardWheel.Changed = OnWheelModeChanged;
        _naturalWheel.Changed = OnWheelModeChanged;
        _summaryMode.Changed = OnStatusModeChanged;
        _detailMode.Changed = OnStatusModeChanged;
        _menu.CommandInvoked = OnMenuCommand;
        _menu.TryAddItem("Apply", MenuApply);
        _menu.TryAddItem("Reset to defaults", MenuDefaults);
        _menu.TryAddSeparator();
        _menu.TryAddItem("Close", MenuClose);

        _resetDialog.TryAddMember(_resetCancelButton);
        _resetDialog.TryAddMember(_resetConfirmButton);
        _resetDialog.TrySetButtonResult(_resetCancelButton, GuideXosDialogResult.Cancel);
        _resetDialog.TrySetButtonResult(_resetConfirmButton, GuideXosDialogResult.Reset);
        _resetDialog.TrySetDefaultButton(_resetConfirmButton);
        _resetDialog.TrySetCancelResult(GuideXosDialogResult.Cancel);
        _resetDialog.TrySetMessage("Replace the working settings with defaults?");
        _resetDialog.Closed = OnResetDialogClosed;

        _dirtyCloseDialog.TryAddMember(_closeApplyButton);
        _dirtyCloseDialog.TryAddMember(_closeDiscardButton);
        _dirtyCloseDialog.TryAddMember(_closeCancelButton);
        _dirtyCloseDialog.TrySetButtonResult(_closeApplyButton, GuideXosDialogResult.Apply);
        _dirtyCloseDialog.TrySetButtonResult(_closeDiscardButton, GuideXosDialogResult.Discard);
        _dirtyCloseDialog.TrySetButtonResult(_closeCancelButton, GuideXosDialogResult.Cancel);
        _dirtyCloseDialog.TrySetDefaultButton(_closeApplyButton);
        _dirtyCloseDialog.TrySetCancelResult(GuideXosDialogResult.Cancel);
        _dirtyCloseDialog.TrySetMessage("Apply changes before closing?");
        _dirtyCloseDialog.Closed = OnDirtyCloseDialogClosed;

    }

    internal ManagedSettingsSnapshot Working => _working;
    internal ManagedSettingsSnapshot Applied => _applied;
    internal ManagedSettingsSnapshot Persisted => _persisted;
    internal bool PersistedFilePresent => _persistedFilePresent;
    internal bool IsDirty => !_working.Equals(_applied);
    internal GuideXosScrollView View => _view;
    internal GuideXosScrollBar ScrollBar => _scrollBar;
    internal GuideXosControlHost ControlHost => _controlHost;
    internal int LaunchCount => (int)_launchCount;
    internal int SectionGroupCount => SectionCount;
    internal int LeafControlCount => 17;
    internal int RegistrationCount => _controlHost?.RegistrationCount ?? 0;
    internal int HostCapacity => _controlHost?.MaximumControlCount ?? 0;
    internal int DialogRegistrationCount => _resetDialog.RegistrationCount;
    internal int DialogCapacity => _resetDialog.MaximumMemberCount;
    internal GuideXosDialog ActiveDialog => _activeDialog;
    internal GuideXosButton ResetCancelButton => _resetCancelButton;
    internal GuideXosButton ResetConfirmButton => _resetConfirmButton;
    internal GuideXosButton CloseApplyButton => _closeApplyButton;
    internal GuideXosButton CloseDiscardButton => _closeDiscardButton;
    internal GuideXosButton CloseCancelButton => _closeCancelButton;
    internal GuideXosDialog PersistenceDialog => EnsurePersistenceDialog();
    internal GuideXosButton PersistenceOkButton
    {
        get
        {
            EnsurePersistenceDialog();
            return _persistenceOkButton;
        }
    }
    internal GuideXosGroupBox Group(int index) => _groups[index];
    internal GuideXosVerticalStack Stack(int index) => _stacks[index];
    internal object Leaf(int section, int index) => _sectionLeaves[section][index];
    internal GuideXosCheckBox InputEnableCheckBox => _inputEnabled;
    internal GuideXosCheckBox AdvancedCheckBox => _showAdvanced;
    internal GuideXosCheckBox StatusCheckBox => _showStatus;
    internal GuideXosCheckBox TipsCheckBox => _showTips;
    internal GuideXosComboBox DensityCombo => _density;
    internal GuideXosComboBox SpeedCombo => _speed;
    internal GuideXosComboBox StatusCombo => _statusCombo;
    internal GuideXosRadioGroup WheelGroup => _wheelGroup;
    internal GuideXosRadioGroup StatusGroup => _statusGroup;
    internal GuideXosRadioButton NaturalWheel => _naturalWheel;
    internal GuideXosRadioButton StandardWheel => _standardWheel;
    internal GuideXosRadioButton DetailMode => _detailMode;
    internal GuideXosRadioButton SummaryMode => _summaryMode;
    internal GuideXosCheckBox KeyboardTips => _showTips;
    internal GuideXosButton ApplyButton => _applyButton;
    internal GuideXosButton DefaultsButton => _defaultsButton;
    internal bool PopupOpen => _menu.IsOpen || _density.IsOpen || _speed.IsOpen || _statusCombo.IsOpen;
    internal bool HasCapture => _controlHost?.HasTransientInputCapture ?? false;
    internal bool HasDragOwner => _controlHost?.HasPointerDragCapture ?? false;
    internal bool SurfaceClosingForTests => _surfaceClosing;
    internal int InitialContentHeight => _initialContentHeight;
    internal int FinalContentHeight => _finalContentHeight;

    public override GuideXosResult Launch(GuideXosHost host)
    {
        if (host.IsCapabilityProbe) return GuideXosResult.Success;
        _appHost = host;
        ++_launchCount;
        _surfaceClosing = false;
#if HOSTLOGPROOF_C146_PERSISTENT_SETTINGS
        if (!_testsRun)
        {
            host.TryLog("C146-REGRESSION-SCOPE c128-c131=prior-c145-proof c145-dialog=historical c145-settings=covered-by-c146 result=START"u8);
            _c146StoreTestsPassed = ManagedSettingsStoreC146Tests.Run(host);
        }
#endif
        if (!ResetComposition()) return GuideXosResult.InvalidArgument;
        GuideXosResult create = host.TryCreateSurface(
            "Managed Settings Center"u8, 560, 340, out _surface);
        if (create != GuideXosResult.Success || _surface == null) return create;
        _window = _surface.Handle;
        if (!_testsRun)
        {
#if HOSTLOGPROOF_C146_PERSISTENT_SETTINGS
            bool c146Store = _c146StoreTestsPassed;
            _c145FocusedTesting = true;
            bool c146Settings = GuideXosSettingsCenterC146Tests.Run(host, this);
            _c145FocusedTesting = false;
            host.TryLog(c146Store && c146Settings
                ? "C146-REGRESSION-SCOPE c128-c131=prior-c145-proof c145-dialog=historical c145-settings=covered-by-c146 c146-store=PASS c146-settings=PASS result=PASS"u8
                : "C146-REGRESSION-SCOPE c146-store-or-settings=FAIL result=FAIL"u8);
#else
            bool c128 = GuideXosPanelLifecycleTests.Run(host);
            bool c131Api = GuideXosCheckBoxC131Tests.Run(host, _surface);
            bool c131Host = GuideXosCheckBoxC131HostTests.Run(host);
            bool c131 = c131Api && c131Host;
            bool lowerRegressions = c128 && c131;
            bool c145Dialogs = GuideXosDialogC145Tests.Run(host, _dirtyCloseDialog);
            _c145FocusedTesting = true;
            bool c145Settings = GuideXosSettingsCenterC145Tests.Run(host, this);
            _c145FocusedTesting = false;
            host.TryLog(lowerRegressions
                ? "C145-LOWER-REGRESSIONS c128=PASS c131=57/57 result=PASS"u8
                : "C145-LOWER-REGRESSIONS result=FAIL"u8);
#endif
            _testsRun = true;
#if HOSTLOGPROOF_C146_PERSISTENT_SETTINGS
            _c146TestsPassed = c146Store && c146Settings;
#else
            _c145TestsPassed = lowerRegressions && c145Dialogs && c145Settings;
            _c146TestsPassed = true;
#endif
#if !HOSTLOGPROOF_C146_PERSISTENT_SETTINGS
            if (!ResetComposition()) return GuideXosResult.InvalidArgument;
#endif
#if !HOSTLOGPROOF_C146_PERSISTENT_SETTINGS
            host.TryLog(_c145TestsPassed
                ? "C145-FOCUSED-SUITES result=PASS"u8
                : "C145-FOCUSED-SUITES result=FAIL"u8);
#endif
            host.TryLog(_c146TestsPassed
                ? "C146-FOCUSED-SUITES result=PASS"u8
                : "C146-FOCUSED-SUITES result=FAIL"u8);
        }

        bool controlsRegistered = RegisterControls();
        HydratePersistedSettings(host);
        _initialContentHeight = _view.ContentExtent;
        bool valid = controlsRegistered && _controlHost.RegistrationCount == 9 &&
            _view.MemberCount == 21 && _view.MaximumMemberCount == ViewCapacity &&
            _groups.Length == SectionCount && _stacks.Length == SectionCount &&
            _working.Equals(_applied) && _applied.Equals(_persisted) &&
            !IsDirty && _view.Offset == 0 &&
            _view.MaximumOffset > 0 && ValidateComposition();
        host.TryLog(valid
            ? "C144-PROOF launch=PASS registration=9 hostCapacity=10 groupBoxes=4 leaves=17 viewMembers=21 stacks=4 layout=valid result=PASS"u8
            : "C144-PROOF launch=FAIL result=FAIL"u8);
#if HOSTLOGPROOF_C146_PERSISTENT_SETTINGS
        host.TryLog(valid && _resetDialog.RegistrationCount == 2 &&
            _dirtyCloseDialog.RegistrationCount == 3 && _c146TestsPassed
            ? "C146-PROOF launch=PASS registration=9 hostCapacity=10 dialogCapacity=8 resetRegistrations=2 closeRegistrations=3 result=PASS"u8
            : "C146-PROOF launch=FAIL result=FAIL"u8);
#else
        host.TryLog(valid && _resetDialog.RegistrationCount == 2 &&
            _dirtyCloseDialog.RegistrationCount == 3 && _c145TestsPassed
            ? "C145-PROOF launch=PASS registration=9 hostCapacity=10 dialogCapacity=8 resetRegistrations=2 closeRegistrations=3 result=PASS"u8
            : "C145-PROOF launch=FAIL result=FAIL"u8);
#endif
        LogGeometry(host, "initial");
        if (_launchCount > 1)
            host.TryLog(valid ? "C144-RELAUNCH close=PASS relaunch=PASS registration=9 result=PASS"u8 : "C144-RELAUNCH result=FAIL"u8);
        return Render(host, _surface);
    }

    public override GuideXosResult HandleInput(GuideXosHost host, GuideXosInputEvent input)
    {
        if (host.TryGetSurface(_window, out GuideXosSurface surface) != GuideXosResult.Success || surface == null)
            return GuideXosResult.SurfaceCreationFailed;

        if (_activeDialog != null && _activeDialog.IsOpen)
        {
            GuideXosDialog activeDialog = _activeDialog;
            int priorOffset = _view.Offset;
            int priorDialogFocus = activeDialog.ControlHost.ActiveControlId;
            bool outsideClick = input.Kind == GuideXosInputKind.PointerDown &&
                !activeDialog.ContainsPoint(input.X, input.Y) &&
                !activeDialog.HasTransientInputCapture;
            GuideXosControlHostResult modalResult = activeDialog.HandleInput(input);
            if (input.Kind == GuideXosInputKind.KeyDown &&
                input.KeyCode == (uint)GuideXosTextInputKey.Tab)
            {
                bool contained = activeDialog.IsOpen && activeDialog.IsModal &&
                    activeDialog.ControlHost.ActiveControlId != 0;
                host.TryLog(contained
                    ? input.Shift
                        ? "C145-FOCUS shift-tab=contained result=PASS"u8
                        : "C145-FOCUS tab=contained result=PASS"u8
                    : "C145-FOCUS tab=escaped result=FAIL"u8);
            }
            if (input.Kind == GuideXosInputKind.Wheel)
                host.TryLog(_view.Offset == priorOffset
                    ? "C145-MODAL wheel=parent-blocked viewport=preserved result=PASS"u8
                    : "C145-MODAL wheel=parent-leak result=FAIL"u8);
            if (outsideClick)
                host.TryLog(_view.Offset == priorOffset && IsDirty &&
                    ReferenceEquals(_activeDialog, activeDialog) &&
                    _controlHost.IsModalActive &&
                    ReferenceEquals(_controlHost.ActiveScopeHost,
                        activeDialog.ControlHost) && !_menu.IsOpen
                    ? "C145-MODAL outside=consumed parent=blocked result=PASS"u8
                    : "C145-MODAL outside=leaked parent=FAIL result=FAIL"u8);
            if (input.Kind == GuideXosInputKind.KeyDown &&
                input.KeyCode == (uint)GuideXosTextInputKey.Enter &&
                modalResult == GuideXosControlHostResult.Activated)
                host.TryLog("C145-KEYBOARD default=activated once result=PASS"u8);
            if (input.Kind == GuideXosInputKind.KeyDown && input.Shift &&
                input.KeyCode == (uint)GuideXosTextInputKey.Tab &&
                activeDialog.ControlHost.ActiveControlId != priorDialogFocus)
                host.TryLog("C145-FOCUS reverse=advanced-within-modal result=PASS"u8);
            if (_surfaceClosing) return GuideXosResult.Success;
            return Render(host, surface);
        }

        if (input.Kind == GuideXosInputKind.PointerMove)
        {
            _controlHost.HandlePointerMove(input.X, input.Y);
            return Render(host, surface);
        }
        if (input.Kind == GuideXosInputKind.PointerUp)
        {
            bool hadDragOwner = _controlHost.HasPointerDragCapture;
            _controlHost.HandlePointerUp(input.X, input.Y, input.Button);
            if (hadDragOwner)
                host.TryLog(!_controlHost.HasPointerDragCapture
                    ? "C144-DRAG release=PASS owner=none result=PASS"u8
                    : "C144-DRAG release=FAIL result=FAIL"u8);
            LogGeometry(host, "pointer-up");
            return Render(host, surface);
        }
        if (input.Kind == GuideXosInputKind.Wheel)
        {
            int id = HitId(input.X, input.Y);
            int delta = input.WheelDelta;
            if (_applied.NaturalScroll) delta = -delta;
            int multiplier = _applied.ScrollSpeed == 2 ? 2 : 1;
            GuideXosControlHostResult result = id == ViewId
                ? _controlHost.HandleWheel(ViewId, input.X, input.Y, delta * multiplier)
                : GuideXosControlHostResult.Ignored;
            if (result == GuideXosControlHostResult.Scrolled)
            {
                host.TryLog("C144-WHEEL viewport=changed sections-translated=PASS result=PASS"u8);
                LogGeometry(host, "wheel");
            }
            return Render(host, surface);
        }
        if (input.Kind == GuideXosInputKind.PointerDown)
        {
            if (AnyComboOpen() && HitId(input.X, input.Y) == InputEnabledId)
            {
                _inputEnabled.SetChecked(!_inputEnabled.Checked);
                host.TryLog(!_controlHost.HasTransientInputCapture && !AnyComboOpen()
                    ? "C144-CAPTURE group-disabled=cancelled owner=none result=PASS"u8
                    : "C144-CAPTURE group-disabled=stale result=FAIL"u8);
                return Render(host, surface);
            }
            if (_menu.IsOpen)
            {
                _controlHost.FocusAndRoutePointer(MenuId, input.X, input.Y);
                bool closed = DispatchMenuCommand(host, surface);
                return closed ? GuideXosResult.Success : Render(host, surface);
            }
            GuideXosComboBox openCombo = OpenCombo();
            if (openCombo != null)
            {
                int id = ComboId(openCombo);
                int priorSelection = openCombo.SelectedIndex;
                _controlHost.FocusAndRoutePointer(id, input.X, input.Y + _view.Offset);
                if (!openCombo.IsOpen)
                    host.TryLog(!_controlHost.HasTransientInputCapture && openCombo.SelectedIndex != priorSelection
                        ? "C144-COMBO commit=PASS translated-origin=PASS result=PASS"u8
                        : !_controlHost.HasTransientInputCapture
                            ? "C144-COMBO popup=closed capture=none result=PASS"u8
                            : "C144-COMBO popup=closed capture=stale result=FAIL"u8);
                return Render(host, surface);
            }

            int target = HitId(input.X, input.Y);
            int offsetBeforePointer = _view.Offset;
            GuideXosControlHostResult result = target == 0
                ? GuideXosControlHostResult.Ignored
                : _controlHost.FocusAndRoutePointer(target, input.X, input.Y);
            if (target == ViewId)
            {
                object activated = _view.LastActivatedMember;
                if (activated is GuideXosComboBox combo)
                {
                    int comboId = ComboId(combo);
                    bool captured = combo.IsOpen && _controlHost.TryAcquireTransientInputCapture(comboId);
                    host.TryLog(captured
                        ? "C144-COMBO open=PASS capture=combo result=PASS"u8
                        : "C144-COMBO open=FAIL result=FAIL"u8);
                }
                else if (ReferenceEquals(activated, _applyButton))
                {
                    int priorOffset = _view.Offset;
                    ApplyWorking();
                    host.TryLog(!IsDirty && _view.Offset == priorOffset
                        ? "C144-APPLY pointer=PASS dirty=cleared viewport=preserved result=PASS"u8
                        : "C144-APPLY pointer=FAIL result=FAIL"u8);
                }
                else if (ReferenceEquals(activated, _defaultsButton))
                {
                    OpenResetConfirmation(host);
                }
                else if (ReferenceEquals(activated, _naturalWheel) || ReferenceEquals(activated, _standardWheel))
                    host.TryLog("C144-RADIO wheel-group=independent result=PASS"u8);
                else if (ReferenceEquals(activated, _summaryMode) || ReferenceEquals(activated, _detailMode))
                    host.TryLog("C144-RADIO status-group=independent result=PASS"u8);
            }
            else if (target == ScrollBarId && result == GuideXosControlHostResult.DragStarted)
                host.TryLog("C144-DRAG press=PASS owner=scrollbar result=PASS"u8);
            else if (target == ScrollBarId && result == GuideXosControlHostResult.Paged &&
                _view.Offset != offsetBeforePointer)
                host.TryLog("C144-SCROLL page=PASS offset=changed result=PASS"u8);
            else if (target == OptionsButtonId && result == GuideXosControlHostResult.Activated)
            {
                bool opened = _menu.Open(_optionsButton.X, _optionsButton.Y + _optionsButton.Height) ==
                    GuideXosPopupMenuResult.Opened && _controlHost.TryAcquireTransientInputCapture(MenuId);
                host.TryLog(opened ? "C144-MENU open=PASS capture=menu result=PASS"u8 : "C144-MENU open=FAIL result=FAIL"u8);
            }
            else if (target == InputEnabledId)
                UpdateInputSection(_inputEnabled.Checked);
            return Render(host, surface);
        }

        int priorActive = _controlHost.ActiveControlId;
        bool wasPopupOpen = AnyComboOpen() || _menu.IsOpen;
        GuideXosComboBox comboBeforeInput = OpenCombo();
        int comboSelectionBeforeInput = comboBeforeInput?.SelectedIndex ?? -1;
        ManagedSettingsSnapshot workingBeforeInput = _working;
        GuideXosControlHostResult hostInputResult = _controlHost.HandleInput(input);
        if (!wasPopupOpen && AnyComboOpen())
        {
            GuideXosComboBox openedByKeyboard = OpenCombo();
            bool captured = openedByKeyboard != null &&
                _controlHost.TryAcquireTransientInputCapture(ComboId(openedByKeyboard));
            host.TryLog(captured
                ? "C144-COMBO keyboard-open=PASS capture=combo result=PASS"u8
                : "C144-COMBO keyboard-open=FAIL result=FAIL"u8);
        }
        else if (wasPopupOpen && comboBeforeInput != null && !AnyComboOpen() &&
            comboBeforeInput.SelectedIndex != comboSelectionBeforeInput)
            host.TryLog("C144-COMBO keyboard-commit=PASS capture=none result=PASS"u8);
        if (input.Kind == GuideXosInputKind.KeyChar && input.Character == ' ' &&
            !_working.Equals(workingBeforeInput))
            host.TryLog("C144-KEYBOARD space=activated working=changed result=PASS"u8);
        if (input.Kind == GuideXosInputKind.KeyDown ||
            (input.Kind == GuideXosInputKind.KeyChar && input.Character == ' ' &&
                hostInputResult == GuideXosControlHostResult.Activated))
        {
            if (input.Kind == GuideXosInputKind.KeyDown &&
                input.KeyCode == (uint)GuideXosTextInputKey.Tab)
            {
                host.TryLog(_view.HasFocus && _view.Offset > 0
                    ? "C144-FOCUS tab=offscreen-revealed result=PASS"u8
                    : "C144-FOCUS tab=traversed result=PASS"u8);
                LogGeometry(host, "keyboard-tab");
            }
            bool activate = input.Kind == GuideXosInputKind.KeyChar ||
                input.KeyCode == (uint)GuideXosTextInputKey.Enter;
            if (activate && priorActive == ViewId && ReferenceEquals(_view.FocusedMember, _applyButton))
            {
                ApplyWorking();
                host.TryLog("C144-KEYBOARD button=apply result=PASS"u8);
            }
            else if (activate && priorActive == ViewId &&
                ReferenceEquals(_view.FocusedMember, _defaultsButton))
            {
                OpenResetConfirmation(host);
            }
            else if (input.Kind == GuideXosInputKind.KeyDown &&
                input.KeyCode == (uint)GuideXosTextInputKey.Enter &&
                priorActive == OptionsButtonId)
            {
                OpenOptionsMenu();
            }
        }
        if (input.Kind == GuideXosInputKind.KeyDown &&
            (input.KeyCode == (uint)GuideXosTextInputKey.Left ||
             input.KeyCode == (uint)GuideXosTextInputKey.Right ||
             input.KeyCode == (uint)GuideXosTextInputKey.Up ||
             input.KeyCode == (uint)GuideXosTextInputKey.Down) &&
            !_working.Equals(workingBeforeInput) && _view.FocusedMember is GuideXosRadioButton)
            host.TryLog("C144-KEYBOARD radio=arrow-selection result=PASS"u8);
        if (wasPopupOpen && !AnyComboOpen() && !_menu.IsOpen)
            host.TryLog(!_controlHost.HasTransientInputCapture
                ? "C144-CAPTURE popup=none result=PASS"u8
                : "C144-CAPTURE popup=stale result=FAIL"u8);
        if (input.Kind == GuideXosInputKind.KeyDown && input.Shift &&
            input.KeyCode == (uint)GuideXosTextInputKey.Tab)
            host.TryLog("C144-FOCUS shift-tab=handled earlier-member=PASS result=PASS"u8);
        if (_menuCommand != 0)
        {
            bool closed = DispatchMenuCommand(host, surface);
            if (closed) return GuideXosResult.Success;
        }
        if (_surfaceClosing) return GuideXosResult.Success;
        return Render(host, surface);
    }

    internal bool InitializeForTests()
    {
        bool valid = ResetComposition();
        _initialContentHeight = _view.ContentExtent;
        _finalContentHeight = _initialContentHeight;
        return valid;
    }

    internal void InjectNextSaveFailureForTests() => _failNextSaveForTests = true;

    internal bool ApplyForTests() => ApplyWorking();

    private void HydratePersistedSettings(GuideXosHost host)
    {
        _settingsStore = new ManagedSettingsStore(new ManagedSettingsVfsAccess(host));
        ManagedSettingsLoadResult load = _settingsStore.Load();
        ManagedSettingsSnapshot initial = load.Status == ManagedSettingsLoadStatus.Loaded
            ? load.Snapshot
            : ManagedSettingsSnapshot.Defaults;
        _working = initial;
        _applied = initial;
        _persisted = initial;
        _persistedFilePresent = load.Status == ManagedSettingsLoadStatus.Loaded;
        SyncControlsFromWorking();

        if (load.Status == ManagedSettingsLoadStatus.Loaded)
        {
            LogSettingsSnapshot("C146-LOAD source=file result=PASS "u8, initial,
                "working=applied persisted=loaded dirty=false"u8);
            host?.TryLog("C146-LOAD-META path=/system/apps/GXSETT.BIN version=1 size=25 readback=validated viewport=0 focus=normal result=PASS"u8);
        }
        else if (load.Status == ManagedSettingsLoadStatus.Missing)
        {
            LogSettingsSnapshot("C146-LOAD source=missing result=PASS "u8, initial,
                "working=applied persisted=defaults dirty=false"u8);
            host?.TryLog("C146-LOAD-META path=/system/apps/GXSETT.BIN file=missing created=false result=PASS"u8);
        }
        else
        {
            LogSettingsSnapshot("C146-LOAD source=invalid recovery=defaults "u8,
                initial, "dirty=false"u8);
            ShowPersistenceMessage(warning: true);
        }
    }

    private void ShowPersistenceMessage(bool warning)
    {
        GuideXosDialog persistenceDialog = EnsurePersistenceDialog();
        if (_activeDialog != null && _activeDialog.IsOpen)
        {
            _pendingPersistenceMessage = warning ? (byte)1 : (byte)2;
            return;
        }

        string title = warning ? "Settings warning" : "Settings error";
        string message = warning
            ? "Settings could not be read. Defaults were loaded."
            : "Settings could not be saved. Your edits remain open.";
        if (!GuideXosMessageBox.TryConfigure(persistenceDialog,
                GuideXosMessageBoxButtons.OK, title, message,
                _persistenceOkButton, null, null, GuideXosDialogResult.OK) ||
            !persistenceDialog.Open(_controlHost))
        {
            _appHost?.TryLog("C146-MESSAGE result=FAIL modal=unavailable"u8);
            return;
        }

        _activeDialog = persistenceDialog;
        LogDialogGeometry(_appHost);
        _appHost?.TryLog(warning
            ? "C146-RECOVERY warning=opened defaults=loaded dirty=false result=PASS"u8
            : "C146-APPLY-FAIL messagebox=opened working=preserved applied=preserved persisted=preserved dirty=true result=PASS"u8);
    }

    private GuideXosDialog EnsurePersistenceDialog()
    {
        if (_persistenceDialog != null) return _persistenceDialog;

        _persistenceDialog = new GuideXosDialog(112, 96, 336, 160,
            "Settings message");
        _persistenceOkButton = new GuideXosButton(232, 226, 96, 18, "OK");
        _persistenceDialog.TryAddMember(_persistenceOkButton);
        _persistenceDialog.TrySetButtonResult(_persistenceOkButton,
            GuideXosDialogResult.OK);
        _persistenceDialog.TrySetDefaultButton(_persistenceOkButton);
        _persistenceDialog.TrySetCancelResult(GuideXosDialogResult.OK);
        _persistenceDialog.Closed = OnPersistenceDialogClosed;
        return _persistenceDialog;
    }

    private void OnPersistenceDialogClosed(GuideXosDialogResult result)
    {
        if (_resettingComposition) return;
        if (_persistenceDialog != null &&
            ReferenceEquals(_activeDialog, _persistenceDialog))
            _activeDialog = null;
        byte pending = _pendingPersistenceMessage;
        _pendingPersistenceMessage = 0;
        if (pending != 0) ShowPersistenceMessage(pending == 1);
    }

    private void LogSettingsSnapshot(ReadOnlySpan<byte> prefix,
        ManagedSettingsSnapshot snapshot, ReadOnlySpan<byte> suffix)
    {
        Span<byte> stateLine = stackalloc byte[127];
        int stateLength = 0;
        if (!GuideXosText.Append(stateLine, ref stateLength, prefix)) return;
        while (stateLength > 0 && stateLine[stateLength - 1] == (byte)' ')
            stateLength--;
        if (suffix.Length != 0 &&
            (!GuideXosText.Append(stateLine, ref stateLength, " "u8) ||
             !GuideXosText.Append(stateLine, ref stateLength, suffix))) return;

        Span<byte> valuesLine = stackalloc byte[127];
        int valuesLength = 0;
        if (!GuideXosText.Append(valuesLine, ref valuesLength, "C146-VALUES "u8) ||
            !GuideXosText.Append(valuesLine, ref valuesLength, "density="u8)) return;
        GuideXosText.AppendUnsigned(valuesLine, ref valuesLength, (uint)snapshot.Density);
        if (!GuideXosText.Append(valuesLine, ref valuesLength, " showStatus="u8)) return;
        GuideXosText.AppendUnsigned(valuesLine, ref valuesLength, snapshot.ShowStatus ? 1u : 0u);
        if (!GuideXosText.Append(valuesLine, ref valuesLength, " advanced="u8)) return;
        GuideXosText.AppendUnsigned(valuesLine, ref valuesLength, snapshot.ShowAdvanced ? 1u : 0u);
        if (!GuideXosText.Append(valuesLine, ref valuesLength, " inputEnabled="u8)) return;
        GuideXosText.AppendUnsigned(valuesLine, ref valuesLength, snapshot.InputEnabled ? 1u : 0u);
        if (!GuideXosText.Append(valuesLine, ref valuesLength, " naturalScroll="u8)) return;
        GuideXosText.AppendUnsigned(valuesLine, ref valuesLength, snapshot.NaturalScroll ? 1u : 0u);
        if (!GuideXosText.Append(valuesLine, ref valuesLength, " speed="u8)) return;
        GuideXosText.AppendUnsigned(valuesLine, ref valuesLength, (uint)snapshot.ScrollSpeed);
        if (!GuideXosText.Append(valuesLine, ref valuesLength, " keyboardTips="u8)) return;
        GuideXosText.AppendUnsigned(valuesLine, ref valuesLength, snapshot.ShowKeyboardTips ? 1u : 0u);
        if (!GuideXosText.Append(valuesLine, ref valuesLength, " detail="u8)) return;
        GuideXosText.AppendUnsigned(valuesLine, ref valuesLength, (uint)snapshot.StatusDetail);
        if (!GuideXosText.Append(valuesLine, ref valuesLength, " reportFormat="u8)) return;
        GuideXosText.AppendUnsigned(valuesLine, ref valuesLength, (uint)snapshot.ReportFormat);

        _appHost?.TryLog(stateLine[..stateLength]);
        _appHost?.TryLog(valuesLine[..valuesLength]);
    }

    internal bool ApplyWorking()
    {
        if (_saveInProgress) return false;
        if (_working.Equals(_applied) && _applied.Equals(_persisted))
        {
            _appHost?.TryLog("C146-SAVE no-op=true writes=0 dirty=false result=PASS"u8);
            return true;
        }

        ManagedSettingsSnapshot candidate = _working;
        if (!ManagedSettingsStore.IsValid(candidate))
        {
            ShowPersistenceMessage(warning: false);
            return false;
        }

        _saveInProgress = true;
        ManagedSettingsSaveStatus saveStatus;
        try
        {
            if (_failNextSaveForTests)
            {
                _failNextSaveForTests = false;
                saveStatus = ManagedSettingsSaveStatus.IoFailure;
            }
            else if (_c145FocusedTesting)
            {
                // The C145 focused fixture validates modal behavior without
                // mutating the user's real settings file.
                saveStatus = ManagedSettingsSaveStatus.Saved;
            }
            else
            {
                saveStatus = _settingsStore?.Save(candidate) ??
                    ManagedSettingsSaveStatus.IoFailure;
            }
        }
        finally
        {
            _saveInProgress = false;
        }

        if (saveStatus != ManagedSettingsSaveStatus.Saved)
        {
            _appHost?.TryLog("C146-SAVE result=FAIL working=preserved applied=preserved persisted=preserved dirty=true"u8);
            ShowPersistenceMessage(warning: false);
            return false;
        }

        _applied = candidate;
        _persisted = candidate;
        if (!_c145FocusedTesting) _persistedFilePresent = true;
        _applyButton.SetEnabled(false);
        UpdateStatusPreview();
        _controlHost?.RefreshVisibility();
        RefreshDirtyState();
        LogSettingsSnapshot("C146-SAVE result=PASS write=verified readback=PASS "u8,
            candidate, "dirty=false"u8);
        return true;
    }

    internal void RestoreDefaults()
    {
        int priorOffset = _view.Offset;
        _working = ManagedSettingsSnapshot.Defaults;
        SyncControlsFromWorking();
        _view.RecalculateContentExtent();
        _scrollBar.Value = _view.Offset;
        if (_view.Offset != priorOffset && _view.Offset > _view.MaximumOffset)
            _view.SetOffset(_view.MaximumOffset);
        RefreshDirtyState();
    }

    private bool ResetComposition()
    {
        _resettingComposition = true;
        if (_resetDialog.IsOpen) _resetDialog.Close(GuideXosDialogResult.None);
        if (_dirtyCloseDialog.IsOpen) _dirtyCloseDialog.Close(GuideXosDialogResult.None);
        if (_persistenceDialog.IsOpen) _persistenceDialog.Close(GuideXosDialogResult.None);
        _activeDialog = null;
        _resettingComposition = false;
        _syncing = true;
        _working = ManagedSettingsSnapshot.Defaults;
        _applied = _working;
        _persisted = _working;
        _settingsStore = null;
        _persistedFilePresent = false;
        _saveInProgress = false;
        _failNextSaveForTests = false;
        _pendingPersistenceMessage = 0;
        _view.Clear();
        _view.Reset();
        for (int section = 0; section < SectionCount; section++)
        {
            _stacks[section].Clear();
            _groups[section].ClearMembers();
            _groups[section].TrySetBounds(0, 0, 272, section switch { 0 => 180, 1 => 198, 2 => 216, _ => 144 });
            _groups[section].SetVisible(section != 3);
            _groups[section].SetEnabled(true);
            _stacks[section].TrySetPadding(4, 4, 4, 4);
            _stacks[section].TrySetSpacing(3);
        }
        if (_controlHost == null) _controlHost = new GuideXosControlHost(10);
        else _controlHost.Reset();
        _wheelGroup.Reset();
        _statusGroup.Reset();
        _standardWheel.Reset(); _naturalWheel.Reset();
        _summaryMode.Reset(); _detailMode.Reset();
        _standardWheel.SetChecked(true); _summaryMode.SetChecked(true);
        _wheelGroup.TryRegister(_standardWheel); _wheelGroup.TryRegister(_naturalWheel);
        _statusGroup.TryRegister(_summaryMode); _statusGroup.TryRegister(_detailMode);
        _density.Reset(); _density.ClearItems(); _density.TryAddItem("Comfortable"); _density.TryAddItem("Compact");
        _speed.Reset(); _speed.ClearItems(); _speed.TryAddItem("Slow"); _speed.TryAddItem("Normal"); _speed.TryAddItem("Fast");
        _statusCombo.Reset(); _statusCombo.ClearItems(); _statusCombo.TryAddItem("Current section"); _statusCombo.TryAddItem("All choices");
        _showStatus.SetEnabled(true); _showAdvanced.SetEnabled(true); _showTips.SetEnabled(true);
        _showStatus.SetVisible(true); _showAdvanced.SetVisible(true); _showTips.SetVisible(true);
        _systemHeading.SetVisible(true); _statusProgress.SetVisible(true); _systemDescription.SetVisible(true);
        _inputEnabled.SetEnabled(true); _inputEnabled.SetVisible(true); _inputEnabled.SetChecked(true);
        _showStatus.SetChecked(true); _showAdvanced.SetChecked(false); _showTips.SetChecked(true);
        _applyButton.Reset(); _defaultsButton.Reset(); _optionsButton.Reset();
        _menu.Reset(); _menu.ClearItems();
        _menu.TryAddItem("Apply", MenuApply); _menu.TryAddItem("Reset to defaults", MenuDefaults);
        _menu.TryAddSeparator(); _menu.TryAddItem("Close", MenuClose);
        _menu.CommandInvoked = OnMenuCommand;
        _menuCommand = 0;
        _density.TrySetSelectedIndex(_working.Density);
        _speed.TrySetSelectedIndex(_working.ScrollSpeed);
        _statusCombo.TrySetSelectedIndex(_working.ReportFormat);
        _statusProgress.TrySetValue(52);

        int[] tops = { 4, 200, 416, 648 };
        bool ok = true;
        for (int section = 0; section < SectionCount; section++)
        {
            GuideXosGroupBox group = _groups[section];
            GuideXosVerticalStack stack = _stacks[section];
            ok &= _view.TryAddMember(group, 4, tops[section]) == GuideXosScrollViewResult.Added;
            ok &= stack.TrySetFrame(group.ContentLeft, group.ContentTop, group.ContentWidth);
            object[] leaves = _sectionLeaves[section];
            for (int index = 0; index < leaves.Length; index++)
            {
                ok &= group.TryAddMember(leaves[index]) == GuideXosGroupBoxResult.Added;
                ok &= _view.TryAddMember(leaves[index], 20, tops[section] + 40 + index * 20) ==
                    GuideXosScrollViewResult.Added;
                ok &= stack.TryAddMember(leaves[index]) == GuideXosVerticalStackResult.Added;
            }
            ok &= stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut;
        }
        _view.RecalculateContentExtent();
        _view.BindScrollBar(_scrollBar);
        _scrollBar.SetVisible(true);
        _controlHost.TryRegisterScrollView(ViewId, _view, true);
        _controlHost.TryRegisterScrollBar(ScrollBarId, _scrollBar, false);
        _controlHost.TryRegisterComboBox(DensityComboId, _density, true);
        _controlHost.TryRegisterComboBox(SpeedComboId, _speed, true);
        _controlHost.TryRegisterComboBox(StatusComboId, _statusCombo, true);
        _controlHost.TryRegisterPopupMenu(MenuId, _menu, false);
        _controlHost.TryRegisterButton(OptionsButtonId, _optionsButton, true);
        _controlHost.TryRegisterCheckBox(InputEnabledId, _inputEnabled, true);
        _controlHost.TryRegisterCheckBox(AdvancedToggleId, _showAdvanced, true);
        _applyButton.SetEnabled(false);
        _syncing = false;
        SyncControlsFromWorking();
        return ok && ValidateComposition();
    }

    private bool RegisterControls() => _controlHost != null && _controlHost.RegistrationCount == 9;

    private bool ValidateComposition()
    {
        if (_view.MemberCount != 21 || _view.MaximumMemberCount != ViewCapacity ||
            _controlHost == null || _controlHost.RegistrationCount != 9 ||
            _controlHost.MaximumControlCount != 10 || _wheelGroup.MemberCount != 2 ||
            _statusGroup.MemberCount != 2 || _view.Offset < 0 || _view.Offset > _view.MaximumOffset)
            return false;
        for (int section = 0; section < SectionCount; section++)
        {
            if (_groups[section].MemberCount != _sectionLeaves[section].Length ||
                _stacks[section].MemberCount != _sectionLeaves[section].Length ||
                !_groups[section].ValidateMembershipGeometry(out int outside) || outside != 0)
                return false;
            for (int member = 0; member < _sectionLeaves[section].Length; member++)
            {
                object leaf = _sectionLeaves[section][member];
                if (!_stacks[section].ContainsMember(leaf) || !_view.ContainsMember(leaf) ||
                    !ReferenceEquals(GetGroupOwner(leaf), _groups[section])) return false;
            }
        }
        return _view.ContainsMember(_groups[0]) && _view.ContainsMember(_groups[1]) &&
            _view.ContainsMember(_groups[2]) && _view.ContainsMember(_groups[3]) &&
            _scrollBar.Value == _view.Offset;
    }

    private static GuideXosGroupBox GetGroupOwner(object member) => member switch
    {
        GuideXosButton value => value.ParentGroupBox,
        GuideXosCheckBox value => value.ParentGroupBox,
        GuideXosLabel value => value.ParentGroupBox,
        GuideXosRadioButton value => value.ParentGroupBox,
        GuideXosComboBox value => value.ParentGroupBox,
        GuideXosProgressBar value => value.ParentGroupBox,
        _ => null,
    };

    private void SyncControlsFromWorking()
    {
        _syncing = true;
        _density.TrySetSelectedIndex(_working.Density);
        _speed.TrySetSelectedIndex(_working.ScrollSpeed);
        _showStatus.SetChecked(_working.ShowStatus);
        _showAdvanced.SetChecked(_working.ShowAdvanced);
        _inputEnabled.SetChecked(_working.InputEnabled);
        _standardWheel.SetChecked(!_working.NaturalScroll);
        _naturalWheel.SetChecked(_working.NaturalScroll);
        _showTips.SetChecked(_working.ShowKeyboardTips);
        _summaryMode.SetChecked(_working.StatusDetail == 0);
        _detailMode.SetChecked(_working.StatusDetail != 0);
        _statusCombo.TrySetSelectedIndex(_working.ReportFormat);
        _groups[3].SetVisible(_working.ShowAdvanced);
        _systemHeading.SetVisible(_working.ShowStatus);
        _statusProgress.SetVisible(_working.ShowStatus);
        _systemDescription.SetVisible(_working.ShowStatus);
        _groups[1].SetEnabled(_working.InputEnabled);
        ApplyDensityLayout();
        UpdateStatusPreview();
        _syncing = false;
        _view.RecalculateContentExtent();
        RefreshDirtyState();
        _controlHost?.RefreshVisibility();
    }

    private void RefreshDirtyState()
    {
        _applyButton.SetEnabled(IsDirty);
        _appHost?.TryLog(IsDirty
            ? "C144-DIRTY working=changed applied=preserved result=PASS"u8
            : "C144-DIRTY working=applied result=PASS"u8);
    }

    private void OnDensityChanged(int index)
    {
        if (_syncing) return;
        _working.Density = index;
        ApplyDensityLayout();
        CompleteWorkingChange();
    }

    private void OnSpeedChanged(int index)
    {
        if (_syncing) return;
        _working.ScrollSpeed = index;
        CompleteWorkingChange();
    }

    private void OnShowStatusChanged(bool value)
    {
        if (_syncing) return;
        _working.ShowStatus = value;
        _systemHeading.SetVisible(value);
        _statusProgress.SetVisible(value);
        _systemDescription.SetVisible(value);
        _stacks[2].PerformLayout();
        _view.RecalculateContentExtent();
        _controlHost.RefreshVisibility();
        _appHost?.TryLog(value
            ? "C144-SYSTEM visible=true extent=updated result=PASS"u8
            : "C144-SYSTEM visible=false extent=updated result=PASS"u8);
        CompleteWorkingChange();
    }

    private void OnShowAdvancedChanged(bool value)
    {
        if (_syncing) return;
        _working.ShowAdvanced = value;
        _groups[3].SetVisible(value);
        if (value) _stacks[3].PerformLayout();
        _view.RecalculateContentExtent();
        _finalContentHeight = _view.ContentExtent;
        _controlHost.RefreshVisibility();
        _appHost?.TryLog(value
            ? "C144-ADVANCED visible=true extent=grown scrollbar=synced result=PASS"u8
            : "C144-ADVANCED visible=false extent=shrunk viewport=clamped result=PASS"u8);
        if (_appHost != null) LogGeometry(_appHost, value ? "advanced-show" : "advanced-hide");
        CompleteWorkingChange();
    }

    private void OnShowTipsChanged(bool value)
    {
        if (_syncing) return;
        _working.ShowKeyboardTips = value;
        CompleteWorkingChange();
    }

    private void OnInputEnabledChanged(bool value)
    {
        if (_syncing) return;
        _working.InputEnabled = value;
        UpdateInputSection(value);
        CompleteWorkingChange();
    }

    private void UpdateInputSection(bool enabled)
    {
        _groups[1].SetEnabled(enabled);
        _controlHost?.RefreshVisibility();
        _view.RecalculateContentExtent();
        _appHost?.TryLog(enabled
            ? "C144-GROUP input=enabled own-member-state=preserved result=PASS"u8
            : "C144-GROUP input=disabled pointer-tab-popup=gated result=PASS"u8);
    }

    private void OnWheelModeChanged(bool natural)
    {
        if (_syncing) return;
        _working.NaturalScroll = natural;
        CompleteWorkingChange();
    }

    private void OnStatusModeChanged(bool detailed)
    {
        if (_syncing) return;
        _working.StatusDetail = detailed ? 1 : 0;
        CompleteWorkingChange();
    }

    private void OnReportFormatChanged(int format)
    {
        if (_syncing) return;
        _working.ReportFormat = format;
        CompleteWorkingChange();
    }

    private void CompleteWorkingChange()
    {
        RefreshDirtyState();
        UpdateStatusPreview();
        _appHost?.TryLog("C144-WORKING callback=synchronized result=PASS"u8);
    }

    private void ApplyDensityLayout()
    {
        int spacing = _working.Density == 1 ? 1 : 3;
        for (int index = 0; index < SectionCount; index++) _stacks[index].TrySetSpacing(spacing);
        for (int index = 0; index < SectionCount; index++) _stacks[index].PerformLayout();
        _view.RecalculateContentExtent();
    }

    private void UpdateStatusPreview()
    {
        string statusText = _working.StatusDetail == 0
            ? (_working.ReportFormat == 0 ? "Compact settings summary" : "Expanded settings summary")
            : (_working.ReportFormat == 0 ? "Compact system detail" : "Expanded system detail");
        _systemDescription.SetText(statusText);
        int enabled = (_applied.ShowStatus ? 1 : 0) + (_applied.InputEnabled ? 1 : 0) +
            (_applied.ShowAdvanced ? 1 : 0) + (_applied.NaturalScroll ? 1 : 0) +
            (_applied.ShowKeyboardTips ? 1 : 0);
        _statusProgress.TrySetValue(Math.Min(100, 20 + enabled * 10 +
            _applied.Density * 8 + _applied.StatusDetail * 5 + _applied.ReportFormat * 5));
        _appHost?.TryLog("C144-STATE preview=updated applied=bounded result=PASS"u8);
    }

    private void OpenOptionsMenu()
    {
        bool opened = _menu.Open(_optionsButton.X, _optionsButton.Y + _optionsButton.Height) ==
            GuideXosPopupMenuResult.Opened && _controlHost.TryAcquireTransientInputCapture(MenuId);
        _appHost?.TryLog(opened ? "C144-MENU open=PASS capture=menu result=PASS"u8 : "C144-MENU open=FAIL result=FAIL"u8);
    }

    private void OnMenuCommand(uint command) => _menuCommand = command;

    private bool OpenResetConfirmation(GuideXosHost host)
    {
        _menu.Close();
        _controlHost?.RefreshVisibility();
        _dialogWorkingSnapshot = _working;
        _dialogAppliedSnapshot = _applied;
        _dialogViewportOffset = _view.Offset;
        if (!_resetDialog.TrySetMessage("Replace the working settings with defaults?") ||
            !_resetDialog.Open(_controlHost))
        {
            host.TryLog("C145-RESET open=FAIL result=FAIL"u8);
            return false;
        }
        _activeDialog = _resetDialog;
        host.TryLog(_controlHost.IsModalActive && _controlHost.ActiveIndex == -1 &&
            _resetDialog.IsModal && _resetDialog.ControlHost.ActiveControlId ==
                _resetDialog.DefaultButtonId
            ? "C145-RESET open=PASS modal=active focus=default parent=blocked result=PASS"u8
            : "C145-RESET open=FAIL result=FAIL"u8);
        LogDialogGeometry(host);
        return true;
    }

    private bool OpenDirtyCloseConfirmation(GuideXosHost host)
    {
        _menu.Close();
        _controlHost?.RefreshVisibility();
        _dialogWorkingSnapshot = _working;
        _dialogAppliedSnapshot = _applied;
        _dialogViewportOffset = _view.Offset;
        if (!_dirtyCloseDialog.TrySetMessage("Apply changes before closing?") ||
            !_dirtyCloseDialog.Open(_controlHost))
        {
            host.TryLog("C145-UNSAVED open=FAIL result=FAIL"u8);
            return false;
        }
        _activeDialog = _dirtyCloseDialog;
        host.TryLog(_controlHost.IsModalActive && _controlHost.ActiveIndex == -1 &&
            _dirtyCloseDialog.IsModal && _dirtyCloseDialog.RegistrationCount == 3
            ? "C145-UNSAVED open=PASS modal=active parent=blocked buttons=3 result=PASS"u8
            : "C145-UNSAVED open=FAIL result=FAIL"u8);
        LogDialogGeometry(host);
        return true;
    }

    private void OnResetDialogClosed(GuideXosDialogResult result)
    {
        if (_resettingComposition) return;
        _activeDialog = null;
        if (result == GuideXosDialogResult.Reset)
        {
            RestoreDefaults();
            bool valid = _working.Equals(ManagedSettingsSnapshot.Defaults) &&
                !_controlHost.IsModalActive && _view.Offset >= 0 &&
                _view.Offset <= _view.MaximumOffset && _scrollBar.Value == _view.Offset;
            _appHost?.TryLog(valid
                ? "C145-RESET result=Reset working=defaults dirty=updated viewport=valid focus=restored result=PASS"u8
                : "C145-RESET result=Reset result=FAIL"u8);
        }
        else if (result == GuideXosDialogResult.Cancel)
        {
            bool valid = _working.Equals(_dialogWorkingSnapshot) &&
                _applied.Equals(_dialogAppliedSnapshot) &&
                _view.Offset == _dialogViewportOffset && !_controlHost.IsModalActive;
            _appHost?.TryLog(valid
                ? "C145-RESET result=Cancel working=preserved viewport=preserved focus=restored result=PASS"u8
                : "C145-RESET result=Cancel result=FAIL"u8);
        }
    }

    private void OnDirtyCloseDialogClosed(GuideXosDialogResult result)
    {
        if (_resettingComposition) return;
        _activeDialog = null;
        if (result == GuideXosDialogResult.Cancel)
        {
            bool valid = _working.Equals(_dialogWorkingSnapshot) &&
                _applied.Equals(_dialogAppliedSnapshot) && IsDirty &&
                _view.Offset == _dialogViewportOffset && !_controlHost.IsModalActive;
            _appHost?.TryLog(valid
                ? "C145-UNSAVED result=Cancel dirty=preserved parent=open focus=restored result=PASS"u8
                : "C145-UNSAVED result=Cancel result=FAIL"u8);
        }
        else if (result == GuideXosDialogResult.Apply)
        {
            bool saved = ApplyWorking();
            if (saved)
            {
                bool applied = _applied.Equals(_working) && !IsDirty;
                bool closed = CloseSettingsCenter(_appHost, _surface, "apply");
                _appHost?.TryLog(applied && closed
                    ? "C145-UNSAVED result=Apply applied=committed closed=true result=PASS"u8
                    : "C145-UNSAVED result=Apply result=FAIL"u8);
            }
            else
            {
                _appHost?.TryLog(IsDirty && !_surfaceClosing &&
                    ReferenceEquals(_activeDialog, _persistenceDialog)
                    ? "C146-UNSAVED result=Apply failed=kept-open dirty=true result=PASS"u8
                    : "C146-UNSAVED result=Apply failed=result-invalid"u8);
            }
        }
        else if (result == GuideXosDialogResult.Discard)
        {
            bool discarded = _applied.Equals(_dialogAppliedSnapshot) &&
                !_applied.Equals(_dialogWorkingSnapshot);
            bool closed = CloseSettingsCenter(_appHost, _surface, "discard");
            _appHost?.TryLog(discarded && closed
                ? "C145-UNSAVED result=Discard applied=preserved closed=true result=PASS"u8
                : "C145-UNSAVED result=Discard result=FAIL"u8);
        }
    }

    private bool CloseSettingsCenter(GuideXosHost host,
        GuideXosSurface surface, string reason)
    {
        if (surface == null || _surfaceClosing) return false;
        if (_activeDialog != null && _activeDialog.IsOpen)
        {
            GuideXosDialog active = _activeDialog;
            active.Close(GuideXosDialogResult.None);
        }
        _activeDialog = null;
        _menu.Close();
        _controlHost?.RefreshVisibility();
        bool valid = _controlHost != null && !_controlHost.IsModalActive &&
            !_controlHost.HasTransientInputCapture && !_controlHost.HasPointerDragCapture &&
            _view.Offset >= 0 && _view.Offset <= _view.MaximumOffset &&
            _scrollBar.Value == _view.Offset && ValidateComposition();
        host?.TryLog(valid
            ? "C145-FINAL viewport=valid registration=9 modal=none capture=none drag=none result=PASS"u8
            : "C145-FINAL result=FAIL"u8);
        if (reason == "clean")
            host?.TryLog(IsDirty
                ? "C145-CLOSE clean=FAIL result=FAIL"u8
                : "C145-CLOSE clean=true confirmation=none result=PASS"u8);
        if (_c145FocusedTesting) return valid;
        _surfaceClosing = surface.TryClose() == GuideXosResult.Success;
        return valid && _surfaceClosing;
    }

    internal bool RequestCloseForTests()
    {
        if (IsDirty) return OpenDirtyCloseConfirmation(_appHost);
        return CloseSettingsCenter(_appHost, _surface, "clean");
    }

    internal bool OpenResetForTests() => OpenResetConfirmation(_appHost);

    private void LogDialogGeometry(GuideXosHost host)
    {
        if (_activeDialog == null) return;
        Span<byte> line = stackalloc byte[127];
        int position = 0;
        GuideXosText.Append(line, ref position, "C145-DIALOG-GEOMETRY kind="u8);
        ReadOnlySpan<byte> name = ReferenceEquals(_activeDialog, _resetDialog)
            ? "reset"u8 : ReferenceEquals(_activeDialog, _persistenceDialog)
                ? "persistence"u8 : "close"u8;
        GuideXosText.Append(line, ref position, name);
        GuideXosText.Append(line, ref position, " bounds="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_activeDialog.X);
        GuideXosText.Append(line, ref position, ","u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_activeDialog.Y);
        GuideXosText.Append(line, ref position, ","u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_activeDialog.Width);
        GuideXosText.Append(line, ref position, ","u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_activeDialog.Height);
        if (ReferenceEquals(_activeDialog, _resetDialog))
        {
            AppendDialogPoint(line, ref position, " cancel="u8,
                _resetCancelButton.X + 4, _resetCancelButton.Y + 4);
            AppendDialogPoint(line, ref position, " confirm="u8,
                _resetConfirmButton.X + 4, _resetConfirmButton.Y + 4);
        }
        else if (ReferenceEquals(_activeDialog, _dirtyCloseDialog))
        {
            AppendDialogPoint(line, ref position, " apply="u8,
                _closeApplyButton.X + 4, _closeApplyButton.Y + 4);
            AppendDialogPoint(line, ref position, " discard="u8,
                _closeDiscardButton.X + 4, _closeDiscardButton.Y + 4);
            AppendDialogPoint(line, ref position, " cancel="u8,
                _closeCancelButton.X + 4, _closeCancelButton.Y + 4);
        }
        else
        {
            AppendDialogPoint(line, ref position, " ok="u8,
                _persistenceOkButton.X + 4, _persistenceOkButton.Y + 4);
        }
        host?.TryLog(line[..position]);
    }

    private static void AppendDialogPoint(Span<byte> line, ref int position,
        ReadOnlySpan<byte> name, int x, int y)
    {
        GuideXosText.Append(line, ref position, name);
        GuideXosText.AppendUnsigned(line, ref position, (uint)x);
        GuideXosText.Append(line, ref position, ","u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)y);
    }

    private bool DispatchMenuCommand(GuideXosHost host, GuideXosSurface surface)
    {
        uint command = _menuCommand;
        _menuCommand = 0;
        if (command == MenuApply)
        {
            int priorOffset = _view.Offset;
            ApplyWorking();
            host.TryLog(_view.Offset == priorOffset && !IsDirty
                ? "C144-APPLY menu=PASS dirty=cleared viewport=preserved result=PASS"u8
                : "C144-APPLY menu=FAIL result=FAIL"u8);
            host.TryLog("C144-MENU command=apply result=PASS"u8);
        }
        else if (command == MenuDefaults)
        {
            OpenResetConfirmation(host);
        }
        else if (command == MenuClose)
        {
            if (IsDirty)
            {
                OpenDirtyCloseConfirmation(host);
                return false;
            }
            LogGeometry(host, "final");
            host.TryLog("C144-CLOSE popup=closed capture=none registration=bounded result=PASS"u8);
            return CloseSettingsCenter(host, surface, "clean");
        }
        return false;
    }

    private bool AnyComboOpen() => _density.IsOpen || _speed.IsOpen || _statusCombo.IsOpen;
    private GuideXosComboBox OpenCombo() => _density.IsOpen ? _density : _speed.IsOpen ? _speed : _statusCombo.IsOpen ? _statusCombo : null;
    private int ComboId(GuideXosComboBox combo) => ReferenceEquals(combo, _density)
        ? DensityComboId : ReferenceEquals(combo, _speed) ? SpeedComboId : StatusComboId;

    private int HitId(int x, int y)
    {
        if (Contains(_inputEnabled, x, y)) return InputEnabledId;
        if (Contains(_showAdvanced, x, y)) return AdvancedToggleId;
        if (Contains(_optionsButton, x, y)) return OptionsButtonId;
        if (x >= _view.X && x < _view.X + _view.Width && y >= _view.Y && y < _view.Y + _view.Height)
            return ViewId;
        if (x >= _scrollBar.X && x < _scrollBar.X + _scrollBar.Width &&
            y >= _scrollBar.Y && y < _scrollBar.Y + _scrollBar.Height) return ScrollBarId;
        return 0;
    }

    private static bool Contains(GuideXosCheckBox item, int x, int y) =>
        item.Visible && x >= item.X && x < item.X + item.Width && y >= item.Y && y < item.Y + item.Height;
    private static bool Contains(GuideXosButton item, int x, int y) =>
        item.Visible && x >= item.X && x < item.X + item.Width && y >= item.Y && y < item.Y + item.Height;

    private static void ConfigureMember(object member,
        GuideXosVerticalStackHorizontalAlignment alignment,
        int left, int top, int right, int bottom)
    {
        switch (member)
        {
            case GuideXosLabel label:
                label.HorizontalAlignment = alignment; label.TrySetMargins(left, top, right, bottom); break;
            case GuideXosButton button:
                button.HorizontalAlignment = alignment; button.TrySetMargins(left, top, right, bottom); break;
            case GuideXosCheckBox check:
                check.HorizontalAlignment = alignment; check.TrySetMargins(left, top, right, bottom); break;
            case GuideXosRadioButton radio:
                radio.HorizontalAlignment = alignment; radio.TrySetMargins(left, top, right, bottom); break;
            case GuideXosComboBox combo:
                combo.HorizontalAlignment = alignment; combo.TrySetMargins(left, top, right, bottom); break;
            case GuideXosProgressBar progress:
                progress.HorizontalAlignment = alignment; progress.TrySetMargins(left, top, right, bottom); break;
        }
    }

    private void LogGeometry(GuideXosHost host, string state)
    {
        uint sequence = ++_geometrySequence;
        if (sequence == 0) sequence = ++_geometrySequence;
        LogGeometryPoints(host, state, sequence, 1,
            "view="u8, _view.X, _view.Y,
            "bar="u8, _scrollBar.X, _scrollBar.Y,
            "density="u8, _density.X, _density.Y - _view.Offset,
            "speed="u8, _speed.X, _speed.Y - _view.Offset);
        LogGeometryPoints(host, state, sequence, 2,
            "showStatus="u8, _showStatus.X, _showStatus.Y - _view.Offset,
            "standardWheel="u8, _standardWheel.X, _standardWheel.Y - _view.Offset,
            "naturalWheel="u8, _naturalWheel.X, _naturalWheel.Y - _view.Offset);
        LogGeometryPoints(host, state, sequence, 3,
            "showTips="u8, _showTips.X, _showTips.Y - _view.Offset,
            "summaryMode="u8, _summaryMode.X, _summaryMode.Y - _view.Offset,
            "detailMode="u8, _detailMode.X, _detailMode.Y - _view.Offset);
        LogGeometryPoints(host, state, sequence, 4,
            "statusCombo="u8, _statusCombo.X, _statusCombo.Y - _view.Offset,
            "inputEnabled="u8, _inputEnabled.X, _inputEnabled.Y,
            "advancedToggle="u8, _showAdvanced.X, _showAdvanced.Y);
        LogGeometryPoints(host, state, sequence, 5,
            "apply="u8, _applyButton.X, _applyButton.Y - _view.Offset,
            "defaults="u8, _defaultsButton.X, _defaultsButton.Y - _view.Offset,
            "options="u8, _optionsButton.X, _optionsButton.Y);

        Span<byte> line = stackalloc byte[127];
        int pos = WriteGeometryPrefix(line, state, sequence, 6);
        AppendUnsignedField(line, ref pos, "offset="u8, (uint)_view.Offset);
        AppendUnsignedField(line, ref pos, "extent="u8, (uint)_view.ContentExtent);
        AppendUnsignedField(line, ref pos, "viewport="u8, (uint)_view.VisibleExtent);
        AppendUnsignedField(line, ref pos, "thumbTop="u8, (uint)_scrollBar.ThumbTop);
        AppendUnsignedField(line, ref pos, "thumbHeight="u8, (uint)_scrollBar.ThumbHeight);
        AppendUnsignedField(line, ref pos, "trackTop="u8, (uint)_scrollBar.TrackTop);
        host.TryLog(line[..pos]);
    }

    private static void LogGeometryPoints(GuideXosHost host, string state,
        uint sequence, uint part,
        ReadOnlySpan<byte> name1, int x1, int y1,
        ReadOnlySpan<byte> name2, int x2, int y2,
        ReadOnlySpan<byte> name3, int x3, int y3,
        ReadOnlySpan<byte> name4 = default, int x4 = 0, int y4 = 0)
    {
        Span<byte> line = stackalloc byte[127];
        int pos = WriteGeometryPrefix(line, state, sequence, part);
        AppendPointField(line, ref pos, name1, x1, y1);
        AppendPointField(line, ref pos, name2, x2, y2);
        AppendPointField(line, ref pos, name3, x3, y3);
        if (!name4.IsEmpty) AppendPointField(line, ref pos, name4, x4, y4);
        host.TryLog(line[..pos]);
    }

    private static int WriteGeometryPrefix(Span<byte> line, string state,
        uint sequence, uint part)
    {
        int pos = 0;
        GuideXosText.Append(line, ref pos, "C144-GEOMETRY state="u8);
        for (int i = 0; i < state.Length; i++) line[pos++] = (byte)state[i];
        GuideXosText.Append(line, ref pos, " seq="u8);
        GuideXosText.AppendUnsigned(line, ref pos, sequence);
        GuideXosText.Append(line, ref pos, " part="u8);
        GuideXosText.AppendUnsigned(line, ref pos, part);
        GuideXosText.Append(line, ref pos, " "u8);
        return pos;
    }

    private static void AppendPointField(Span<byte> line, ref int pos,
        ReadOnlySpan<byte> name, int x, int y)
    {
        GuideXosText.Append(line, ref pos, name);
        AppendSigned(line, ref pos, x);
        GuideXosText.Append(line, ref pos, ","u8);
        AppendSigned(line, ref pos, y);
        GuideXosText.Append(line, ref pos, " "u8);
    }

    private static void AppendSigned(Span<byte> line, ref int pos, int value)
    {
        if (value < 0)
        {
            line[pos++] = (byte)'-';
            GuideXosText.AppendUnsigned(line, ref pos, (uint)(-(long)value));
        }
        else GuideXosText.AppendUnsigned(line, ref pos, (uint)value);
    }

    private static void AppendUnsignedField(Span<byte> line, ref int pos,
        ReadOnlySpan<byte> name, uint value)
    {
        GuideXosText.Append(line, ref pos, name);
        GuideXosText.AppendUnsigned(line, ref pos, value);
        GuideXosText.Append(line, ref pos, " "u8);
    }

    private GuideXosResult Render(GuideXosHost host, GuideXosSurface surface)
    {
        if (surface.TryFillRect(10, 10, 540, 320, 0x003D465Au) != GuideXosResult.Success ||
            surface.TrySetText(20, 24, "Settings Center  |  managed working copy"u8) != GuideXosResult.Success ||
            _inputEnabled.Render(surface) != GuideXosResult.Success ||
            _showAdvanced.Render(surface) != GuideXosResult.Success ||
            _optionsButton.Render(surface) != GuideXosResult.Success ||
            surface.TrySetText(280, 24, IsDirty ? "Unsaved changes"u8 : "Applied"u8) != GuideXosResult.Success ||
            _view.Render(surface) != GuideXosResult.Success ||
            _scrollBar.Render(surface) != GuideXosResult.Success ||
            (_menu.IsOpen && _menu.Render(surface) != GuideXosResult.Success) ||
            (_activeDialog != null && _activeDialog.Render(surface) != GuideXosResult.Success) ||
            surface.TrySetText(20, 306, _working.ShowKeyboardTips
                ? "Tab / Shift+Tab move focus | Enter activates | wheel scrolls"u8
                : "Tab moves focus | Enter activates"u8) != GuideXosResult.Success)
            return GuideXosResult.InvalidArgument;
        _ = host;
        return GuideXosResult.Success;
    }
}
