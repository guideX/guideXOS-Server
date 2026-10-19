using System.Diagnostics;
using System.Globalization;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
using Navigation.Core;
using StellarNavigator.Scene;

namespace StellarNavigator.Gui;

internal sealed class MainForm : Form
{
    private readonly SceneBuilder? _sceneBuilder;
    private readonly IndexedChebyshevEphemerisProvider? _ephemeris;
    private readonly EpochInputResolver _epochResolver;
    private readonly TimeConverter? _timeConverter;
    private readonly SceneGenerationGate _generationGate = new();
    private readonly string? _ephemerisError;
    private readonly string _timeStatus;
    private readonly string _orientationStatus;
    private readonly string _trajectoryStartupStatus;
    private readonly Stopwatch _startup;
    private readonly SolarMapControl _map = new();
    private readonly ComboBox _epochKind = NewCombo(180);
    private readonly TextBox _epochText = new() { Width = 150, Text = "0", Font = new Font("Consolas", 9F) };
    private readonly CheckBox _allowProvisional = new() { Text = "Allow provisional future UTC", AutoSize = true };
    private readonly ComboBox _originMode = NewCombo(190);
    private readonly ComboBox _orientation = NewCombo(190);
    private readonly ComboBox _analysisTarget = NewCombo(205);
    private readonly CheckBox _trails = new() { Text = "Sampled geometric trails", AutoSize = true };
    private readonly CheckBox _showTrajectory = new() { Text = "SN8 simulated trajectory", AutoSize = true, Checked = true };
    private readonly CheckBox _showManeuvers = new() { Text = "Maneuver markers", AutoSize = true, Checked = true };
    private readonly ListBox _objects = new() { Dock = DockStyle.Top, Height = 220, IntegralHeight = false };
    private readonly ListBox _maneuverList = new() { Width = 315, Height = 76, IntegralHeight = false };
    private readonly ListBox _missionEvents = new() { Width = 410, Height = 190, IntegralHeight = false, HorizontalScrollbar = true };
    private readonly Label _trajectoryInfo = new() { AutoSize = true, MaximumSize = new Size(315, 0) };
    private readonly TextBox _initialEt = NewInput(200);
    private readonly TextBox _initialX = NewInput(76);
    private readonly TextBox _initialY = NewInput(76);
    private readonly TextBox _initialZ = NewInput(76);
    private readonly TextBox _initialVx = NewInput(76);
    private readonly TextBox _initialVy = NewInput(76);
    private readonly TextBox _initialVz = NewInput(76);
    private readonly TextBox _maneuverEt = NewInput(87);
    private readonly TextBox _deltaVx = NewInput(68);
    private readonly TextBox _deltaVy = NewInput(68);
    private readonly TextBox _deltaVz = NewInput(68);
    private readonly TextBox _maneuverLabel = NewInput(118);
    private readonly CheckBox _maneuverEnabled = new() { Text = "Enabled", AutoSize = true, Checked = true };
    private readonly TextBox _details = new()
    {
        Dock = DockStyle.Fill,
        Multiline = true,
        ReadOnly = true,
        ScrollBars = ScrollBars.Vertical,
        WordWrap = true,
        Font = new Font("Consolas", 8.5F),
        BackColor = Color.FromArgb(13, 19, 31),
        ForeColor = Color.FromArgb(222, 231, 244),
        BorderStyle = BorderStyle.None,
    };
    private readonly ToolStripStatusLabel _status = new() { Spring = true, TextAlign = ContentAlignment.MiddleLeft };
    private readonly ToolStripStatusLabel _sceneMetrics = new();
    private readonly ToolStripStatusLabel _paintMetrics = new();
    private readonly ToolStripStatusLabel _cacheMetrics = new();
    private readonly ToolStripStatusLabel _memoryMetrics = new();
    private readonly System.Windows.Forms.Timer _animationTimer = new() { Interval = 250 };
    private CancellationTokenSource? _buildCancellation;
    private NavigationScene? _scene;
    private EpochResolution? _currentEpochResolution;
    private bool _buildInFlight;
    private bool _hasFitted;
    private bool _syncingSelection;
    private bool _syncingAnalysisTarget;
    private long _lastInteractionTicks;
    private bool _isPlaying;
    private SimulatedTrajectoryScenario? _trajectoryScenario;
    private List<ImpulsiveManeuver> _maneuvers = [];
    private bool _syncingManeuverSelection;

    public MainForm(SceneBuilder? sceneBuilder, IndexedChebyshevEphemerisProvider? ephemeris,
        TimeConverter? timeConverter, string? ephemerisError, string timeStatus,
        string orientationStatus, string trajectoryStatus, Stopwatch startup)
    {
        _sceneBuilder = sceneBuilder;
        _ephemeris = ephemeris;
        _timeConverter = timeConverter;
        _epochResolver = new EpochInputResolver(timeConverter);
        _ephemerisError = ephemerisError;
        _timeStatus = timeStatus;
        _orientationStatus = orientationStatus;
        _trajectoryStartupStatus = trajectoryStatus;
        _trajectoryScenario = sceneBuilder?.CreateDemonstrationTrajectoryScenario();
        _startup = startup;

        Text = "Stellar Navigator — SN8 Simulated Trajectories";
        StartPosition = FormStartPosition.CenterScreen;
        WindowState = FormWindowState.Maximized;
        MinimumSize = new Size(1180, 720);
        BackColor = Color.FromArgb(12, 18, 29);
        ForeColor = Color.FromArgb(232, 238, 247);
        Font = new Font("Segoe UI", 9F);
        KeyPreview = true;

        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Padding = new Padding(0) };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 94));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 25));
        root.Controls.Add(CreateToolbar(), 0, 0);
        root.Controls.Add(CreateContent(), 0, 1);
        root.Controls.Add(CreateStatusStrip(), 0, 2);
        Controls.Add(root);

        _map.SelectionChanged += OnMapSelectionChanged;
        _map.PaintMeasured += OnPaintMeasured;
        _epochKind.Items.AddRange(["ET seconds (TDB)", "JD TDB", "UTC"]);
        _epochKind.SelectedIndex = 0;
        _originMode.Items.AddRange(["Solar System barycenter (SSB)", "Earth geocenter", "Simulated spacecraft"]);
        _originMode.SelectedIndex = 0;
        _orientation.Items.AddRange(["J2000 inertial axes", "IAU_EARTH axes (low precision)"]);
        _orientation.SelectedIndex = 0;
        _analysisTarget.Items.AddRange(SceneBuilder.Definitions
            .Select(definition => new AnalysisTargetEntry(definition.Id, definition.Name)).ToArray());
        _analysisTarget.DisplayMember = nameof(AnalysisTargetEntry.Name);
        _analysisTarget.SelectedIndex = SceneBuilder.Definitions.ToList().FindIndex(item => item.Id == "earth");
        _orientation.Enabled = orientationStatus.StartsWith("IAU_EARTH enabled", StringComparison.Ordinal);
        _allowProvisional.Enabled = timeConverter is not null;
        _allowProvisional.Checked = false;
        _allowProvisional.ForeColor = Color.FromArgb(181, 200, 224);
        _allowProvisional.BackColor = Color.FromArgb(18, 27, 42);
        _trails.ForeColor = Color.FromArgb(181, 200, 224);
        _trails.BackColor = Color.FromArgb(18, 27, 42);
        _showTrajectory.ForeColor = _showManeuvers.ForeColor = Color.FromArgb(181, 200, 224);
        _showTrajectory.BackColor = _showManeuvers.BackColor = Color.FromArgb(18, 27, 42);
        _showTrajectory.Enabled = _sceneBuilder?.IsTrajectoryModelAvailable == true;
        _showManeuvers.Enabled = _sceneBuilder?.IsTrajectoryModelAvailable == true;
        _trajectoryInfo.Text = _sceneBuilder?.IsTrajectoryModelAvailable == true
            ? "Idealized Sun-centered Newtonian two-body simulation · approximate/simulated state · not flight navigation."
            : _trajectoryStartupStatus;
        _trajectoryInfo.ForeColor = Color.FromArgb(255, 205, 142);
        _maneuverList.BackColor = Color.FromArgb(17, 25, 39);
        _maneuverList.ForeColor = Color.FromArgb(225, 233, 245);
        _maneuverList.BorderStyle = BorderStyle.FixedSingle;
        _analysisTarget.ForeColor = Color.FromArgb(225, 233, 245);
        _missionEvents.BackColor = Color.FromArgb(17, 25, 39);
        _missionEvents.ForeColor = Color.FromArgb(225, 233, 245);
        _missionEvents.BorderStyle = BorderStyle.FixedSingle;
        _maneuverList.SelectedIndexChanged += OnManeuverListSelectionChanged;
        _analysisTarget.SelectedIndexChanged += OnAnalysisTargetChanged;
        _missionEvents.SelectedIndexChanged += OnMissionEventSelected;
        _map.ShowTrajectory = _showTrajectory.Checked;
        _map.ShowManeuvers = _showManeuvers.Checked;
        _showTrajectory.CheckedChanged += (_, _) => { _map.ShowTrajectory = _showTrajectory.Checked; _map.Invalidate(); };
        _showManeuvers.CheckedChanged += (_, _) => { _map.ShowManeuvers = _showManeuvers.Checked; _map.Invalidate(); };
        _epochKind.SelectedIndexChanged += (_, _) => UpdateTimeControls();
        _originMode.SelectedIndexChanged += (_, _) =>
        {
            UpdateOriginControls();
            if (_scene is not null)
                RequestSceneBuild();
        };
        _orientation.SelectedIndexChanged += (_, _) => { if (_scene is not null) RequestSceneBuild(); };
        _trails.CheckedChanged += (_, _) => { if (_scene is not null) RequestSceneBuild(); };
        _objects.SelectedIndexChanged += OnObjectListSelectionChanged;
        _animationTimer.Tick += OnAnimationTick;
        Shown += OnShown;
        FormClosing += (_, _) =>
        {
            _animationTimer.Stop();
            _buildCancellation?.Cancel();
            _generationGate.CancelPending();
        };
        KeyDown += OnKeyDown;
        RefreshTrajectoryControls();
        UpdateTimeControls();
        UpdateOriginControls();
        if (_sceneBuilder is null)
            _map.SetDiagnostic(_ephemerisError ?? "Essential DE440s ephemeris data is unavailable.");
    }

    private Control CreateToolbar()
    {
        var panel = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 2,
            Padding = new Padding(8, 7, 8, 3),
            BackColor = Color.FromArgb(18, 27, 42),
        };
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        panel.Controls.Add(CreateTimeToolbar(), 0, 0);
        panel.Controls.Add(CreateMapToolbar(), 0, 1);
        return panel;
    }

    private Control CreateTimeToolbar()
    {
        var row = NewFlowRow();
        row.Controls.Add(NewCaption("Epoch:"));
        row.Controls.Add(_epochKind);
        row.Controls.Add(_epochText);
        row.Controls.Add(NewButton("Set epoch", (_, _) => SetEpochFromControls()));
        row.Controls.Add(NewButton("−1 day", (_, _) => StepEpoch(-86400)));
        row.Controls.Add(NewButton("−1 hour", (_, _) => StepEpoch(-3600)));
        row.Controls.Add(NewButton("+1 hour", (_, _) => StepEpoch(3600)));
        row.Controls.Add(NewButton("+1 day", (_, _) => StepEpoch(86400)));
        row.Controls.Add(NewButton("Play / pause", (_, _) => ToggleAnimation()));
        row.Controls.Add(_allowProvisional);
        row.Controls.Add(NewCaption("TDB; ET-native by default"));
        return row;
    }

    private Control CreateMapToolbar()
    {
        var row = NewFlowRow();
        row.Controls.Add(NewCaption("Scientific origin:"));
        row.Controls.Add(_originMode);
        row.Controls.Add(NewCaption("Axes:"));
        row.Controls.Add(_orientation);
        row.Controls.Add(_trails);
        row.Controls.Add(_showTrajectory);
        row.Controls.Add(_showManeuvers);
        row.Controls.Add(NewButton("Fit Solar System", (_, _) => FitSolarSystem()));
        row.Controls.Add(NewButton("Reset view", (_, _) => _map.ResetView()));
        row.Controls.Add(NewButton("Center selected", (_, _) => _map.CenterOnSelected()));
        row.Controls.Add(NewButton("Zoom +", (_, _) => MeasureInteraction(() => _map.ZoomAtCenter(1.35))));
        row.Controls.Add(NewButton("Zoom −", (_, _) => MeasureInteraction(() => _map.ZoomAtCenter(1.0 / 1.35))));
        return row;
    }

    private Control CreateContent()
    {
        var split = new SplitContainer
        {
            Dock = DockStyle.Fill,
            Orientation = Orientation.Vertical,
            FixedPanel = FixedPanel.Panel2,
            BackColor = Color.FromArgb(12, 18, 29),
            BorderStyle = BorderStyle.None,
            SplitterWidth = 5,
        };
        split.Panel1.Controls.Add(_map);
        split.Panel2.Padding = new Padding(11, 10, 12, 8);
        var right = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 4 };
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, ClientSize.Height >= 900 ? 510 : 430));
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 35));
        var title = new Label { Text = "OBJECTS & SCIENTIFIC STATE", Dock = DockStyle.Fill, ForeColor = Color.FromArgb(186, 208, 237), Font = new Font("Segoe UI Semibold", 9.5F, FontStyle.Bold), TextAlign = ContentAlignment.MiddleLeft };
        right.Controls.Add(title, 0, 0);
        _objects.BackColor = Color.FromArgb(17, 25, 39);
        _objects.ForeColor = Color.FromArgb(225, 233, 245);
        _objects.BorderStyle = BorderStyle.FixedSingle;
        var tabs = new TabControl { Dock = DockStyle.Fill };
        var objectsPage = new TabPage("Objects") { BackColor = Color.FromArgb(12, 18, 29), ForeColor = Color.Gainsboro };
        _objects.Dock = DockStyle.Fill;
        objectsPage.Controls.Add(_objects);
        var trajectoryPage = new TabPage("Trajectory & maneuvers") { BackColor = Color.FromArgb(12, 18, 29), ForeColor = Color.Gainsboro };
        trajectoryPage.Controls.Add(CreateTrajectoryPanel());
        tabs.TabPages.Add(objectsPage);
        tabs.TabPages.Add(trajectoryPage);
        right.Controls.Add(tabs, 0, 1);
        var detailsContainer = new Panel { Dock = DockStyle.Fill, Padding = new Padding(0, 10, 0, 0) };
        detailsContainer.Controls.Add(_details);
        right.Controls.Add(detailsContainer, 0, 2);
        var buttonRow = NewFlowRow();
        buttonRow.Controls.Add(NewButton("Center on object", (_, _) => _map.CenterOnSelected()));
        right.Controls.Add(buttonRow, 0, 3);
        split.Panel2.Controls.Add(right);
        split.SizeChanged += (_, _) =>
        {
            if (split.Width < 900)
                return;
            split.Panel1MinSize = 420;
            split.Panel2MinSize = 455;
            int desired = split.Width - 485;
            if (desired > split.Panel1MinSize && desired < split.Width - split.Panel2MinSize)
                split.SplitterDistance = desired;
        };
        _map.Dock = DockStyle.Fill;
        return split;
    }

    private Control CreateTrajectoryPanel()
    {
        bool available = _sceneBuilder?.IsTrajectoryModelAvailable == true;
        var initialGroup = new GroupBox
        {
            Text = "Initial state · Sun-relative J2000",
            Width = 440,
            Height = 166,
            ForeColor = Color.FromArgb(190, 211, 238),
            Enabled = available,
        };
        var initialRows = NewVerticalRows();
        initialRows.Controls.Add(InputRow("Initial ET (TDB s)", _initialEt));
        initialRows.Controls.Add(VectorInputRow("Position m", _initialX, _initialY, _initialZ));
        initialRows.Controls.Add(VectorInputRow("Velocity km/s", _initialVx, _initialVy, _initialVz));
        var initialButtons = NewInputRow();
        initialButtons.Controls.Add(NewButton("Apply initial state", (_, _) => ApplyInitialScenario()));
        initialButtons.Controls.Add(NewButton("Restore demonstration", (_, _) => RestoreDemonstration()));
        initialRows.Controls.Add(initialButtons);
        initialGroup.Controls.Add(initialRows);

        var maneuverGroup = new GroupBox
        {
            Text = $"Impulsive Δv events · maximum {TrajectoryGenerator.MaximumManeuvers}",
            Width = 440,
            Height = 246,
            ForeColor = Color.FromArgb(190, 211, 238),
            Enabled = available,
        };
        var maneuverRows = NewVerticalRows();
        _maneuverList.Width = 410;
        maneuverRows.Controls.Add(_maneuverList);
        maneuverRows.Controls.Add(InputRow("Execution ET (TDB s)", _maneuverEt));
        maneuverRows.Controls.Add(VectorInputRow("Δv m/s", _deltaVx, _deltaVy, _deltaVz));
        var labelRow = NewInputRow();
        labelRow.Controls.Add(MiniLabel("Label", 45));
        labelRow.Controls.Add(_maneuverLabel);
        labelRow.Controls.Add(_maneuverEnabled);
        maneuverRows.Controls.Add(labelRow);
        var maneuverButtons = NewInputRow();
        maneuverButtons.Controls.Add(NewButton("Add / update", (_, _) => ApplyManeuver()));
        maneuverButtons.Controls.Add(NewButton("Remove", (_, _) => RemoveManeuver()));
        maneuverRows.Controls.Add(maneuverButtons);
        maneuverGroup.Controls.Add(maneuverRows);

        var eventsGroup = new GroupBox
        {
            Text = "Mission events · ET/TDB seconds past J2000",
            Width = 440,
            Height = 258,
            ForeColor = Color.FromArgb(190, 211, 238),
            Enabled = available,
        };
        var eventRows = NewVerticalRows();
        var targetRow = NewInputRow();
        targetRow.Controls.Add(MiniLabel("Closest approach to", 118));
        _analysisTarget.Width = 284;
        targetRow.Controls.Add(_analysisTarget);
        eventRows.Controls.Add(targetRow);
        _missionEvents.Width = 410;
        eventRows.Controls.Add(_missionEvents);
        eventsGroup.Controls.Add(eventRows);

        var stack = NewVerticalRows();
        stack.Controls.Add(_trajectoryInfo);
        stack.Controls.Add(initialGroup);
        stack.Controls.Add(maneuverGroup);
        stack.Controls.Add(eventsGroup);
        var panel = new Panel { Dock = DockStyle.Fill, AutoScroll = true, BackColor = Color.FromArgb(12, 18, 29) };
        panel.Controls.Add(stack);
        return panel;
    }

    private static FlowLayoutPanel NewVerticalRows() => new()
    {
        Dock = DockStyle.Top,
        AutoSize = true,
        AutoSizeMode = AutoSizeMode.GrowAndShrink,
        FlowDirection = FlowDirection.TopDown,
        WrapContents = false,
        Padding = new Padding(4),
        BackColor = Color.FromArgb(12, 18, 29),
    };

    private static FlowLayoutPanel NewInputRow() => new()
    {
        AutoSize = true,
        WrapContents = false,
        FlowDirection = FlowDirection.LeftToRight,
        Margin = new Padding(1, 2, 1, 2),
        BackColor = Color.FromArgb(12, 18, 29),
    };

    private static Control InputRow(string label, Control input)
    {
        var row = NewInputRow();
        row.Controls.Add(MiniLabel(label, 106));
        row.Controls.Add(input);
        return row;
    }

    private static Control VectorInputRow(string label, TextBox x, TextBox y, TextBox z)
    {
        var row = NewInputRow();
        row.Controls.Add(MiniLabel(label, 80));
        row.Controls.Add(MiniLabel("X", 12));
        row.Controls.Add(x);
        row.Controls.Add(MiniLabel("Y", 12));
        row.Controls.Add(y);
        row.Controls.Add(MiniLabel("Z", 12));
        row.Controls.Add(z);
        return row;
    }

    private static Label MiniLabel(string text, int width) => new()
    {
        Text = text,
        AutoSize = false,
        Width = width,
        Height = 26,
        TextAlign = ContentAlignment.MiddleLeft,
        ForeColor = Color.FromArgb(181, 200, 224),
        Margin = new Padding(2, 1, 0, 0),
    };

    private Control CreateStatusStrip()
    {
        var strip = new StatusStrip { Dock = DockStyle.Fill, SizingGrip = false, BackColor = Color.FromArgb(20, 29, 44), ForeColor = Color.FromArgb(190, 204, 222) };
        _status.Text = _ephemerisError ?? "Loading authenticated DE440s scene…";
        strip.Items.Add(_status);
        strip.Items.Add(new ToolStripStatusLabel(" "));
        strip.Items.Add(_sceneMetrics);
        strip.Items.Add(new ToolStripStatusLabel(" | "));
        strip.Items.Add(_paintMetrics);
        strip.Items.Add(new ToolStripStatusLabel(" | "));
        strip.Items.Add(_cacheMetrics);
        strip.Items.Add(new ToolStripStatusLabel(" | "));
        strip.Items.Add(_memoryMetrics);
        return strip;
    }

    private void OnShown(object? sender, EventArgs e)
    {
        _startup.Stop();
        if (_sceneBuilder is null)
        {
            _status.Text = _ephemerisError ?? "Map unavailable: DE440s ephemeris data did not load.";
            return;
        }
        _currentEpochResolution = _epochResolver.Resolve(EpochInputKind.EtSeconds, "0");
        UpdateTimeStatus();
        RequestSceneBuild();
        UpdateMetrics();
    }

    private void SetEpochFromControls()
    {
        var kind = SelectedEpochKind();
        EpochResolution resolution = _epochResolver.Resolve(kind, _epochText.Text, _allowProvisional.Checked);
        if (!resolution.Success)
        {
            ShowDiagnostic(resolution.Error ?? "Epoch input was not accepted.");
            return;
        }
        _currentEpochResolution = resolution;
        UpdateTimeStatus();
        RequestSceneBuild();
    }

    private void StepEpoch(double seconds)
    {
        EpochResolution? current = _currentEpochResolution;
        if (current is null || !current.Success)
        {
            current = _epochResolver.Resolve(SelectedEpochKind(), _epochText.Text, _allowProvisional.Checked);
            if (!current.Success)
            {
                ShowDiagnostic(current.Error ?? "The current epoch cannot be stepped.");
                return;
            }
        }
        EtSeconds stepped;
        try
        {
            stepped = EpochInputResolver.StepEt(current.Epoch, seconds);
        }
        catch (ArgumentOutOfRangeException ex)
        {
            ShowDiagnostic(ex.Message);
            return;
        }
        _epochKind.SelectedIndex = 0; // subsequent increments remain on precision-preserving ET
        _epochText.Text = stepped.Value.ToString("R", CultureInfo.InvariantCulture);
        _currentEpochResolution = _epochResolver.Resolve(EpochInputKind.EtSeconds, _epochText.Text);
        UpdateTimeStatus();
        RequestSceneBuild();
    }

    private void ToggleAnimation()
    {
        _isPlaying = !_isPlaying;
        if (_isPlaying)
            _animationTimer.Start();
        else
            _animationTimer.Stop();
        _status.Text = _isPlaying ? "Playing at 1 simulated minute per 250 ms; one scene build at a time." : "Animation paused.";
    }

    private void OnAnimationTick(object? sender, EventArgs e)
    {
        if (!_isPlaying || _buildInFlight)
            return;
        StepEpoch(60.0);
    }

    private void RequestSceneBuild(SimulatedTrajectoryScenario? scenarioCandidate = null,
        IReadOnlyList<ImpulsiveManeuver>? maneuverCandidate = null)
    {
        if (_sceneBuilder is null)
            return;
        if (_currentEpochResolution is null || !_currentEpochResolution.Success)
        {
            var result = _epochResolver.Resolve(SelectedEpochKind(), _epochText.Text, _allowProvisional.Checked);
            if (!result.Success)
            {
                ShowDiagnostic(result.Error ?? "Epoch input was not accepted.");
                return;
            }
            _currentEpochResolution = result;
        }

        var resolution = _currentEpochResolution;
        SimulatedTrajectoryScenario? scenario = scenarioCandidate ?? _trajectoryScenario;
        bool includeTrajectory = _sceneBuilder.IsTrajectoryModelAvailable && scenario is not null;
        var maneuvers = (maneuverCandidate ?? _maneuvers).ToArray();
        var request = new SceneBuildRequest(resolution.Epoch,
            SelectedOriginMode(), SelectedOrientation(),
            IncludeMoon: true, IncludeTrails: _trails.Checked,
            TimeQuality: resolution.TimeQuality,
            EpochRepresentation: resolution.RepresentationPrecision,
            IncludeSimulatedTrajectory: includeTrajectory,
            TrajectoryScenario: includeTrajectory ? scenario : null,
            Maneuvers: includeTrajectory ? maneuvers : null,
            TrajectoryAnalysisTargetBodyId: (_analysisTarget.SelectedItem as AnalysisTargetEntry)?.Id ?? "earth");
        long ticket = _generationGate.BeginRequest();
        var cancellation = new CancellationTokenSource();
        var prior = _buildCancellation;
        _buildCancellation = cancellation;
        prior?.Cancel();
        _buildInFlight = true;
        _status.Text = "Evaluating authenticated ephemeris scene…";
        _map.SetDiagnostic(null);
        _ = BuildSceneAsync(ticket, request, cancellation);
    }

    private async Task BuildSceneAsync(long ticket, SceneBuildRequest request, CancellationTokenSource cancellation)
    {
        try
        {
            NavigationScene result = await Task.Run(
                () => _sceneBuilder!.Build(request, cancellation.Token), cancellation.Token);
            if (!_generationGate.IsCurrent(ticket) || cancellation.IsCancellationRequested || IsDisposed)
                return;
            _scene = result;
            if (request.IncludeSimulatedTrajectory && result.Trajectory is not null)
            {
                _trajectoryScenario = request.TrajectoryScenario;
                _maneuvers = request.Maneuvers?.ToList() ?? [];
                RefreshTrajectoryControls();
                _trajectoryInfo.Text =
                    $"{result.Trajectory.Model.ModelIdentity} · {result.Trajectory.Model.Quality} · " +
                    $"{result.Trajectory.Model.Samples.Count}/{TrajectoryGenerator.DefaultMaximumSamples} samples · " +
                    $"coast + maneuvers recomputed in {result.Trajectory.Model.GenerationDuration.TotalMilliseconds:F2} ms. " +
                    "Simulation only; not observed spacecraft motion.";
            }
            _map.SetScene(result);
            _epochText.Text = result.Epoch.Value.ToString("R", CultureInfo.InvariantCulture);
            if (_originMode.SelectedIndex != (int)result.OriginMode)
                _originMode.SelectedIndex = (int)result.OriginMode;
            RefreshObjectList(result);
            RefreshMissionEvents(result);
            UpdateTimeStatus();
            _status.Text = $"Scene ready · {result.Objects.Count - 1} ephemeris objects · " +
                (result.Trajectory is null ? "SN8 trajectory unavailable" : "SN8 two-body trajectory") +
                $" · {result.Orientation} X/Y projection · {result.ScientificOriginId}";
            _map.SetDiagnostic(null);
            if (!_hasFitted)
            {
                _map.FitSolarSystem();
                _hasFitted = true;
            }
            UpdateDetails(_map.Scene?.FindObject(_map.SelectedId ?? ""));
            UpdateMetrics();
        }
        catch (OperationCanceledException)
        {
            // A newer epoch/origin request superseded this one.
        }
        catch (Exception ex)
        {
            if (_generationGate.IsCurrent(ticket) && !IsDisposed)
                ShowDiagnostic(ex.Message);
        }
        finally
        {
            if (_generationGate.IsCurrent(ticket))
            {
                _buildInFlight = false;
                if (ReferenceEquals(_buildCancellation, cancellation))
                    _buildCancellation = null;
                if (_isPlaying && _scene is null)
                    StopAnimation();
            }
            cancellation.Dispose();
        }
    }

    private void ShowDiagnostic(string message)
    {
        _map.SetDiagnostic(message);
        string active = _scene is null ? "No valid scene is loaded." :
            $"Displayed scene remains at ET {_scene.Epoch.Value.ToString("R", CultureInfo.InvariantCulture)}s.";
        _status.Text = $"Request refused: {message}  {active}";
        if (message.Contains("outside", StringComparison.OrdinalIgnoreCase) && _isPlaying)
            StopAnimation();
    }

    private void StopAnimation()
    {
        _isPlaying = false;
        _animationTimer.Stop();
        if (!IsDisposed)
            _status.Text = "Animation stopped at an unavailable scene epoch.";
    }

    private void RefreshObjectList(NavigationScene scene)
    {
        _syncingSelection = true;
        try
        {
            _objects.BeginUpdate();
            _objects.Items.Clear();
            foreach (var item in scene.Objects)
                _objects.Items.Add(item);
            if (scene.Trajectory is { } trajectory)
                _objects.Items.Add(trajectory.VehicleMarker);
            _objects.DisplayMember = nameof(SceneObject.Name);
            int selected = -1;
            for (int i = 0; i < _objects.Items.Count; i++)
                if (_objects.Items[i] is SceneObject candidate && candidate.Id == _map.SelectedId)
                {
                    selected = i;
                    break;
                }
            if (selected < 0 && string.IsNullOrEmpty(_map.SelectedId))
                selected = scene.Objects.ToList().FindIndex(x => x.Id == "earth");
            _objects.SelectedIndex = selected;
        }
        finally
        {
            _objects.EndUpdate();
            _syncingSelection = false;
        }
    }

    private void OnMapSelectionChanged(SceneObject? item)
    {
        UpdateDetails(item);
        if (item is null || _syncingSelection)
            return;
        if (item.Definition is { } definition)
        {
            int targetIndex = _analysisTarget.Items.Cast<AnalysisTargetEntry>()
                .ToList().FindIndex(entry => entry.Id == definition.Id);
            if (targetIndex >= 0 && _analysisTarget.SelectedIndex != targetIndex)
            {
                _syncingAnalysisTarget = true;
                _analysisTarget.SelectedIndex = targetIndex;
                _syncingAnalysisTarget = false;
                if (_scene is not null)
                    RequestSceneBuild();
            }
        }
        if (item.ManeuverSelection is { } maneuver)
        {
            _map.SetSelectedManeuver(maneuver.Id);
            _syncingManeuverSelection = true;
            _maneuverList.SelectedIndex = FindManeuverListIndex(maneuver.Id);
            _syncingManeuverSelection = false;
        }
        else
        {
            _map.SetSelectedManeuver(null);
            _syncingManeuverSelection = true;
            _maneuverList.SelectedIndex = -1;
            _syncingManeuverSelection = false;
        }
        _syncingSelection = true;
        int index = -1;
        for (int i = 0; i < _objects.Items.Count; i++)
            if (_objects.Items[i] is SceneObject candidate && candidate.Id == item.Id) { index = i; break; }
        _objects.SelectedIndex = index;
        _syncingSelection = false;
    }

    private void OnObjectListSelectionChanged(object? sender, EventArgs e)
    {
        if (_syncingSelection || _objects.SelectedItem is not SceneObject selected)
            return;
        _map.Select(selected.Id);
    }

    private void OnManeuverListSelectionChanged(object? sender, EventArgs e)
    {
        if (_syncingManeuverSelection || _maneuverList.SelectedItem is not ManeuverListEntry selected)
            return;
        LoadManeuverIntoEditor(selected.Maneuver);
        _map.SetSelectedManeuver(selected.Maneuver.Id);
        _map.Select($"maneuver:{selected.Maneuver.Id}");
    }

    private void OnAnalysisTargetChanged(object? sender, EventArgs e)
    {
        if (_syncingAnalysisTarget || _analysisTarget.SelectedItem is not AnalysisTargetEntry selected)
            return;
        if (_scene is not null)
        {
            _map.Select(selected.Id);
            RequestSceneBuild();
        }
    }

    private void OnMissionEventSelected(object? sender, EventArgs e)
    {
        if (_missionEvents.SelectedItem is not MissionEventListEntry selected || !double.IsFinite(selected.Event.EtSeconds))
            return;
        _epochKind.SelectedIndex = 0;
        _epochText.Text = selected.Event.EtSeconds.ToString("R", CultureInfo.InvariantCulture);
        _currentEpochResolution = _epochResolver.Resolve(EpochInputKind.EtSeconds, _epochText.Text);
        if (!_currentEpochResolution.Success)
        {
            ShowDiagnostic(_currentEpochResolution.Error ?? "The selected event epoch could not be accepted.");
            return;
        }
        UpdateTimeStatus();
        RequestSceneBuild();
    }

    private void RefreshMissionEvents(NavigationScene scene)
    {
        _missionEvents.BeginUpdate();
        try
        {
            _missionEvents.Items.Clear();
            if (scene.Trajectory?.MissionEvents is { } timeline)
            {
                foreach (var item in timeline.Events)
                    _missionEvents.Items.Add(new MissionEventListEntry(item, FormatMissionEvent(item)));
                _missionEvents.DisplayMember = nameof(MissionEventListEntry.Text);
            }
        }
        finally
        {
            _missionEvents.EndUpdate();
        }
    }

    private static string FormatMissionEvent(MissionEvent item)
    {
        string epoch = $"ET {item.EtSeconds.ToString("R", CultureInfo.InvariantCulture)} s TDB";
        if (item.Type == MissionEventType.ClosestApproach)
        {
            string targetName = SceneBuilder.Definitions.FirstOrDefault(definition => definition.Id == item.ObjectId)?.Name
                ?? item.ObjectId ?? "unknown target";
            string distance = item.DistanceMeters.HasValue
                ? (item.DistanceMeters.Value / 1000.0).ToString("G6", CultureInfo.InvariantCulture)
                : "n/a";
            string relativeSpeed = item.RelativeSpeedMetersPerSecond.HasValue
                ? $" · {item.RelativeSpeedMetersPerSecond.Value.ToString("G5", CultureInfo.InvariantCulture)} m/s rel."
                : string.Empty;
            return $"Closest to {targetName} · {distance} km · " +
                $"{(item.IsRefined == true ? "refined" : "sampled")}{relativeSpeed} · {epoch}";
        }
        if (item.Type == MissionEventType.Maneuver)
        {
            string magnitude = item.DeltaVMagnitudeMetersPerSecond.HasValue
                ? item.DeltaVMagnitudeMetersPerSecond.Value.ToString("G5", CultureInfo.InvariantCulture)
                : "n/a";
            return $"{item.Name} · {item.ManeuverStatus} · |Δv| {magnitude} m/s · {epoch}";
        }
        return item.Type == MissionEventType.Warning
            ? $"WARNING · {item.Warning} · {epoch}"
            : $"{item.Name} · {epoch}";
    }

    private int FindManeuverListIndex(string id)
    {
        for (int i = 0; i < _maneuverList.Items.Count; i++)
            if (_maneuverList.Items[i] is ManeuverListEntry entry && entry.Maneuver.Id == id)
                return i;
        return -1;
    }

    private void RefreshTrajectoryControls()
    {
        if (_trajectoryScenario is { } scenario)
        {
            _initialEt.Text = scenario.InitialState.EtSeconds.ToString("R", CultureInfo.InvariantCulture);
            _initialX.Text = scenario.InitialState.PositionMeters.X.ToString("R", CultureInfo.InvariantCulture);
            _initialY.Text = scenario.InitialState.PositionMeters.Y.ToString("R", CultureInfo.InvariantCulture);
            _initialZ.Text = scenario.InitialState.PositionMeters.Z.ToString("R", CultureInfo.InvariantCulture);
            _initialVx.Text = (scenario.InitialState.VelocityMetersPerSecond.X / 1000.0).ToString("R", CultureInfo.InvariantCulture);
            _initialVy.Text = (scenario.InitialState.VelocityMetersPerSecond.Y / 1000.0).ToString("R", CultureInfo.InvariantCulture);
            _initialVz.Text = (scenario.InitialState.VelocityMetersPerSecond.Z / 1000.0).ToString("R", CultureInfo.InvariantCulture);
        }
        _syncingManeuverSelection = true;
        try
        {
            _maneuverList.BeginUpdate();
            _maneuverList.Items.Clear();
            foreach (var maneuver in _maneuvers.OrderBy(m => m.EtSeconds).ThenBy(m => m.Id, StringComparer.Ordinal))
            {
                string label = string.IsNullOrWhiteSpace(maneuver.Label) ? maneuver.Id : maneuver.Label;
                _maneuverList.Items.Add(new ManeuverListEntry(maneuver,
                    $"{label} · ET {maneuver.EtSeconds:R}s · {(maneuver.Enabled ? "enabled" : "disabled")}"));
            }
            _maneuverList.DisplayMember = nameof(ManeuverListEntry.Text);
            _maneuverList.SelectedIndex = -1;
        }
        finally
        {
            _maneuverList.EndUpdate();
            _syncingManeuverSelection = false;
        }
        _map.SetSelectedManeuver(null);
    }

    private void LoadManeuverIntoEditor(ImpulsiveManeuver maneuver)
    {
        _maneuverEt.Text = maneuver.EtSeconds.ToString("R", CultureInfo.InvariantCulture);
        _deltaVx.Text = maneuver.DeltaVelocityMetersPerSecond.X.ToString("R", CultureInfo.InvariantCulture);
        _deltaVy.Text = maneuver.DeltaVelocityMetersPerSecond.Y.ToString("R", CultureInfo.InvariantCulture);
        _deltaVz.Text = maneuver.DeltaVelocityMetersPerSecond.Z.ToString("R", CultureInfo.InvariantCulture);
        _maneuverLabel.Text = maneuver.Label;
        _maneuverEnabled.Checked = maneuver.Enabled;
    }

    private void ApplyInitialScenario()
    {
        if (_trajectoryScenario is null)
            return;
        if (!TryReadFinite(_initialEt, "Initial ET", out double et) ||
            !TryReadFinite(_initialX, "Initial position X", out double x) ||
            !TryReadFinite(_initialY, "Initial position Y", out double y) ||
            !TryReadFinite(_initialZ, "Initial position Z", out double z) ||
            !TryReadFinite(_initialVx, "Initial velocity X", out double vx) ||
            !TryReadFinite(_initialVy, "Initial velocity Y", out double vy) ||
            !TryReadFinite(_initialVz, "Initial velocity Z", out double vz))
            return;
        var candidate = _trajectoryScenario with
        {
            InitialState = new OrbitState(et, new Vector3D(x, y, z), new Vector3D(vx * 1000.0, vy * 1000.0, vz * 1000.0)),
        };
        string? validation = candidate.Validate() ?? ValidateManeuverTimeline(candidate, _maneuvers);
        if (validation is not null)
        {
            ShowDiagnostic(validation);
            return;
        }
        RequestSceneBuild(candidate, _maneuvers.ToArray());
    }

    private void ApplyManeuver()
    {
        if (_trajectoryScenario is null)
            return;
        if (!TryReadFinite(_maneuverEt, "Maneuver ET", out double et) ||
            !TryReadFinite(_deltaVx, "Δv X", out double dx) ||
            !TryReadFinite(_deltaVy, "Δv Y", out double dy) ||
            !TryReadFinite(_deltaVz, "Δv Z", out double dz))
            return;
        var selected = _maneuverList.SelectedItem as ManeuverListEntry;
        string id = selected?.Maneuver.Id ?? NextManeuverId();
        string label = _maneuverLabel.Text.Trim();
        if (label.Length > 64)
        {
            ShowDiagnostic("Maneuver label must be at most 64 characters.");
            return;
        }
        var edited = new ImpulsiveManeuver(id, et, new Vector3D(dx, dy, dz), "J2000",
            _maneuverEnabled.Checked, label);
        var candidate = _maneuvers.ToList();
        int existing = candidate.FindIndex(m => m.Id == id);
        if (existing >= 0)
            candidate[existing] = edited;
        else
            candidate.Add(edited);
        string? validation = ValidateManeuverTimeline(_trajectoryScenario, candidate);
        if (validation is not null)
        {
            ShowDiagnostic(validation);
            return;
        }
        RequestSceneBuild(_trajectoryScenario, candidate);
    }

    private void RemoveManeuver()
    {
        if (_trajectoryScenario is null || _maneuverList.SelectedItem is not ManeuverListEntry selected)
        {
            ShowDiagnostic("Select a maneuver to remove.");
            return;
        }
        var candidate = _maneuvers.Where(m => m.Id != selected.Maneuver.Id).ToArray();
        RequestSceneBuild(_trajectoryScenario, candidate);
    }

    private void RestoreDemonstration()
    {
        if (_sceneBuilder?.CreateDemonstrationTrajectoryScenario() is not { } demo)
            return;
        RequestSceneBuild(demo, Array.Empty<ImpulsiveManeuver>());
    }

    private string NextManeuverId()
    {
        for (int i = 1; i <= TrajectoryGenerator.MaximumManeuvers; i++)
        {
            string id = $"M{i:D2}";
            if (_maneuvers.All(m => m.Id != id))
                return id;
        }
        return $"M{TrajectoryGenerator.MaximumManeuvers:D2}";
    }

    private string? ValidateManeuverTimeline(SimulatedTrajectoryScenario scenario,
        IReadOnlyList<ImpulsiveManeuver> maneuvers)
    {
        if (maneuvers.Count > TrajectoryGenerator.MaximumManeuvers)
            return $"At most {TrajectoryGenerator.MaximumManeuvers} maneuver events are allowed.";
        var ids = new HashSet<string>(StringComparer.Ordinal);
        var epochs = new HashSet<double>();
        foreach (var maneuver in maneuvers)
        {
            if (!ids.Add(maneuver.Id) || !epochs.Add(maneuver.EtSeconds))
                return "Maneuver identifiers and ET epochs must be unique.";
            if (!double.IsFinite(maneuver.EtSeconds) || maneuver.EtSeconds < scenario.InitialState.EtSeconds ||
                maneuver.EtSeconds - scenario.InitialState.EtSeconds > scenario.MaximumHorizonSeconds)
                return "Maneuver ET must be at or after the initial ET and within the configured scenario horizon.";
            if (maneuver.FrameId != "J2000" || !double.IsFinite(maneuver.DeltaVelocityMetersPerSecond.X) ||
                !double.IsFinite(maneuver.DeltaVelocityMetersPerSecond.Y) ||
                !double.IsFinite(maneuver.DeltaVelocityMetersPerSecond.Z) ||
                maneuver.DeltaVelocityMetersPerSecond.Magnitude > TrajectoryGenerator.MaximumDeltaVelocityMetersPerSecond)
                return $"Δv must use J2000 components, be finite, and have magnitude at most {TrajectoryGenerator.MaximumDeltaVelocityMetersPerSecond:N0} m/s.";
        }
        return null;
    }

    private bool TryReadFinite(TextBox box, string field, out double value)
    {
        bool valid = double.TryParse(box.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out value) &&
            double.IsFinite(value);
        if (!valid)
            ShowDiagnostic($"{field} must be a finite number using invariant decimal notation.");
        return valid;
    }

    private void UpdateDetails(SceneObject? item)
    {
        if (item is null)
        {
            _details.Text = "Select a celestial marker or object from the list.";
            return;
        }
        if (item.ManeuverSelection is { } maneuver)
        {
            var model = _scene?.Trajectory?.Model;
            double preSpeed = maneuver.PreManeuverSunRelativeState.VelocityMetersPerSecond.Magnitude;
            double postSpeed = maneuver.PostManeuverSunRelativeState.VelocityMetersPerSecond.Magnitude;
            _details.Text =
                $"SIMULATED IMPULSIVE MANEUVER · {(maneuver.Enabled ? "enabled" : "disabled")}\r\n" +
                $"Identifier: {maneuver.Id}\r\n" +
                $"Label: {(string.IsNullOrWhiteSpace(maneuver.Label) ? "(none)" : maneuver.Label)}\r\n" +
                $"Execution ET (TDB): {maneuver.EtSeconds:R} s\r\n" +
                $"Δv frame: J2000 inertial axes\r\n" +
                $"Δv X/Y/Z: {maneuver.DeltaVelocityMetersPerSecond.X:R}, {maneuver.DeltaVelocityMetersPerSecond.Y:R}, {maneuver.DeltaVelocityMetersPerSecond.Z:R} m/s\r\n" +
                $"Δv magnitude: {maneuver.MagnitudeMetersPerSecond:F6} m/s\r\n" +
                $"Pre-maneuver Sun-relative speed: {preSpeed / 1000.0:F6} km/s\r\n" +
                $"Post-maneuver Sun-relative speed: {postSpeed / 1000.0:F6} km/s\r\n" +
                $"Position continuity: {maneuver.PreState.Position.Equals(maneuver.PostState.Position)}\r\n" +
                $"Coordinate origin in map: {maneuver.PostState.OriginId}\r\n" +
                $"Model: {model?.ModelIdentity ?? "idealized two-body"}\r\n" +
                "This is a simulated instantaneous velocity change, not an actual thruster command.\r\n" +
                "Model limits: no multi-body perturbations, finite-duration thrust, mass depletion, or navigation uncertainty.";
            return;
        }
        if (item.Id == "sn8-spacecraft" || item.ModeledSample is not null)
        {
            var trajectory = _scene?.Trajectory;
            if (trajectory is null)
            {
                _details.Text = "SN8 simulated trajectory data is unavailable.";
                return;
            }
            OrbitState sunRelative = item.ModeledSample?.SunRelativeState ?? trajectory.Model.CurrentState;
            CartesianState displayed = item.State;
            var scenario = trajectory.Model.Scenario;
            string sampleKind = item.ModeledSample is null
                ? "Current simulated spacecraft position"
                : $"Trajectory sample: {item.ModeledSample.Id} · segment {item.ModeledSample.SegmentIndex} · {item.ModeledSample.Kind}";
            _details.Text =
                $"SN8 SIMULATED SPACECRAFT · {trajectory.Model.Quality}\r\n" +
                $"Simulation identifier: {scenario.Id}\r\n" +
                $"Central body: Sun (NAIF 10)\r\n" +
                $"Model: {trajectory.Model.ModelIdentity}\r\n" +
                $"Initial ET (TDB): {scenario.InitialState.EtSeconds:R} s\r\n" +
                $"{sampleKind}\r\n" +
                $"State ET (TDB): {sunRelative.EtSeconds:R} s\r\n" +
                $"Current display ET (TDB): {_scene?.Epoch.Value.ToString("R", CultureInfo.InvariantCulture)} s\r\n" +
                $"Sun-relative position X/Y/Z: {sunRelative.PositionMeters.X:R}, {sunRelative.PositionMeters.Y:R}, {sunRelative.PositionMeters.Z:R} m\r\n" +
                $"Sun-relative velocity X/Y/Z: {sunRelative.VelocityMetersPerSecond.X:R}, {sunRelative.VelocityMetersPerSecond.Y:R}, {sunRelative.VelocityMetersPerSecond.Z:R} m/s\r\n" +
                $"Distance from Sun: {sunRelative.PositionMeters.Magnitude / 1000.0:N3} km\r\n" +
                $"Speed relative to Sun: {sunRelative.VelocityMetersPerSecond.Magnitude / 1000.0:F6} km/s\r\n" +
                $"Visualization position: {displayed.Position.X:R}, {displayed.Position.Y:R}, {displayed.Position.Z:R} m\r\n" +
                $"Coordinate frame: J2000 initial model; displayed as {displayed.Orientation}\r\n" +
                $"Coordinate origin: Sun for dynamics; {displayed.OriginId} for map placement\r\n" +
                $"Gravitational parameter: {scenario.GravitationalParameterIdentity} · {trajectory.Model.Provenance}\r\n" +
                $"Simulation provenance: {scenario.Provenance}\r\n" +
                $"Validity: ±{scenario.MaximumHorizonSeconds / TwoBodyPropagator.SecondsPerJulianYear:F1} Julian years from initial ET; at most {TrajectoryGenerator.MaximumManeuvers} maneuvers and {TrajectoryGenerator.DefaultMaximumSamples} samples.\r\n" +
                $"Simulation quality: {displayed.Quality}; numerical implementation was compared with CSPICE, but this state is not a validated reference or observed spacecraft trajectory.\r\n" +
                "Limitations: two-body Sun gravity only; no planetary perturbations, relativity, radiation pressure, finite thrust, mass depletion, or navigation errors.\r\n\r\n" +
                $"State placement provenance:\r\n{displayed.Provenance}";
            return;
        }
        CartesianState s = item.State;
        var scene = _scene;
        double jd = new EtSeconds(s.EtSeconds).ToJulianDateTdb();
        string naif = item.NaifId.HasValue ? $"NAIF ID: {item.NaifId.Value}" : "NAIF ID: none (explicit simulated object)";
        string stateKind = item.IsSimulated ? "SIMULATED / APPROXIMATE" : s.Quality.ToString().ToUpperInvariant();
        string timeInfo = scene?.TimeQuality switch
        {
            TimeConversionQuality.Validated => "UTC time quality: Validated SN5 table",
            TimeConversionQuality.ProvisionalStaleTable => "UTC time quality: PROVISIONAL stale leap-second table",
            _ => "Time input: " + (scene?.EpochRepresentation == TimeRepresentationPrecision.JulianDateDouble
                ? "JD TDB (binary64 representation precision limited)"
                : "direct ET seconds (TDB)"),
        };
        _details.Text =
            $"{item.Name}\r\n" +
            $"{naif}\r\n" +
            $"Identity: {item.CenterClassification}\r\n\r\n" +
            $"Position {s.Orientation} X/Y/Z (km):\r\n  {s.Position.X / 1000.0,18:F3}\r\n  {s.Position.Y / 1000.0,18:F3}\r\n  {s.Position.Z / 1000.0,18:F3}\r\n" +
            $"Velocity {s.Orientation} VX/VY/VZ (km/s):\r\n  {s.Velocity.X / 1000.0,18:F6}\r\n  {s.Velocity.Y / 1000.0,18:F6}\r\n  {s.Velocity.Z / 1000.0,18:F6}\r\n\r\n" +
            $"Distance from scientific origin: {s.Position.Magnitude / 1000.0:N3} km\r\n" +
            $"Scientific origin: {s.OriginId}\r\n" +
            $"Reference axes: {s.Orientation} (map displays X/Y; Z retained)\r\n" +
            $"Epoch ET (TDB): {s.EtSeconds:R} s\r\n" +
            $"Epoch JD TDB: {jd:F9}\r\n" +
            $"State quality: {stateKind}\r\n" +
            $"Simulated contribution: {s.IsSimulated}\r\n" +
            $"{timeInfo}\r\n\r\n" +
            $"Provenance / source:\r\n{s.Provenance}";
    }

    private void UpdateTimeStatus()
    {
        if (_currentEpochResolution is null)
        {
            _status.Text = _timeStatus;
            return;
        }
        string quality = _currentEpochResolution.TimeQuality switch
        {
            TimeConversionQuality.Validated => "UTC conversion: Validated",
            TimeConversionQuality.ProvisionalStaleTable => "UTC conversion: PROVISIONAL (stale leap-second table)",
            _ => _currentEpochResolution.InputKind == EpochInputKind.JulianDateTdb
                ? "Input: JD TDB · binary64 time resolution is limited"
                : "Input: direct ET seconds (TDB)",
        };
        _status.Text = quality;
    }

    private void UpdateMetrics()
    {
        if (_scene is not null)
        {
            string trajectory = _scene.Trajectory is null
                ? "SN8 unavailable"
                : $"SN8 {_scene.Trajectory.Model.Samples.Count}/{TrajectoryGenerator.DefaultMaximumSamples} samples · { _scene.Trajectory.Model.GenerationDuration.TotalMilliseconds:F1} ms";
            _sceneMetrics.Text = $"Scene {_scene.BuildDuration.TotalMilliseconds:F1} ms · trails {_scene.TrailSampleCount}/{SceneBuilder.MaximumTotalTrailSamples} · {trajectory}";
            _cacheMetrics.Text = _ephemeris is null
                ? "Cache n/a"
                : $"Cache {_ephemeris.CachedBytes / 1024.0:F0} KiB · {_ephemeris.CacheHitCount} hit / {_ephemeris.CacheMissCount} miss";
        }
        _memoryMetrics.Text = $"Working set {Process.GetCurrentProcess().WorkingSet64 / (1024.0 * 1024.0):F1} MiB";
        _paintMetrics.Text = $"Paint {_map.LastPaintMilliseconds:F2} ms";
    }

    private void OnPaintMeasured(double milliseconds)
    {
        _paintMetrics.Text = $"Paint {milliseconds:F2} ms";
    }

    private void UpdateTimeControls()
    {
        _allowProvisional.Enabled = _timeConverter is not null && SelectedEpochKind() == EpochInputKind.Utc;
    }

    private void UpdateOriginControls()
    {
        bool spacecraft = SelectedOriginMode() == SceneOriginMode.SimulatedSpacecraft;
        if (SelectedOrientation() == FrameOrientation.IauEarth && spacecraft)
            _orientation.SelectedIndex = 0;
        _orientation.Enabled = _orientationStatus.StartsWith("IAU_EARTH enabled", StringComparison.Ordinal) && !spacecraft;
    }

    private void FitSolarSystem()
    {
        MeasureInteraction(_map.FitSolarSystem);
    }

    private void MeasureInteraction(Action action)
    {
        long start = Stopwatch.GetTimestamp();
        action();
        _lastInteractionTicks = Stopwatch.GetTimestamp() - start;
        _status.Text = $"View interaction updated in {Stopwatch.GetElapsedTime(0, _lastInteractionTicks).TotalMilliseconds:F3} ms; scientific states unchanged.";
    }

    private void OnKeyDown(object? sender, KeyEventArgs e)
    {
        const double pan = 38;
        if (e.KeyCode == Keys.Escape)
        {
            _map.CancelInteraction();
            _status.Text = "Active map interaction canceled.";
            e.Handled = true;
        }
        else if (e.KeyCode == Keys.F)
        {
            FitSolarSystem();
            e.Handled = true;
        }
        else if (e.KeyCode == Keys.Home)
        {
            _map.ResetView();
            e.Handled = true;
        }
        else if (e.KeyCode is Keys.Add or Keys.Oemplus)
        {
            MeasureInteraction(() => _map.ZoomAtCenter(1.22));
            e.Handled = true;
        }
        else if (e.KeyCode is Keys.Subtract or Keys.OemMinus)
        {
            MeasureInteraction(() => _map.ZoomAtCenter(1.0 / 1.22));
            e.Handled = true;
        }
        else if (e.KeyCode == Keys.Left) { MeasureInteraction(() => _map.PanByPixels(pan, 0)); e.Handled = true; }
        else if (e.KeyCode == Keys.Right) { MeasureInteraction(() => _map.PanByPixels(-pan, 0)); e.Handled = true; }
        else if (e.KeyCode == Keys.Up) { MeasureInteraction(() => _map.PanByPixels(0, pan)); e.Handled = true; }
        else if (e.KeyCode == Keys.Down) { MeasureInteraction(() => _map.PanByPixels(0, -pan)); e.Handled = true; }
        e.SuppressKeyPress = e.Handled;
    }

    private SceneOriginMode SelectedOriginMode() => _originMode.SelectedIndex switch
    {
        1 => SceneOriginMode.Earth,
        2 => SceneOriginMode.SimulatedSpacecraft,
        _ => SceneOriginMode.SolarSystemBarycenter,
    };

    private FrameOrientation SelectedOrientation() =>
        _orientation.SelectedIndex == 1 ? FrameOrientation.IauEarth : FrameOrientation.J2000;

    private EpochInputKind SelectedEpochKind() => _epochKind.SelectedIndex switch
    {
        1 => EpochInputKind.JulianDateTdb,
        2 => EpochInputKind.Utc,
        _ => EpochInputKind.EtSeconds,
    };

    private static FlowLayoutPanel NewFlowRow() => new()
    {
        Dock = DockStyle.Fill,
        WrapContents = false,
        AutoScroll = true,
        FlowDirection = FlowDirection.LeftToRight,
        Padding = new Padding(1, 2, 1, 0),
        BackColor = Color.FromArgb(18, 27, 42),
    };

    private static Label NewCaption(string text) => new()
    {
        Text = text,
        AutoSize = true,
        ForeColor = Color.FromArgb(181, 200, 224),
        TextAlign = ContentAlignment.MiddleCenter,
        Padding = new Padding(1, 7, 1, 0),
    };

    private static Button NewButton(string text, EventHandler handler)
    {
        var button = new Button
        {
            Text = text,
            AutoSize = true,
            Height = 27,
            Margin = new Padding(3, 0, 3, 0),
            FlatStyle = FlatStyle.Flat,
            BackColor = Color.FromArgb(38, 55, 78),
            ForeColor = Color.FromArgb(236, 242, 250),
            Padding = new Padding(5, 0, 5, 0),
        };
        button.FlatAppearance.BorderColor = Color.FromArgb(69, 92, 124);
        button.Click += handler;
        return button;
    }

    private static ComboBox NewCombo(int width) => new()
    {
        Width = width,
        DropDownStyle = ComboBoxStyle.DropDownList,
        FlatStyle = FlatStyle.Flat,
        BackColor = Color.FromArgb(25, 38, 57),
        ForeColor = Color.FromArgb(236, 242, 250),
        Margin = new Padding(3, 0, 3, 0),
    };

    private static TextBox NewInput(int width) => new()
    {
        Width = width,
        Height = 24,
        Font = new Font("Consolas", 8.5F),
        TextAlign = HorizontalAlignment.Right,
        BackColor = Color.FromArgb(17, 25, 39),
        ForeColor = Color.FromArgb(225, 233, 245),
        BorderStyle = BorderStyle.FixedSingle,
        Margin = new Padding(2, 2, 2, 2),
    };

    private sealed record ManeuverListEntry(ImpulsiveManeuver Maneuver, string Text);
    private sealed record AnalysisTargetEntry(string Id, string Name);
    private sealed record MissionEventListEntry(MissionEvent Event, string Text);
}
