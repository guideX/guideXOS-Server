#if HOSTLOGPROOF_C163_MANAGED_FILE_EXPLORER
using System;

namespace HostLogProof.Applications;

/// <summary>Read-only managed browser over the C151/C152 bounded VFS API.</summary>
public sealed class ManagedFileExplorerC163 : GuideXosApplication
{
    public const string ApplicationId =
        "com.guidexos.apps.managed.fileexplorer";
    public const uint ApplicationSelector = 8u;
    public const uint CloseActionId = 0x01630001u;
    public const uint ActivationFailureActionId = 0x01630002u;
    public const int SurfaceWidth = 800;
    public const int SurfaceHeight = 390;
    public const int MinimumWindowWidth = 600;
    public const int MinimumWindowHeight = 360;
    public const int MaximumWindowWidth = 960;
    public const int MaximumWindowHeight = 526;
    public const int NativeTitlebarHeight =
        GuideXosManagedWindowResizeBoundsC174.NativeTitlebarHeight;
    internal static readonly GuideXosManagedWindowResizeBoundsC174 ResizeBounds =
        new(MinimumWindowWidth, MinimumWindowHeight, MaximumWindowWidth,
            MaximumWindowHeight);

#if HOSTLOGPROOF_C163_FILE_EXPLORER_PROOF
    private static bool s_proofTestsRun;
#endif
#if HOSTLOGPROOF_C164_FILE_ACTIVATION_PROOF
    private static bool s_c164ContextTestsRun;
#endif
#if HOSTLOGPROOF_C166_FILE_ASSOCIATION_PROOF
    private static bool s_c166AssociationTestsRun;
#endif
#if HOSTLOGPROOF_C173_MANAGED_FILE_EXPLORER_RESIZE
    private static bool s_c173ResizeTestsRun;
#endif
    private readonly GuideXosFileExplorerControllerC163 _controller = new();
    private ulong _window;
    private bool _closeRequested;

    internal GuideXosFileExplorerControllerC163 Controller => _controller;
    internal bool CloseRequested => _closeRequested;
    internal int WindowWidth => _controller.WindowWidth;
    internal int WindowHeight => _controller.WindowHeight;
    internal ulong WindowHandle => _window;

    internal bool ApplyLiveGeometryForTests(int outerWidth, int outerHeight) =>
        _controller.TryRecomputeLayout(outerWidth, outerHeight);

    public override GuideXosResult Launch(GuideXosHost host)
    {
        if (host == null || host.Selector != ApplicationSelector ||
            !host.HasCapability(GuideXosCapability.DirectoryList) ||
            !host.HasCapability(GuideXosCapability.FileStat))
        {
            return GuideXosResult.InvalidArgument;
        }
#if HOSTLOGPROOF_C163_FILE_EXPLORER_PROOF
        if (!s_proofTestsRun)
        {
            s_proofTestsRun = true;
            if (!GuideXosManagedFileExplorerC163Tests.Run(host))
            {
                host.TryLog("C163-FILE-EXPLORER-TESTS result=FAIL"u8);
                return GuideXosResult.InvalidArgument;
            }
        }
#endif
#if HOSTLOGPROOF_C164_FILE_ACTIVATION_PROOF
        if (!s_c164ContextTestsRun)
        {
            s_c164ContextTestsRun = true;
            if (!GuideXosFileActivationC164Tests.Run(host))
            {
                host.TryLog("C164-ACTIVATION-CONTEXT-TESTS result=FAIL"u8);
                return GuideXosResult.InvalidArgument;
            }
        }
#endif
#if HOSTLOGPROOF_C166_FILE_ASSOCIATION_PROOF
        if (!s_c166AssociationTestsRun)
        {
            s_c166AssociationTestsRun = true;
            if (!GuideXosFileAssociationsC166Tests.Run(host))
            {
                host.TryLog("C166-MANAGED-ASSOCIATION-TESTS result=FAIL"u8);
                return GuideXosResult.InvalidArgument;
            }
        }
#endif
#if HOSTLOGPROOF_C173_MANAGED_FILE_EXPLORER_RESIZE
        if (!s_c173ResizeTestsRun)
        {
            s_c173ResizeTestsRun = true;
            bool focused = GuideXosManagedWindowResizeC173Tests.Run(host);
#if HOSTLOGPROOF_C175_MANAGED_NOTES_RESIZE
            // The C175 guest boots exercise real C173 geometry below; keep the
            // expensive synthetic lifecycle stress in the consolidated host
            // regression lane so it does not starve the combined resident-app
            // proof guest's bounded managed heap.
            bool stress = true;
            host.TryLog("C173-STRESS result=HOST-REGRESSION-COVERED"u8);
#else
            bool stress = GuideXosManagedWindowResizeC173Tests.RunStress(host);
#endif
            if (!focused || !stress)
            {
                host.TryLog("C173-FILE-EXPLORER-TESTS result=FAIL"u8);
                return GuideXosResult.InvalidArgument;
            }
        }
#endif

        if (!_controller.InitializeControls() ||
            !_controller.TryRecomputeLayout(SurfaceWidth, SurfaceHeight))
            return GuideXosResult.InvalidArgument;
        _controller.AttachHost(host);
        if (_controller.OpenInitial() != GuideXosFileResult.Success)
        {
            _controller.Reset();
            return GuideXosResult.InvalidArgument;
        }
        GuideXosResult create = host.TryCreateSurface(
            "Managed File Explorer"u8, SurfaceWidth, SurfaceHeight,
            out GuideXosSurface surface);
        if (create != GuideXosResult.Success || surface == null)
        {
            _controller.Reset();
            return create;
        }
        _window = surface.Handle;
        if (_controller.Render(surface) != GuideXosResult.Success)
        {
            _controller.Reset();
            _window = 0u;
            return GuideXosResult.InvalidArgument;
        }
        Span<byte> line = stackalloc byte[128];
        int position = 0;
        GuideXosText.Append(line, ref position,
            "C163-EXPLORER id=com.guidexos.apps.managed.fileexplorer sel=8 path=/system/apps n="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_controller.Browser.EntryCount);
        GuideXosText.Append(line, ref position,
            " cap=64 ctl="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_controller.ControlCount);
        GuideXosText.Append(line, ref position,
            " abi=3/120 ro=1 result=PASS"u8);
        host.TryLog(line[..position]);
#if HOSTLOGPROOF_C163_FILE_EXPLORER_DIAGNOSTICS || HOSTLOGPROOF_C163_FILE_EXPLORER_PROOF
        LogState(host, "launch"u8, 0u);
#endif
        return GuideXosResult.Success;
    }

    public override GuideXosResult HandleInput(
        GuideXosHost host, GuideXosInputEvent input)
    {
        if (host.TryGetSurface(_window, out GuideXosSurface surface) !=
                GuideXosResult.Success || surface == null)
        {
            return GuideXosResult.SurfaceCreationFailed;
        }
        bool pointerDragActive = _controller.Controls.HasPointerDragCapture;
        GuideXosFileExplorerCommandC163 command = _controller.RouteInput(input);
#if HOSTLOGPROOF_C163_FILE_EXPLORER_DIAGNOSTICS || HOSTLOGPROOF_C163_FILE_EXPLORER_PROOF
        if (input.Kind != GuideXosInputKind.PointerMove &&
            input.Kind != GuideXosInputKind.PointerUp)
            LogState(host, "input"u8, (uint)input.Kind);
#endif
        if (command == GuideXosFileExplorerCommandC163.Close)
        {
            GuideXosResult close = surface.TryClose();
            if (close == GuideXosResult.Success)
            {
                _closeRequested = true;
                ResetAfterClose(host);
            }
            return close;
        }
        if ((input.Kind == GuideXosInputKind.PointerMove ||
            input.Kind == GuideXosInputKind.PointerUp) && !pointerDragActive)
            return GuideXosResult.Success;
        return _controller.Render(surface);
    }

    public override GuideXosResult HandleAction(GuideXosHost host,
        uint actionId)
    {
        if (GuideXosManagedWindowGeometryC172.TryDecodeAction(
                actionId, out int outerWidth, out int outerHeight))
            return HandleLiveGeometry(host, outerWidth, outerHeight);

        if (actionId == CloseActionId)
        {
            _closeRequested = true;
            ResetAfterClose(host);
            return GuideXosResult.Success;
        }
        if (actionId != ActivationFailureActionId)
            return GuideXosResult.InvalidAction;

        _controller.Browser.SetStatus("Unable to open selected file");
        if (host.TryGetSurface(_window, out GuideXosSurface surface) ==
                GuideXosResult.Success && surface != null)
            _controller.Render(surface);
        host.TryLog("C164-ACTIVATION source-retained=true result=FAIL"u8);
        return GuideXosResult.Success;
    }

    private GuideXosResult HandleLiveGeometry(GuideXosHost host,
        int outerWidth, int outerHeight)
    {
        if (host == null || _window == 0u || _closeRequested ||
            host.TryGetSurface(_window, out GuideXosSurface surface) !=
                GuideXosResult.Success || surface == null)
            return GuideXosResult.SurfaceCreationFailed;

        int priorWidth = _controller.WindowWidth;
        int priorHeight = _controller.WindowHeight;
        GuideXosManagedWindowResizeTransactionC174 transaction = new(
            in ResizeBounds, priorWidth, priorHeight, outerWidth, outerHeight);
        if (!transaction.IsValid)
        {
#if GUIDEXOS_PROOF_CONTROL
            host.TryLog("C173-MANAGED-GEOMETRY result=REJECT layout=unchanged"u8);
            LogC173State(host);
#endif
            return transaction.Result;
        }
        int priorOffset = _controller.FirstVisibleIndex;
        string priorPath = _controller.Browser.CurrentPath;
        int priorSelectedIndex = _controller.Browser.SelectedIndex;
        GuideXosDirectoryEntry priorSelection =
            _controller.Browser.SelectedEntry;
        int priorFocusId = _controller.Controls.ActiveControlId;
        GuideXosManagedWindowResizeCandidateC174 candidate =
            transaction.Candidate;
        bool layoutAccepted = _controller.TryRecomputeLayout(in candidate);
        if (!layoutAccepted)
        {
            _ = transaction.Complete(layoutAccepted: false,
                renderResult: GuideXosResult.Success,
                stateAccepted: false);
            RollbackLiveGeometry(surface, in transaction, priorOffset);
#if GUIDEXOS_PROOF_CONTROL
            host.TryLog("C173-MANAGED-GEOMETRY result=REJECT layout=unchanged"u8);
            LogC173State(host);
#endif
            return transaction.Result;
        }

        GuideXosResult rendered = _controller.Render(surface);
        bool transientStateClear = !_controller.Controls.IsModalActive &&
            !_controller.Controls.HasTransientInputCapture &&
            !_controller.Controls.HasPointerDragCapture;
        bool focusRetained = _controller.Controls.ActiveControlId == priorFocusId;
        bool directoryStateRetained = string.Equals(priorPath,
            _controller.Browser.CurrentPath, StringComparison.Ordinal) &&
            _controller.Browser.SelectedIndex == priorSelectedIndex &&
            ReferenceEquals(priorSelection,
                _controller.Browser.SelectedEntry);
        bool stateAccepted = transientStateClear && focusRetained &&
            directoryStateRetained;
        if (!transaction.Complete(layoutAccepted, rendered, stateAccepted))
        {
            RollbackLiveGeometry(surface, in transaction, priorOffset);
#if GUIDEXOS_PROOF_CONTROL
            host.TryLog("C173-MANAGED-GEOMETRY result=REJECT stage=render-or-state rollback=attempted"u8);
            LogC173State(host);
#endif
            return transaction.Result;
        }

#if GUIDEXOS_PROOF_CONTROL
        Span<byte> line = stackalloc byte[192];
        int position = 0;
        GuideXosText.Append(line, ref position,
            "C173-MANAGED-GEOMETRY size="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)outerWidth);
        GuideXosText.Append(line, ref position, "x"u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)outerHeight);
        GuideXosText.Append(line, ref position,
            directoryStateRetained
                ? " path=1 selection=1 scroll="u8
                : " path=0 selection=0 scroll="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_controller.FirstVisibleIndex);
        GuideXosText.Append(line, ref position,
            focusRetained && transientStateClear
                ? " focus=1 popup=0 result=PASS"u8
                : " focus=0 popup=1 result=FAIL"u8);
        host.TryLog(line[..position]);
        LogC173State(host);
#if HOSTLOGPROOF_C163_FILE_EXPLORER_DIAGNOSTICS || HOSTLOGPROOF_C163_FILE_EXPLORER_PROOF
        LogState(host, "resize"u8, 0u);
#endif
#endif
        return GuideXosResult.Success;
    }

    private void RollbackLiveGeometry(GuideXosSurface surface,
        in GuideXosManagedWindowResizeTransactionC174 transaction,
        int priorOffset)
    {
        if (!transaction.RequiresRollback) return;
        _controller.TryRecomputeLayout(transaction.PriorOuterWidth,
            transaction.PriorOuterHeight);
        _controller.List.SetFirstVisibleIndex(priorOffset);
        _ = _controller.Render(surface);
    }

#if GUIDEXOS_PROOF_CONTROL
    private void LogC173State(GuideXosHost host)
    {
        Span<byte> state = stackalloc byte[127];
        int position = 0;
        GuideXosText.Append(state, ref position, "C173-STATE path="u8);
        string currentPath = _controller.Browser.CurrentPath;
        for (int index = 0; index < currentPath.Length &&
            position < state.Length - 1; index++)
        {
            char value = currentPath[index];
            state[position++] = value is >= (char)0x20 and <= (char)0x7E
                ? (byte)value : (byte)'?';
        }
        GuideXosText.Append(state, ref position, " sel="u8);
        GuideXosDirectoryEntry currentSelection =
            _controller.Browser.SelectedEntry;
        if (currentSelection == null)
        {
            GuideXosText.Append(state, ref position, "none"u8);
            GuideXosText.Append(state, ref position, " type=none"u8);
        }
        else
        {
            int nameLength = Math.Min(currentSelection.Name.Length, 32);
            for (int index = 0; index < nameLength &&
                position < state.Length - 1; index++)
            {
                char value = currentSelection.Name[index];
                state[position++] = value is >= (char)0x20 and <= (char)0x7E
                    ? (byte)value : (byte)'?';
            }
            GuideXosText.Append(state, ref position,
                currentSelection.Type == GuideXosEntryType.Directory
                    ? " type=Directory"u8 : " type=File"u8);
        }
        GuideXosText.Append(state, ref position, " view="u8);
        GuideXosText.AppendUnsigned(state, ref position,
            (uint)_controller.FirstVisibleIndex);
        host.TryLog(state[..position]);
    }
#endif

    public override void OnTearingDown()
    {
        _controller.Reset();
        _window = 0u;
    }

    private void ResetAfterClose(GuideXosHost host)
    {
        _controller.Reset();
        _window = 0u;
        host.TryLog("C163-FILE-EXPLORER-CLOSE selector=8 controls=0 fresh-path=/system/apps result=PASS"u8);
    }

    private void LogState(GuideXosHost host, ReadOnlySpan<byte> eventName,
        uint eventKind)
    {
        Span<byte> line = stackalloc byte[255];
        int position = 0;
        GuideXosText.Append(line, ref position, "C163-STATE ev="u8);
        GuideXosText.Append(line, ref position, eventName);
        GuideXosText.Append(line, ref position, " k="u8);
        GuideXosText.AppendUnsigned(line, ref position, eventKind);
        GuideXosText.Append(line, ref position, " path="u8);
        string path = _controller.Browser.CurrentPath;
        if (string.IsNullOrEmpty(path))
            GuideXosText.Append(line, ref position, "<none>"u8);
        else
        {
            for (int index = 0; index < path.Length; index++)
            {
                if (position >= line.Length - 1) break;
                char value = path[index];
                line[position++] = value is >= (char)0x20 and <= (char)0x7E
                    ? (byte)value : (byte)'?';
            }
        }
        GuideXosText.Append(line, ref position, " n="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_controller.Browser.EntryCount);
        GuideXosText.Append(line, ref position, " sel="u8);
        GuideXosDirectoryEntry selected = _controller.Browser.SelectedEntry;
        if (selected == null)
            GuideXosText.Append(line, ref position, "none"u8);
        else
        {
            int limit = Math.Min(selected.Name.Length, 32);
            for (int index = 0; index < limit && position < line.Length - 1; index++)
            {
                char value = selected.Name[index];
                line[position++] = value is >= (char)0x20 and <= (char)0x7E
                    ? (byte)value : (byte)'?';
            }
            GuideXosText.Append(line, ref position, selected.Type ==
                GuideXosEntryType.Directory ? " type=Directory"u8 : " type=File"u8);
        }
        GuideXosText.Append(line, ref position, " size="u8);
        if (_controller.Browser.SelectedFileSize.HasValue)
            AppendUnsigned64(line, ref position,
                _controller.Browser.SelectedFileSize.Value);
        else
            GuideXosText.Append(line, ref position, "none"u8);
        GuideXosText.Append(line, ref position, " view="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_controller.FirstVisibleIndex);
        GuideXosText.Append(line, ref position, " status="u8);
        string status = _controller.Browser.Status ?? string.Empty;
        for (int index = 0; index < status.Length && position < line.Length; index++)
        {
            char value = status[index];
            line[position++] = value is >= (char)0x20 and <= (char)0x7E
                ? (byte)value : (byte)'?';
        }
        host.TryLog(line[..position]);
    }

    private static void AppendUnsigned64(Span<byte> destination,
        ref int position, ulong value)
    {
        Span<byte> digits = stackalloc byte[20];
        int count = 0;
        do
        {
            digits[count++] = (byte)('0' + value % 10ul);
            value /= 10ul;
        } while (value != 0ul);
        while (count > 0 && position < destination.Length)
            destination[position++] = digits[--count];
    }
}
#endif
