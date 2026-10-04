#if HOSTLOGPROOF_C161_MANAGED_TASK_MANAGER
using System;

namespace HostLogProof.Applications;

/// <summary>
/// Bounded observer over the C160 application snapshot with one C162
/// exact-identity close operation. C160 remains read-only.
/// </summary>
public sealed unsafe class ManagedTaskManagerC161 : GuideXosApplication
{
    public const string ApplicationId = "com.guidexos.apps.managed.taskmanager";
    public const uint ApplicationSelector = 7u;
    public const uint CloseActionId = 0x01610001u;
    public const uint CloseCompletedActionId = 0x01620001u;
    public const int SurfaceWidth = 800;
    public const int SurfaceHeight = 370;

#if HOSTLOGPROOF_C161_TASK_MANAGER_PROOF
    private static bool s_proofTestsRun;
#endif
#if HOSTLOGPROOF_C162_TASK_MANAGER_CLOSE_PROOF
    private static bool s_closeProofTestsRun;
#endif
    private readonly GuideXosTaskManagerControllerC161 _controller = new();
    private readonly GuideXosDialog _closeDialog = new(
        200, 104, 400, 160, "Confirm application close", 2);
    private readonly GuideXosButton _confirmCloseButton = new(
        300, 232, 100, 20, "Close");
    private readonly GuideXosButton _cancelCloseButton = new(
        412, 232, 100, 20, "Cancel");
    private ulong _window;
    private readonly GuideXosPendingCloseTargetC162 _pendingCloseTarget = new();
    private bool _closePending;

    internal int ControlCount => _controller.ControlCount;
    internal GuideXosTaskManagerControllerC161 Controller => _controller;

    public override GuideXosResult Launch(GuideXosHost host)
    {
        if (host.Selector != ApplicationSelector ||
            !GuideXosApplicationRegistry.TryFind(ApplicationSelector, out _))
            return GuideXosResult.InvalidArgument;

#if HOSTLOGPROOF_C161_TASK_MANAGER_PROOF
        Span<byte> clipboardBeforeLaunch =
            stackalloc byte[GuideXosClipboard.Capacity];
        GuideXosClipboard sharedClipboard = GuideXosClipboard.Shared;
        bool clipboardCaptured = sharedClipboard.TryCopyText(
            clipboardBeforeLaunch, out int clipboardLength);
        if (!s_proofTestsRun)
        {
            s_proofTestsRun = true;
            if (!GuideXosManagedTaskManagerC161Tests.Run(host))
            {
                host.TryLog("C161-TM-TESTS result=FAIL"u8);
                return GuideXosResult.InvalidArgument;
            }
        }
#endif

#if HOSTLOGPROOF_C162_TASK_MANAGER_CLOSE_PROOF
        if (!s_closeProofTestsRun)
        {
            s_closeProofTestsRun = true;
            if (!GuideXosApplicationControlC162Tests.Run(host))
            {
                host.TryLog("C162-TM-CLOSE-ABI result=FAIL"u8);
                return GuideXosResult.InvalidArgument;
            }
        }
#endif

        if (!_controller.InitializeControls())
            return GuideXosResult.InvalidArgument;

        GuideXosResult create = host.TryCreateSurface(
            "Managed Task Manager"u8, SurfaceWidth, SurfaceHeight,
            out GuideXosSurface surface);
        if (create != GuideXosResult.Success || surface == null)
        {
            _controller.Reset();
            return create;
        }
        _window = surface.Handle;

        GuideXosApplicationSnapshotResult snapshotResult =
            host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot snapshot);
        _controller.ApplySnapshot(snapshotResult, in snapshot);
#if HOSTLOGPROOF_C162_TASK_MANAGER_CLOSE_PROOF
        if (!GuideXosApplicationControlC162Tests.RunNativeBoundary(host))
        {
            host.TryLog("C162-CLOSE-NATIVE-BOUNDARY result=FAIL"u8);
            return GuideXosResult.InvalidArgument;
        }
#endif
        if (_controller.Render(surface) != GuideXosResult.Success)
        {
            _controller.Reset();
            _window = 0u;
            return GuideXosResult.InvalidArgument;
        }

#if HOSTLOGPROOF_C161_TASK_MANAGER_PROOF
        bool clipboardPreserved = clipboardCaptured &&
            sharedClipboard.Length == clipboardLength &&
            sharedClipboard.TextSpan.SequenceEqual(
                clipboardBeforeLaunch[..clipboardLength]);
        host.TryLog(clipboardPreserved
            ? "C161-TM-CLIPBOARD stage=launch preserved=true result=PASS"u8
            : "C161-TM-CLIPBOARD stage=launch preserved=false result=FAIL"u8);
        if (!clipboardPreserved) return GuideXosResult.InvalidArgument;
#endif

        LogLaunch(host, snapshotResult, in snapshot);
        LogSelectedDetail(host);
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

        if (_closeDialog.IsOpen)
        {
            _closeDialog.HandleInput(input);
            if (!_closeDialog.IsOpen)
            {
                if (_closeDialog.Result == GuideXosDialogResult.Yes)
                    CompleteCloseApplication(host);
                else
                    CancelCloseApplication(host);
            }
            return RenderWithDialog(surface);
        }

        // This input fallback consumes a bounded close completion if the
        // native after-dispatch notification could not be delivered.
        if (_closePending)
        {
            PollPendingCloseApplication(host);
            return _controller.Render(surface);
        }

        GuideXosTaskManagerCommandC161 command = _controller.RouteInput(input);

        if (input.Kind == GuideXosInputKind.PointerDown &&
            input.Button == GuideXosPointerButton.Primary &&
            input.X >= GuideXosTaskManagerControllerC161.ListX &&
            input.X < GuideXosTaskManagerControllerC161.ListX +
                _controller.List.Width &&
            input.Y >= GuideXosTaskManagerControllerC161.ListY &&
            input.Y < GuideXosTaskManagerControllerC161.ListY +
                _controller.List.Height)
        {
            LogSelection(host, "pointer"u8);
            LogSelectedDetail(host);
        }
        if (input.Kind == GuideXosInputKind.KeyDown &&
            input.KeyCode == (uint)GuideXosTextInputKey.Tab)
        {
            LogFocus(host, input.Shift);
        }
        if (input.Kind == GuideXosInputKind.KeyDown &&
            (input.KeyCode == (uint)GuideXosTextInputKey.Up ||
             input.KeyCode == (uint)GuideXosTextInputKey.Down ||
             input.KeyCode == (uint)GuideXosTextInputKey.Home ||
             input.KeyCode == (uint)GuideXosTextInputKey.End))
        {
            LogSelection(host, "keyboard"u8);
            LogSelectedDetail(host);
        }
        if (command == GuideXosTaskManagerCommandC161.Refresh)
        {
            RefreshAndRender(host, surface, input.Control
                ? "Ctrl+R"u8 : "button/keyboard"u8);
        }
        else if (command == GuideXosTaskManagerCommandC161.CloseApplication)
        {
            if (!OpenCloseConfirmation(host, surface))
                _controller.SetOperationStatus(
                    "Select an eligible application".AsSpan());
            return RenderWithDialog(surface);
        }
        else if (command == GuideXosTaskManagerCommandC161.Close)
        {
            // The native close callback dispatches CloseActionId after the
            // window is actually closed, clearing this managed lifetime.
            return surface.TryClose();
        }
        else if (_controller.Render(surface) != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }

        return GuideXosResult.Success;
    }

    public override GuideXosResult HandleAction(GuideXosHost host, uint actionId)
    {
        if (actionId == CloseCompletedActionId)
        {
            if (!_closePending) return GuideXosResult.Success;
            if (host.TryGetSurface(_window, out GuideXosSurface surface) !=
                    GuideXosResult.Success || surface == null)
                return GuideXosResult.SurfaceCreationFailed;
            PollPendingCloseApplication(host);
            return _controller.Render(surface);
        }

        if (actionId != CloseActionId) return GuideXosResult.InvalidAction;
        _controller.Reset();
        _pendingCloseTarget.Clear();
        _closePending = false;
        if (_closeDialog.IsOpen)
            _closeDialog.Close(GuideXosDialogResult.Cancel);
        _window = 0u;
        host.TryLog("C161-TM-CLOSE controls=0 selection=none result=PASS"u8);
        return GuideXosResult.Success;
    }

    public override void OnTearingDown()
    {
        if (_closeDialog.IsOpen)
            _closeDialog.Close(GuideXosDialogResult.Cancel);
        _pendingCloseTarget.Clear();
        _closePending = false;
        _controller.Reset();
        _window = 0u;
    }

    private bool OpenCloseConfirmation(GuideXosHost host,
        GuideXosSurface surface)
    {
        if (!_controller.CloseApplicationButton.EffectiveEnabled ||
            !_controller.TryGetSelectedRecord(
                out GuideXosApplicationSnapshotRecord record, out _))
            return false;

        if (!_pendingCloseTarget.Capture(record.Identity,
                record.GetDisplayName())) return false;
        string message = "Close \"" + _pendingCloseTarget.DisplayName +
            "\"?\nUnsaved changes may be lost.";
        if (!_closeDialog.TrySetTitle("Confirm application close") ||
            !_closeDialog.TrySetMessage(message) ||
            !_closeDialog.TryClearMembers() ||
            !_confirmCloseButton.TrySetBounds(300, 232, 100, 20) ||
            !_confirmCloseButton.SetLabel("Close") ||
            !_cancelCloseButton.TrySetBounds(412, 232, 100, 20) ||
            !_cancelCloseButton.SetLabel("Cancel") ||
            !_closeDialog.TryAddMember(_confirmCloseButton) ||
            !_closeDialog.TrySetButtonResult(
                _confirmCloseButton, GuideXosDialogResult.Yes) ||
            !_closeDialog.TryAddMember(_cancelCloseButton) ||
            !_closeDialog.TrySetButtonResult(
                _cancelCloseButton, GuideXosDialogResult.Cancel) ||
            !_closeDialog.TrySetDefaultButton(_confirmCloseButton) ||
            !_closeDialog.TrySetCancelResult(GuideXosDialogResult.Cancel) ||
            !_closeDialog.Open(_controller.Controls))
        {
            _pendingCloseTarget.Clear();
            return false;
        }

        host.TryLog("C162-TM-CONFIRM open=true identity=captured modal=true result=PASS"u8);
        return RenderWithDialog(surface) == GuideXosResult.Success;
    }

    private void CompleteCloseApplication(GuideXosHost host)
    {
        if (!_pendingCloseTarget.TryGet(
                out GuideXosApplicationInstanceId identity, out _))
        {
            _controller.SetOperationStatus(
                "No close request is pending".AsSpan());
            return;
        }

        GuideXosApplicationCloseResult closeResult = identity.Value == 0u
            ? GuideXosApplicationCloseResult.InvalidArgument
            : host.TryCloseApplication(identity);
        if (closeResult == GuideXosApplicationCloseResult.Pending)
        {
            _closePending = true;
            _controller.SetOperationStatus(
                "Application close is in progress".AsSpan());
            LogPendingClose(host, identity);
            return;
        }

        _pendingCloseTarget.Clear();
        ApplyCloseResult(host, identity, closeResult);
    }

    private void PollPendingCloseApplication(GuideXosHost host)
    {
        if (!_closePending || !_pendingCloseTarget.TryGet(
                out GuideXosApplicationInstanceId identity, out _)) return;

        GuideXosApplicationCloseResult closeResult =
            host.TryCloseApplication(identity);
        if (closeResult == GuideXosApplicationCloseResult.Pending)
        {
            _controller.SetOperationStatus(
                "Application close is in progress".AsSpan());
            return;
        }

        _closePending = false;
        _pendingCloseTarget.Clear();
        ApplyCloseResult(host, identity, closeResult);
    }

    private void ApplyCloseResult(GuideXosHost host,
        GuideXosApplicationInstanceId identity,
        GuideXosApplicationCloseResult closeResult)
    {
        GuideXosApplicationSnapshotResult refreshResult =
            host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot snapshot);
        bool applied = _controller.ApplySnapshot(refreshResult, in snapshot);
        bool remains = applied && SnapshotContains(in snapshot, identity);

        bool freshComplete = refreshResult ==
            GuideXosApplicationSnapshotResult.Success && applied;
        if (closeResult == GuideXosApplicationCloseResult.Success &&
            freshComplete && !remains)
        {
            _controller.SetOperationStatus("Application closed".AsSpan());
        }
        else if (freshComplete &&
                 closeResult is GuideXosApplicationCloseResult.NotFound or
                    GuideXosApplicationCloseResult.StaleIdentity)
        {
            _controller.SetOperationStatus(
                "Application is no longer available".AsSpan());
        }
        else if (closeResult == GuideXosApplicationCloseResult.Success && remains)
        {
            _controller.SetOperationStatus(
                "Close accepted; target remains in snapshot".AsSpan());
        }
        else if (closeResult == GuideXosApplicationCloseResult.Success &&
                 !freshComplete)
        {
            _controller.SetOperationStatus(
                "Closed; application list refresh failed".AsSpan());
        }
        else
        {
            _controller.SetOperationStatus(CloseErrorStatus(closeResult));
        }

        bool closeOutcomeValidated = freshComplete &&
            (closeResult == GuideXosApplicationCloseResult.Success
                ? !remains
                : closeResult is GuideXosApplicationCloseResult.NotFound or
                    GuideXosApplicationCloseResult.StaleIdentity
                    ? !remains
                    : remains);

        Span<byte> line = stackalloc byte[128];
        int position = 0;
        GuideXosText.Append(line, ref position,
            "C162-TM-CLOSE identity="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)identity.Source);
        GuideXosText.Append(line, ref position, ":"u8);
        AppendIdentityValue(line, ref position, identity.Value);
        GuideXosText.Append(line, ref position, " result="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)closeResult);
        GuideXosText.Append(line, ref position, " fresh="u8);
        GuideXosText.Append(line, ref position,
            applied ? "true"u8 : "false"u8);
        GuideXosText.Append(line, ref position, " remains="u8);
        GuideXosText.Append(line, ref position, remains ? "true"u8 : "false"u8);
        GuideXosText.Append(line, ref position, " result="u8);
        GuideXosText.Append(line, ref position,
            closeOutcomeValidated ? "PASS"u8 : "FAIL"u8);
        host.TryLog(line[..position]);
        LogSelectedDetail(host);
    }

    private static void LogPendingClose(GuideXosHost host,
        GuideXosApplicationInstanceId identity)
    {
        Span<byte> line = stackalloc byte[96];
        int position = 0;
        GuideXosText.Append(line, ref position,
            "C162-TM-CLOSE-PENDING identity="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)identity.Source);
        GuideXosText.Append(line, ref position, ":"u8);
        AppendIdentityValue(line, ref position, identity.Value);
        GuideXosText.Append(line, ref position, " result=Pending"u8);
        host.TryLog(line[..position]);
    }

    private void CancelCloseApplication(GuideXosHost host)
    {
        _pendingCloseTarget.Clear();
        _closePending = false;
        _controller.SetOperationStatus("Close cancelled".AsSpan());
        host.TryLog("C162-TM-CANCEL mutation=none selection=preserved result=PASS"u8);
    }

    private GuideXosResult RenderWithDialog(GuideXosSurface surface)
    {
        GuideXosResult rendered = _controller.Render(surface);
        return rendered != GuideXosResult.Success ? rendered :
            _closeDialog.Render(surface);
    }

    private static bool SnapshotContains(
        in GuideXosApplicationSnapshot snapshot,
        GuideXosApplicationInstanceId identity)
    {
        for (uint index = 0u; index < snapshot.Count; ++index)
        {
            if (snapshot.TryGetRecord(index,
                    out GuideXosApplicationSnapshotRecord record) &&
                record.Identity == identity) return true;
        }
        return false;
    }

    private static ReadOnlySpan<char> CloseErrorStatus(
        GuideXosApplicationCloseResult result) => result switch
    {
        GuideXosApplicationCloseResult.NotSupported =>
            "Application close requires host ABI v3".AsSpan(),
        GuideXosApplicationCloseResult.CapabilityUnavailable =>
            "Application close capability unavailable".AsSpan(),
        GuideXosApplicationCloseResult.Protected =>
            "This application is protected".AsSpan(),
        GuideXosApplicationCloseResult.Pending =>
            "Application close is in progress".AsSpan(),
        GuideXosApplicationCloseResult.CloseFailed =>
            "Application did not close".AsSpan(),
        GuideXosApplicationCloseResult.InvalidArgument =>
            "Application close request was invalid".AsSpan(),
        _ => "Application close failed".AsSpan(),
    };

    private static void AppendIdentityValue(Span<byte> destination,
        ref int position, ulong value)
    {
        Span<byte> digits = stackalloc byte[20];
        int count = 0;
        do
        {
            digits[count++] = (byte)('0' + value % 10u);
            value /= 10u;
        } while (value != 0u);
        while (count != 0 && position < destination.Length)
            destination[position++] = digits[--count];
    }

    private void RefreshAndRender(GuideXosHost host,
        GuideXosSurface surface, ReadOnlySpan<byte> inputSource)
    {
#if HOSTLOGPROOF_C161_TASK_MANAGER_PROOF
        Span<byte> clipboardBeforeRefresh =
            stackalloc byte[GuideXosClipboard.Capacity];
        GuideXosClipboard sharedClipboard = GuideXosClipboard.Shared;
        bool clipboardCaptured = sharedClipboard.TryCopyText(
            clipboardBeforeRefresh, out int clipboardLength);
#endif
        GuideXosApplicationSnapshotResult result =
            host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot snapshot);
        bool applied = _controller.ApplySnapshot(result, in snapshot);
#if HOSTLOGPROOF_C161_TASK_MANAGER_PROOF
        bool clipboardPreserved = clipboardCaptured &&
            sharedClipboard.Length == clipboardLength &&
            sharedClipboard.TextSpan.SequenceEqual(
                clipboardBeforeRefresh[..clipboardLength]);
        if (!clipboardPreserved)
        {
            host.TryLog("C161-TM-CLIPBOARD stage=refresh preserved=false result=FAIL"u8);
        }
#endif
        if (_controller.Render(surface) != GuideXosResult.Success)
        {
            host.TryLog("C161-TM-RENDER result=FAIL"u8);
            return;
        }
        LogRefresh(host, result, applied, inputSource);
        LogSelectedDetail(host);
    }

    private void LogLaunch(GuideXosHost host,
        GuideXosApplicationSnapshotResult result,
        in GuideXosApplicationSnapshot snapshot)
    {
        Span<byte> line = stackalloc byte[128];
        int position = 0;
        GuideXosText.Append(line, ref position,
            "C161-TM-LAUNCH id="u8);
        GuideXosText.AppendUnsigned(line, ref position, ApplicationSelector);
        GuideXosText.Append(line, ref position, " reg="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)GuideXosApplicationRegistry.RegistrationCount);
        GuideXosText.Append(line, ref position, " ctr="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_controller.ControlCount);
        GuideXosText.Append(line, ref position, " cap="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_controller.ControlMaximum);
        GuideXosText.Append(line, ref position, " max="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_controller.SharedControlMaximum);
        GuideXosText.Append(line, ref position, " snap="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)result);
        GuideXosText.Append(line, ref position, " n="u8);
        GuideXosText.AppendUnsigned(line, ref position, snapshot.Count);
        if (TryGetSelfRecord(in snapshot,
                out GuideXosApplicationSnapshotRecord self))
        {
            GuideXosText.Append(line, ref position, " self="u8);
            AppendUnsigned64(line, ref position, self.instanceId);
            GuideXosText.Append(line, ref position, " active="u8);
            GuideXosText.Append(line, ref position, self.IsActive
                ? "1"u8 : "0"u8);
        }
        else
        {
            GuideXosText.Append(line, ref position, " self=missing"u8);
        }
        GuideXosText.Append(line, ref position, " result=PASS"u8);
        host.TryLog(line[..position]);
    }

    private void LogRefresh(GuideXosHost host,
        GuideXosApplicationSnapshotResult result, bool applied,
        ReadOnlySpan<byte> inputSource)
    {
        Span<byte> line = stackalloc byte[128];
        int position = 0;
        GuideXosText.Append(line, ref position, "C161-TM-R s="u8);
        GuideXosText.Append(line, ref position, inputSource);
        GuideXosText.Append(line, ref position, " st="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)result);
        GuideXosText.Append(line, ref position, " n="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_controller.RowCount);
        GuideXosText.Append(line, ref position, " sel="u8);
        if (_controller.HasSelection)
        {
            GuideXosText.AppendUnsigned(line, ref position,
                (uint)_controller.SelectedIdentity.Source);
            GuideXosText.Append(line, ref position, ":"u8);
            AppendUnsigned64(line, ref position,
                _controller.SelectedIdentity.Value);
        }
        else
        {
            GuideXosText.Append(line, ref position, "none"u8);
        }
        GuideXosText.Append(line, ref position, " v="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_controller.FirstVisibleIndex);
        GuideXosText.Append(line, ref position, " ap="u8);
        GuideXosText.Append(line, ref position, applied
            ? "true"u8 : "false"u8);
        GuideXosText.Append(line, ref position, " self="u8);
        GuideXosApplicationSnapshot current = _controller.Snapshot;
        if (_controller.HasSnapshot && TryGetSelfRecord(in current,
                out GuideXosApplicationSnapshotRecord self))
        {
            AppendUnsigned64(line, ref position, self.instanceId);
            GuideXosText.Append(line, ref position, " active="u8);
            GuideXosText.Append(line, ref position,
                self.IsActive ? "1"u8 : "0"u8);
        }
        else
        {
            GuideXosText.Append(line, ref position, "none active=none"u8);
        }
        GuideXosText.Append(line, ref position, " result=PASS"u8);
        host.TryLog(line[..position]);
    }

    private void LogSelection(GuideXosHost host,
        ReadOnlySpan<byte> source)
    {
        if (!_controller.TryGetSelectedRecord(
                out GuideXosApplicationSnapshotRecord record, out int index))
            return;
        Span<byte> line = stackalloc byte[112];
        int position = 0;
        GuideXosText.Append(line, ref position, "C161-TM-SELECT source="u8);
        GuideXosText.Append(line, ref position, source);
        GuideXosText.Append(line, ref position, " row="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)index);
        GuideXosText.Append(line, ref position, " identity="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)record.source);
        GuideXosText.Append(line, ref position, ":"u8);
        AppendUnsigned64(line, ref position, record.instanceId);
        GuideXosText.Append(line, ref position, " on="u8);
        GuideXosText.Append(line, ref position, record.IsActive
            ? "1"u8 : "0"u8);
        GuideXosText.Append(line, ref position, " result=PASS"u8);
        host.TryLog(line[..position]);
    }

    private void LogFocus(GuideXosHost host, bool shift)
    {
        Span<byte> line = stackalloc byte[80];
        int position = 0;
        GuideXosText.Append(line, ref position, "C161-TM-FOCUS control="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_controller.Controls.ActiveControlId);
        GuideXosText.Append(line, ref position, " shift="u8);
        GuideXosText.Append(line, ref position, shift
            ? "true"u8 : "false"u8);
        GuideXosText.Append(line, ref position, " result=PASS"u8);
        host.TryLog(line[..position]);
    }

    private void LogSelectedDetail(GuideXosHost host)
    {
        Span<byte> line = stackalloc byte[128];
        int position = 0;
        GuideXosText.Append(line, ref position, "C161-TM-DETAIL "u8);
        if (!_controller.TryGetSelectedRecord(
                out GuideXosApplicationSnapshotRecord record, out _))
        {
            GuideXosText.Append(line, ref position,
                "selection=none result=PASS"u8);
            host.TryLog(line[..position]);
            LogCloseApplicationEligibility(host);
            return;
        }

        GuideXosText.Append(line, ref position, "source="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)record.source);
        GuideXosText.Append(line, ref position, " identity="u8);
        AppendHex64(line, ref position, record.instanceId);
        GuideXosText.Append(line, ref position, " name="u8);
        AppendRecordText(ref record, true, line, ref position);
        GuideXosText.Append(line, ref position, " state="u8);
        GuideXosText.Append(line, ref position, StateName(record.state));
        GuideXosText.Append(line, ref position, " active="u8);
        GuideXosText.Append(line, ref position,
            record.IsActive ? "Yes"u8 : "No"u8);
        GuideXosText.Append(line, ref position, " result=PASS"u8);
        host.TryLog(line[..position]);

        Span<byte> appIdLine = stackalloc byte[127];
        int appIdPosition = 0;
        GuideXosText.Append(appIdLine, ref appIdPosition, "C161-TM-ID "u8);
        GuideXosText.AppendUnsigned(appIdLine, ref appIdPosition,
            (uint)record.source);
        GuideXosText.Append(appIdLine, ref appIdPosition, ":"u8);
        AppendHex64(appIdLine, ref appIdPosition, record.instanceId);
        GuideXosText.Append(appIdLine, ref appIdPosition, "="u8);
        if (record.applicationIdLength == 0u)
            GuideXosText.Append(appIdLine, ref appIdPosition, "none"u8);
        else
            AppendRecordText(ref record, false, appIdLine,
                ref appIdPosition);
        host.TryLog(appIdLine[..appIdPosition]);
        LogCloseApplicationEligibility(host);
    }

    private void LogCloseApplicationEligibility(GuideXosHost host)
    {
        Span<byte> line = stackalloc byte[88];
        int position = 0;
        GuideXosText.Append(line, ref position,
            "C162-TM-CLOSE-ELIGIBILITY identity="u8);
        if (_controller.TryGetSelectedRecord(
                out GuideXosApplicationSnapshotRecord record, out _))
        {
            GuideXosText.AppendUnsigned(line, ref position,
                (uint)record.source);
            GuideXosText.Append(line, ref position, ":"u8);
            AppendIdentityValue(line, ref position, record.instanceId);
            GuideXosText.Append(line, ref position, " enabled="u8);
            GuideXosText.Append(line, ref position,
                _controller.CloseApplicationButton.EffectiveEnabled
                    ? "true"u8 : "false"u8);
        }
        else
        {
            GuideXosText.Append(line, ref position,
                "none enabled=false"u8);
        }
        GuideXosText.Append(line, ref position, " result=PASS"u8);
        host.TryLog(line[..position]);
    }

    private static void AppendRecordText(
        ref GuideXosApplicationSnapshotRecord record, bool displayName,
        Span<byte> destination, ref int position)
    {
        uint length = displayName
            ? record.displayNameLength : record.applicationIdLength;
        if (displayName)
        {
            fixed (byte* value = record.displayName)
            {
                for (uint index = 0; index < length &&
                     position < destination.Length; index++)
                    destination[position++] = value[index];
            }
        }
        else
        {
            fixed (byte* value = record.applicationId)
            {
                for (uint index = 0; index < length &&
                     position < destination.Length; index++)
                    destination[position++] = value[index];
            }
        }
    }

    private static void AppendHex64(Span<byte> destination,
        ref int position, ulong value)
    {
        const string digits = "0123456789ABCDEF";
        for (int shift = 60; shift >= 0 && position < destination.Length;
             shift -= 4)
            destination[position++] = (byte)digits[(int)((value >> shift) & 0xFu)];
    }

    private static ReadOnlySpan<byte> StateName(
        GuideXosApplicationSnapshotState state) => state switch
    {
        GuideXosApplicationSnapshotState.NotLoaded => "NotLoaded"u8,
        GuideXosApplicationSnapshotState.Running => "Running"u8,
        GuideXosApplicationSnapshotState.Suspended => "Suspended"u8,
        GuideXosApplicationSnapshotState.Terminated => "Terminated"u8,
        _ => "Unknown"u8,
    };

    private static bool TryGetSelfRecord(
        in GuideXosApplicationSnapshot snapshot,
        out GuideXosApplicationSnapshotRecord record)
    {
        for (uint index = 0; index < snapshot.Count; ++index)
        {
            if (snapshot.TryGetRecord(index, out record) &&
                record.source ==
                    GuideXosApplicationSnapshotSource.ManagedLogicalApplication &&
                HasTaskManagerApplicationId(ref record))
            {
                return true;
            }
        }
        record = default;
        return false;
    }

    private static bool HasTaskManagerApplicationId(
        ref GuideXosApplicationSnapshotRecord record)
    {
        ReadOnlySpan<byte> expected =
            "com.guidexos.apps.managed.taskmanager"u8;
        if (record.applicationIdLength != (uint)expected.Length) return false;
        fixed (byte* value = record.applicationId)
        {
            for (int index = 0; index < expected.Length; index++)
            {
                if (value[index] != expected[index]) return false;
            }
        }
        return true;
    }

    private static void AppendUnsigned64(Span<byte> destination,
        ref int position, ulong value)
    {
        Span<byte> digits = stackalloc byte[20];
        int count = 0;
        do
        {
            digits[count++] = (byte)('0' + value % 10u);
            value /= 10u;
        } while (value != 0u);
        while (count != 0 && position < destination.Length)
            destination[position++] = digits[--count];
    }
}
#endif
