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
    private readonly Stopwatch _startup;
    private readonly SolarMapControl _map = new();
    private readonly ComboBox _epochKind = NewCombo(180);
    private readonly TextBox _epochText = new() { Width = 150, Text = "0", Font = new Font("Consolas", 9F) };
    private readonly CheckBox _allowProvisional = new() { Text = "Allow provisional future UTC", AutoSize = true };
    private readonly ComboBox _originMode = NewCombo(190);
    private readonly ComboBox _orientation = NewCombo(190);
    private readonly CheckBox _trails = new() { Text = "Sampled geometric trails", AutoSize = true };
    private readonly ListBox _objects = new() { Dock = DockStyle.Top, Height = 220, IntegralHeight = false };
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
    private long _lastInteractionTicks;
    private bool _isPlaying;

    public MainForm(SceneBuilder? sceneBuilder, IndexedChebyshevEphemerisProvider? ephemeris,
        TimeConverter? timeConverter, string? ephemerisError, string timeStatus,
        string orientationStatus, Stopwatch startup)
    {
        _sceneBuilder = sceneBuilder;
        _ephemeris = ephemeris;
        _timeConverter = timeConverter;
        _epochResolver = new EpochInputResolver(timeConverter);
        _ephemerisError = ephemerisError;
        _timeStatus = timeStatus;
        _orientationStatus = orientationStatus;
        _startup = startup;

        Text = "Stellar Navigator — SN7 Interactive Map";
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
        _orientation.Enabled = orientationStatus.StartsWith("IAU_EARTH enabled", StringComparison.Ordinal);
        _allowProvisional.Enabled = timeConverter is not null;
        _allowProvisional.Checked = false;
        _allowProvisional.ForeColor = Color.FromArgb(181, 200, 224);
        _allowProvisional.BackColor = Color.FromArgb(18, 27, 42);
        _trails.ForeColor = Color.FromArgb(181, 200, 224);
        _trails.BackColor = Color.FromArgb(18, 27, 42);
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
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 225));
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 35));
        var title = new Label { Text = "OBJECTS & SCIENTIFIC STATE", Dock = DockStyle.Fill, ForeColor = Color.FromArgb(186, 208, 237), Font = new Font("Segoe UI Semibold", 9.5F, FontStyle.Bold), TextAlign = ContentAlignment.MiddleLeft };
        right.Controls.Add(title, 0, 0);
        _objects.BackColor = Color.FromArgb(17, 25, 39);
        _objects.ForeColor = Color.FromArgb(225, 233, 245);
        _objects.BorderStyle = BorderStyle.FixedSingle;
        right.Controls.Add(_objects, 0, 1);
        var detailsContainer = new Panel { Dock = DockStyle.Fill, Padding = new Padding(0, 10, 0, 0) };
        detailsContainer.Controls.Add(_details);
        right.Controls.Add(detailsContainer, 0, 2);
        var buttonRow = NewFlowRow();
        buttonRow.Controls.Add(NewButton("Center on object", (_, _) => _map.CenterOnSelected()));
        right.Controls.Add(buttonRow, 0, 3);
        split.Panel2.Controls.Add(right);
        split.SizeChanged += (_, _) =>
        {
            if (split.Width < 800)
                return;
            split.Panel1MinSize = 420;
            split.Panel2MinSize = 330;
            int desired = split.Width - 370;
            if (desired > split.Panel1MinSize && desired < split.Width - split.Panel2MinSize)
                split.SplitterDistance = desired;
        };
        _map.Dock = DockStyle.Fill;
        return split;
    }

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

    private void RequestSceneBuild()
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
        var request = new SceneBuildRequest(resolution.Epoch,
            SelectedOriginMode(), SelectedOrientation(),
            IncludeMoon: true, IncludeTrails: _trails.Checked,
            TimeQuality: resolution.TimeQuality,
            EpochRepresentation: resolution.RepresentationPrecision);
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
            _map.SetScene(result);
            _epochText.Text = result.Epoch.Value.ToString("R", CultureInfo.InvariantCulture);
            if (_originMode.SelectedIndex != (int)result.OriginMode)
                _originMode.SelectedIndex = (int)result.OriginMode;
            RefreshObjectList(result);
            UpdateTimeStatus();
            _status.Text = $"Scene ready · {result.Objects.Count - 1} ephemeris objects + explicit simulation · {result.Orientation} X/Y projection · {result.ScientificOriginId}";
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
            _objects.DisplayMember = nameof(SceneObject.Name);
            int selected = scene.Objects.ToList().FindIndex(x => x.Id == _map.SelectedId);
            _objects.SelectedIndex = selected >= 0 ? selected : scene.Objects.ToList().FindIndex(x => x.Id == "earth");
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

    private void UpdateDetails(SceneObject? item)
    {
        if (item is null)
        {
            _details.Text = "Select a celestial marker or object from the list.";
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
            return;
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
            _sceneMetrics.Text = $"Scene {_scene.BuildDuration.TotalMilliseconds:F1} ms · trails {_scene.TrailSampleCount}/{SceneBuilder.MaximumTotalTrailSamples}";
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
}
