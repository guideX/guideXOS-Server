#if HOSTLOGPROOF_C117_MANAGED_TEXT_AREA
using System;
using System.Text;
using HostLogProof;

namespace HostLogProof.Applications;

/// <summary>
/// Notes-owned editor and footer layout derived from a C174 validated client
/// area. Text remains non-wrapping; wider geometry only raises the visible
/// clipping limit for the existing text-area renderer.
/// </summary>
internal readonly struct GuideXosManagedNotesLayoutC175
{
    public const int MinimumOuterWidth = 560;
    public const int MinimumOuterHeight = 344;
    public const int MaximumOuterWidth = 960;
    public const int MaximumOuterHeight = 436;
    public const int InitialOuterWidth = 600;
    public const int InitialOuterHeight = 360;
    public const int PathLabelY = 66;
    public const int EditorX = 20;
    // Keep the existing optional path label (y=66) on its own text row.
    public const int EditorY = 84;
    public const int InitialEditorColumns = 32;
    public const int InitialEditorLines = 4;
    public const int EditorCharacterWidth = 8;
    public const int EditorLineHeight = 18;

    private GuideXosManagedNotesLayoutC175(
        in GuideXosManagedWindowResizeCandidateC174 geometry,
        int editorColumns, int editorLines)
    {
        OuterWidth = geometry.OuterWidth;
        OuterHeight = geometry.OuterHeight;
        ClientWidth = geometry.ClientWidth;
        ClientHeight = geometry.ClientHeight;
        EditorColumns = editorColumns;
        EditorLines = editorLines;
        StatusY = EditorY + editorLines * EditorLineHeight + 6;
        FooterOffsetY = (editorLines - InitialEditorLines) * EditorLineHeight;
        int pathDisplayTop = PathDisplayY - 14;
        int pathDisplayAvailable = ClientHeight - pathDisplayTop;
        PathDisplayHeight = Math.Min(90,
            pathDisplayAvailable / EditorLineHeight * EditorLineHeight);
        EditorScrollBarX = EditorX + editorColumns * EditorCharacterWidth + 2;
        ListX = EditorX + editorColumns * EditorCharacterWidth + 24;
        ListScrollBarX = ListX + 200;
    }

    public int OuterWidth { get; }
    public int OuterHeight { get; }
    public int ClientWidth { get; }
    public int ClientHeight { get; }
    public int EditorColumns { get; }
    public int EditorLines { get; }
    public int FooterOffsetY { get; }
    public int PathDisplayHeight { get; }
    public int EditorScrollBarX { get; }
    public int ListX { get; }
    public int ListScrollBarX { get; }
    public int StatusY { get; }
    public int EditorInfoY => StatusY + 24;
    public int KeyboardTipsY => StatusY + 46;
    public int SeparatorY => StatusY + 50;
    public int MainButtonsY => StatusY + 70;
    public int OptionsY => StatusY + 100;
    public int SettingsY => StatusY + 130;
    public int PathDisplayY => StatusY + 114;
    public int ShowStatusY => StatusY + 34;
    public int RadioButtonsY => StatusY + 106;

    public static bool TryCreate(
        in GuideXosManagedWindowResizeCandidateC174 geometry,
        out GuideXosManagedNotesLayoutC175 layout)
    {
        layout = default;
        if (geometry.OuterWidth < MinimumOuterWidth ||
            geometry.OuterWidth > MaximumOuterWidth ||
            geometry.OuterHeight < MinimumOuterHeight ||
            geometry.OuterHeight > MaximumOuterHeight ||
            geometry.ClientWidth != geometry.OuterWidth ||
            geometry.ClientHeight != geometry.OuterHeight -
                GuideXosManagedWindowResizeBoundsC174.NativeTitlebarHeight)
        {
            return false;
        }

        int editorColumns = InitialEditorColumns +
            (geometry.ClientWidth - InitialOuterWidth) /
                EditorCharacterWidth;
        editorColumns = Math.Clamp(editorColumns, 24,
            GuideXosTextArea.MaximumSupportedRenderableColumns);

        int extraLines = Math.Max(0,
            (geometry.ClientHeight -
                (InitialOuterHeight -
                    GuideXosManagedWindowResizeBoundsC174.NativeTitlebarHeight)) /
                EditorLineHeight);
        int editorLines = Math.Clamp(InitialEditorLines + extraLines,
            InitialEditorLines, GuideXosTextArea.MaximumVisibleLineCount);

        layout = new GuideXosManagedNotesLayoutC175(in geometry,
            editorColumns, editorLines);
        int bottomControls = layout.SettingsY + 28;
        int listRight = layout.ListScrollBarX + 16;
        return editorColumns >= 24 && editorLines >= InitialEditorLines &&
            EditorY >= PathLabelY + EditorLineHeight &&
            layout.PathDisplayHeight >= 54 &&
            layout.StatusY > EditorY + editorLines * EditorLineHeight &&
            bottomControls <= geometry.ClientHeight &&
            layout.EditorScrollBarX + 16 <= geometry.ClientWidth &&
            listRight <= geometry.ClientWidth;
    }
}
public sealed partial class ManagedNotes
{
    internal static readonly GuideXosManagedWindowResizeBoundsC174 ResizeBoundsC175 =
        new(GuideXosManagedNotesLayoutC175.MinimumOuterWidth,
            GuideXosManagedNotesLayoutC175.MinimumOuterHeight,
            GuideXosManagedNotesLayoutC175.MaximumOuterWidth,
            GuideXosManagedNotesLayoutC175.MaximumOuterHeight);

    private GuideXosManagedNotesLayoutC175 _c175Layout;
    private int _c175WindowWidth;
    private int _c175WindowHeight;

#if HOSTLOGPROOF_C175_MANAGED_NOTES_RESIZE
    private static bool s_c175ResizeTestsRun;
#endif
#if HOSTLOGPROOF_C176_MANAGED_NOTES_NAVIGATION
    private static bool s_c176NavigationTestsRun;
#endif
#if HOSTLOGPROOF_C177_MANAGED_NOTES_NEWLINE_SAVE
    private static bool s_c177NewlineSaveTestsRun;
#endif

    internal int C175WindowWidth => _c175WindowWidth;
    internal int C175WindowHeight => _c175WindowHeight;
    internal int C175EditorColumns => _c175Layout.EditorColumns;
    internal int C175EditorLines => _c175Layout.EditorLines;

    private bool TryInitializeC175Layout()
    {
        if (!ResizeBoundsC175.TryCreateCandidate(
                GuideXosManagedNotesLayoutC175.InitialOuterWidth,
                GuideXosManagedNotesLayoutC175.InitialOuterHeight,
                out GuideXosManagedWindowResizeCandidateC174 candidate) ||
            !GuideXosManagedNotesLayoutC175.TryCreate(in candidate,
                out GuideXosManagedNotesLayoutC175 layout) ||
            !ApplyC175Layout(in layout))
        {
            return false;
        }
        return true;
    }

    private bool TryCalculateC175Layout(
        in GuideXosManagedWindowResizeCandidateC174 geometry,
        out GuideXosManagedNotesLayoutC175 layout)
    {
        return GuideXosManagedNotesLayoutC175.TryCreate(in geometry,
            out layout);
    }

    private bool ApplyC175Layout(
        in GuideXosManagedNotesLayoutC175 layout)
    {
        if (!_textArea.TrySetViewportDimensions(layout.EditorColumns,
                layout.EditorLines))
        {
            return false;
        }

        int footer = layout.FooterOffsetY;
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
        if (!_openButton.TrySetBounds(20, 220 + footer, 90, 28) ||
            !_saveButton.TrySetBounds(120, 220 + footer, 90, 28) ||
            !_saveAsButton.TrySetBounds(220, 220 + footer, 100, 28))
        {
            return false;
        }
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
        if (!_showPathCheckBox.TrySetBounds(330, 220 + footer, 128, 28))
            return false;
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
        if (!_showStatusCheckBox.TrySetBounds(330, layout.ShowStatusY, 128, 28))
            return false;
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
        if (!_fullPathRadio.TrySetBounds(20, layout.RadioButtonsY, 128, 28) ||
            !_fileNameRadio.TrySetBounds(160, layout.RadioButtonsY, 128, 28))
        {
            return false;
        }
#endif
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
        if (!_documentUsage.TrySetBounds(160, layout.EditorInfoY, 312))
        {
            return false;
        }
#endif
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
        if (!_separator.TrySetBounds(20, layout.SeparatorY, 480))
            return false;
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
#if HOSTLOGPROOF_C127_MANAGED_PANEL
        if (!_pathDisplayPanel.ContainsChild(_pathDisplayCombo) &&
            !_pathDisplayCombo.TrySetBounds(20, layout.PathDisplayY,
                268, 28))
        {
            return false;
        }
#else
        if (!_pathDisplayCombo.TrySetBounds(20, layout.PathDisplayY,
                268, 28))
        {
            return false;
        }
#endif
#endif
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
        if (!_pathDisplayBox.TrySetBounds(12, layout.PathDisplayY - 14,
                456, layout.PathDisplayHeight))
        {
            return false;
        }
#endif
#if HOSTLOGPROOF_C127_MANAGED_PANEL
        if (!_pathDisplayPanel.TrySetBounds(12,
                layout.PathDisplayY - 14, 456, layout.PathDisplayHeight))
        {
            return false;
        }
#endif
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
        if (!_c138TextScrollBar.TrySetBounds(layout.EditorScrollBarX,
                GuideXosManagedNotesLayoutC175.EditorY, 16,
                layout.EditorLines * GuideXosManagedNotesLayoutC175.EditorLineHeight) ||
            !_c138ListScrollBar.TrySetBounds(layout.ListScrollBarX, 72,
                16, 72))
        {
            return false;
        }
        if (_c138BindingsInstalled) SyncC138ScrollBars();
#endif

        _c175Layout = layout;
        _c175WindowWidth = layout.OuterWidth;
        _c175WindowHeight = layout.OuterHeight;
        return true;
    }

    private GuideXosResult HandleC175LiveGeometry(GuideXosHost host,
        int outerWidth, int outerHeight)
    {
        if (host == null || _window == 0u ||
            host.TryGetSurface(_window, out GuideXosSurface surface) !=
                GuideXosResult.Success || surface == null)
        {
            return GuideXosResult.SurfaceCreationFailed;
        }

        GuideXosManagedWindowResizeTransactionC174 transaction = new(
            in ResizeBoundsC175, _c175WindowWidth, _c175WindowHeight,
            outerWidth, outerHeight);
        if (!transaction.IsValid)
        {
#if GUIDEXOS_PROOF_CONTROL
            host.TryLog("C175-MANAGED-GEOMETRY result=REJECT layout=unchanged"u8);
#endif
            return transaction.Result;
        }

        GuideXosManagedNotesLayoutC175 priorLayout = _c175Layout;
#if GUIDEXOS_PROOF_CONTROL
        Span<byte> pathBytes = stackalloc byte[512];
        int pathByteCount = Encoding.UTF8.GetByteCount(_currentPath.AsSpan());
        if (pathByteCount > pathBytes.Length)
            return GuideXosResult.InvalidArgument;
        pathBytes = pathBytes[..Encoding.UTF8.GetBytes(
            _currentPath.AsSpan(), pathBytes)];
#endif
        int priorOffset = _textArea.FirstVisibleLine;
        int priorCaret = _textArea.CaretIndex;
        int priorAnchor = _textArea.AnchorIndex;
        int priorPreferredColumn = _textArea.PreferredColumn;
        bool priorTextFocus = _textArea.IsFocused;
        int priorHistoryCount = _textArea.HistoryCount;
        int priorHistoryIndex = _textArea.HistoryIndex;
        GuideXosTextRevision priorRevision = _textArea.CurrentRevision;
        bool priorCanUndo = _textArea.CanUndo;
        bool priorCanRedo = _textArea.CanRedo;
        string priorPath = _currentPath;
#if HOSTLOGPROOF_C152_MANAGED_NOTES_SAVE_WORKFLOW
        bool priorDirty = _c152Enabled && _c152DocumentState.Dirty;
#else
        bool priorDirty = false;
#endif
        int priorControlFocus = _mainControlHost?.ActiveControlId ?? -1;
        ulong priorWindow = _window;
        uint priorLaunchCount = _launchCount;
        Span<byte> priorDocument = stackalloc byte[_textArea.MaximumCharacters];
        if (!_textArea.TryCopyUtf8To(priorDocument, out int priorDocumentLength))
            return GuideXosResult.InvalidArgument;

        GuideXosManagedWindowResizeCandidateC174 candidate =
            transaction.Candidate;
        bool layoutAccepted = TryCalculateC175Layout(in candidate,
            out GuideXosManagedNotesLayoutC175 nextLayout) &&
            ApplyC175Layout(in nextLayout);
        if (!layoutAccepted)
        {
            _ = transaction.Complete(layoutAccepted: false,
                renderResult: GuideXosResult.Success, stateAccepted: false);
            RollbackC175Geometry(host, surface, in transaction, in priorLayout,
                priorOffset);
#if GUIDEXOS_PROOF_CONTROL
            host.TryLog("C175-MANAGED-GEOMETRY result=REJECT stage=layout rollback=attempted"u8);
#endif
            return transaction.Result;
        }

        GuideXosResult renderResult = RenderMain(host, surface,
            _launchCount) ? GuideXosResult.Success :
                GuideXosResult.InvalidArgument;
        Span<byte> currentDocument = stackalloc byte[_textArea.MaximumCharacters];
        bool contentPreserved = _textArea.TryCopyUtf8To(currentDocument,
                out int currentDocumentLength) &&
            priorDocumentLength == currentDocumentLength &&
            priorDocument[..priorDocumentLength].SequenceEqual(
                currentDocument[..currentDocumentLength]);
        bool positionPreserved = _textArea.CaretIndex == priorCaret &&
            _textArea.AnchorIndex == priorAnchor &&
            _textArea.PreferredColumn == priorPreferredColumn;
        bool focusPreserved = _textArea.IsFocused == priorTextFocus;
        bool historyPreserved = _textArea.HistoryCount == priorHistoryCount &&
            _textArea.HistoryIndex == priorHistoryIndex &&
            _textArea.CurrentRevision.Equals(priorRevision) &&
            _textArea.CanUndo == priorCanUndo &&
            _textArea.CanRedo == priorCanRedo;
        bool pathPreserved = string.Equals(_currentPath, priorPath,
            StringComparison.Ordinal);
        bool dirtyPreserved = true;
#if HOSTLOGPROOF_C152_MANAGED_NOTES_SAVE_WORKFLOW
        dirtyPreserved = !_c152Enabled ||
            _c152DocumentState.Dirty == priorDirty;
#endif
        bool controlsPreserved =
            (_mainControlHost?.ActiveControlId ?? -1) == priorControlFocus;
        bool identityPreserved = _window == priorWindow &&
            _launchCount == priorLaunchCount;
        bool viewportValid = _textArea.FirstVisibleLine >= 0 &&
            _textArea.FirstVisibleLine <= _textArea.MaximumFirstVisibleLine &&
            _textArea.CaretLine >= _textArea.FirstVisibleLine &&
            _textArea.CaretLine < _textArea.FirstVisibleLine +
                _textArea.VisibleLineCount;
        bool textStatePreserved = contentPreserved && positionPreserved &&
            focusPreserved && historyPreserved && pathPreserved &&
            dirtyPreserved && controlsPreserved && identityPreserved &&
            viewportValid;
        bool accepted = transaction.Complete(layoutAccepted, renderResult,
            textStatePreserved);
        if (!accepted)
        {
            RollbackC175Geometry(host, surface, in transaction, in priorLayout,
                priorOffset);
#if GUIDEXOS_PROOF_CONTROL
            host.TryLog("C175-MANAGED-GEOMETRY result=REJECT stage=render-or-state rollback=attempted"u8);
            Span<byte> rejection = stackalloc byte[120];
            int rejectedPosition = 0;
            GuideXosText.Append(rejection, ref rejectedPosition,
                "C175-MANAGED-REJECT render="u8);
            GuideXosText.AppendUnsigned(rejection, ref rejectedPosition,
                renderResult == GuideXosResult.Success ? 1u : 0u);
            GuideXosText.Append(rejection, ref rejectedPosition, " doc="u8);
            GuideXosText.AppendUnsigned(rejection, ref rejectedPosition,
                contentPreserved ? 1u : 0u);
            GuideXosText.Append(rejection, ref rejectedPosition, " pos="u8);
            GuideXosText.AppendUnsigned(rejection, ref rejectedPosition,
                positionPreserved ? 1u : 0u);
            GuideXosText.Append(rejection, ref rejectedPosition, " focus="u8);
            GuideXosText.AppendUnsigned(rejection, ref rejectedPosition,
                focusPreserved ? 1u : 0u);
            GuideXosText.Append(rejection, ref rejectedPosition, " hist="u8);
            GuideXosText.AppendUnsigned(rejection, ref rejectedPosition,
                historyPreserved ? 1u : 0u);
            GuideXosText.Append(rejection, ref rejectedPosition, " path="u8);
            GuideXosText.AppendUnsigned(rejection, ref rejectedPosition,
                pathPreserved ? 1u : 0u);
            GuideXosText.Append(rejection, ref rejectedPosition, " dirty="u8);
            GuideXosText.AppendUnsigned(rejection, ref rejectedPosition,
                dirtyPreserved ? 1u : 0u);
            GuideXosText.Append(rejection, ref rejectedPosition, " controls="u8);
            GuideXosText.AppendUnsigned(rejection, ref rejectedPosition,
                controlsPreserved ? 1u : 0u);
            GuideXosText.Append(rejection, ref rejectedPosition, " identity="u8);
            GuideXosText.AppendUnsigned(rejection, ref rejectedPosition,
                identityPreserved ? 1u : 0u);
            GuideXosText.Append(rejection, ref rejectedPosition, " view="u8);
            GuideXosText.AppendUnsigned(rejection, ref rejectedPosition,
                viewportValid ? 1u : 0u);
            host.TryLog(rejection[..rejectedPosition]);
            uint renderProbe = ProbeC175RenderFailures(host, surface);
            Span<byte> probeLine = stackalloc byte[48];
            int probePosition = 0;
            GuideXosText.Append(probeLine, ref probePosition,
                "C175-RENDER-PROBE passBits="u8);
            GuideXosText.AppendUnsigned(probeLine, ref probePosition,
                renderProbe);
            host.TryLog(probeLine[..probePosition]);
#endif
            return transaction.Result;
        }

#if GUIDEXOS_PROOF_CONTROL
        Span<byte> line = stackalloc byte[120];
        int position = 0;
        GuideXosText.Append(line, ref position,
            "C175-MANAGED-GEOMETRY size="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)outerWidth);
        GuideXosText.Append(line, ref position, "x"u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)outerHeight);
        GuideXosText.Append(line, ref position,
            " client="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_c175Layout.ClientWidth);
        GuideXosText.Append(line, ref position, "x"u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_c175Layout.ClientHeight);
        GuideXosText.Append(line, ref position, " editor="u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_c175Layout.EditorColumns);
        GuideXosText.Append(line, ref position, "x"u8);
        GuideXosText.AppendUnsigned(line, ref position,
            (uint)_c175Layout.EditorLines);
        GuideXosText.Append(line, ref position, " result=PASS"u8);
        host.TryLog(line[..position]);

        Span<byte> stateLine = stackalloc byte[120];
        int statePosition = 0;
        GuideXosText.Append(stateLine, ref statePosition,
            "C175-MANAGED-STATE caret="u8);
        GuideXosText.AppendUnsigned(stateLine, ref statePosition,
            (uint)_textArea.CaretIndex);
        GuideXosText.Append(stateLine, ref statePosition, " anchor="u8);
        GuideXosText.AppendUnsigned(stateLine, ref statePosition,
            (uint)_textArea.AnchorIndex);
        GuideXosText.Append(stateLine, ref statePosition, " view="u8);
        GuideXosText.AppendUnsigned(stateLine, ref statePosition,
            (uint)_textArea.FirstVisibleLine);
        GuideXosText.Append(stateLine, ref statePosition, " h="u8);
        GuideXosText.AppendUnsigned(stateLine, ref statePosition,
            (uint)_textArea.HistoryCount);
        GuideXosText.Append(stateLine, ref statePosition, " i="u8);
        GuideXosText.AppendUnsigned(stateLine, ref statePosition,
            (uint)_textArea.HistoryIndex);
        GuideXosText.Append(stateLine, ref statePosition, " undo="u8);
        GuideXosText.AppendUnsigned(stateLine, ref statePosition,
            _textArea.CanUndo ? 1u : 0u);
        GuideXosText.Append(stateLine, ref statePosition, " redo="u8);
        GuideXosText.AppendUnsigned(stateLine, ref statePosition,
            _textArea.CanRedo ? 1u : 0u);
        GuideXosText.Append(stateLine, ref statePosition, " dirty="u8);
        GuideXosText.AppendUnsigned(stateLine, ref statePosition,
#if HOSTLOGPROOF_C152_MANAGED_NOTES_SAVE_WORKFLOW
            _c152Enabled && _c152DocumentState.Dirty ? 1u : 0u);
#else
            0u);
#endif
        GuideXosText.Append(stateLine, ref statePosition,
            " doc=1 focus=1 result=PASS"u8);
        host.TryLog(stateLine[..statePosition]);

        Span<byte> pathLine = stackalloc byte[120];
        int pathPosition = 0;
        GuideXosText.Append(pathLine, ref pathPosition,
            "C175-MANAGED-PATH path="u8);
        GuideXosText.Append(pathLine, ref pathPosition, pathBytes);
        host.TryLog(pathLine[..pathPosition]);
#endif
        return GuideXosResult.Success;
    }

    private void RollbackC175Geometry(GuideXosHost host,
        GuideXosSurface surface,
        in GuideXosManagedWindowResizeTransactionC174 transaction,
        in GuideXosManagedNotesLayoutC175 priorLayout, int priorOffset)
    {
        if (!transaction.RequiresRollback) return;
        if (ApplyC175Layout(in priorLayout))
            _textArea.SetFirstVisibleLine(priorOffset);
        _ = RenderMain(host, surface, _launchCount);
    }

#if GUIDEXOS_PROOF_CONTROL
    // Probe individual render components only after RenderMain rejects a C175
    // candidate. A set bit means that component can render at the candidate
    // geometry. Bit positions follow the order below.
    private uint ProbeC175RenderFailures(GuideXosHost host,
        GuideXosSurface surface)
    {
        uint bits = 0;
        int bit = 0;
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
        ProbeC175RenderBit(ref bits, bit++, SyncDocumentUsage());
#endif
        ProbeC175RenderBit(ref bits, bit++, surface.TryFillRect(10, 10,
            Math.Max(0, _c175Layout.ClientWidth - 20),
            Math.Max(0, _c175Layout.ClientHeight - 20), 0x007A5A9Au) ==
            GuideXosResult.Success);
        ProbeC175RenderBit(ref bits, bit++, GuideXosText.Line(surface, 24,
            "Managed Notes | "u8, "multiline text area"u8));
        ProbeC175RenderBit(ref bits, bit++, _textArea.Render(surface,
            GuideXosManagedNotesLayoutC175.EditorX,
            GuideXosManagedNotesLayoutC175.EditorY,
            GuideXosManagedNotesLayoutC175.EditorLineHeight) ==
            GuideXosResult.Success);
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
        ProbeC175RenderBit(ref bits, bit++, _c138TextScrollBar.Render(surface) ==
            GuideXosResult.Success);
        ProbeC175RenderBit(ref bits, bit++, _c138ListScrollBar.Render(surface) ==
            GuideXosResult.Success);
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
        ProbeC175RenderBit(ref bits, bit++, GuideXosText.Line(surface,
            _c175Layout.StatusY, "Status: "u8, ReadOnlySpan<byte>.Empty));
#endif
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
        ProbeC175RenderBit(ref bits, bit++, _documentUsage.Render(surface) ==
            GuideXosResult.Success);
#else
        ProbeC175RenderBit(ref bits, bit++, GuideXosText.Line(surface,
            _c175Layout.EditorInfoY, "Editor: "u8,
            "bounded ASCII; [] selection; | caret"u8));
#endif
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
        ProbeC175RenderBit(ref bits, bit++, _separator.Render(surface) ==
            GuideXosResult.Success);
#endif
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
        ProbeC175RenderBit(ref bits, bit++, _pathDisplayBox.Render(surface) ==
            GuideXosResult.Success);
#endif
#if HOSTLOGPROOF_C127_MANAGED_PANEL
        ProbeC175RenderBit(ref bits, bit++, _pathDisplayPanel.Render(surface) ==
            GuideXosResult.Success);
#endif
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
        ProbeC175RenderBit(ref bits, bit++, _openButton.Render(surface) ==
            GuideXosResult.Success);
        ProbeC175RenderBit(ref bits, bit++, _saveButton.Render(surface) ==
            GuideXosResult.Success);
        ProbeC175RenderBit(ref bits, bit++, _saveAsButton.Render(surface) ==
            GuideXosResult.Success);
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
        ProbeC175RenderBit(ref bits, bit++, _showPathCheckBox.Render(surface) ==
            GuideXosResult.Success);
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
        ProbeC175RenderBit(ref bits, bit++, _showStatusCheckBox.Render(surface) ==
            GuideXosResult.Success);
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
        ProbeC175RenderBit(ref bits, bit++, _fullPathRadio.Render(surface) ==
            GuideXosResult.Success);
        ProbeC175RenderBit(ref bits, bit++, _fileNameRadio.Render(surface) ==
            GuideXosResult.Success);
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        ProbeC175RenderBit(ref bits, bit++, _pathDisplayCombo.Render(surface) ==
            GuideXosResult.Success);
#endif
        ProbeC175RenderBit(ref bits, bit++, surface.TryAddButton(330,
            _c175Layout.MainButtonsY, 90, 28, "Reload"u8, 3u, out _) ==
            GuideXosResult.Success);
        ProbeC175RenderBit(ref bits, bit++, surface.TryAddButton(430,
            _c175Layout.SettingsY, 90, 28, "Settings"u8,
            C150SettingsActionId, out _) == GuideXosResult.Success);
        return bits;
    }

    private static void ProbeC175RenderBit(ref uint bits, int bit,
        bool succeeded)
    {
        if (succeeded) bits |= 1u << bit;
    }
#endif

#if HOSTLOGPROOF_C175_MANAGED_NOTES_RESIZE
    internal ulong C175WindowForTests => _window;

    internal bool BeginC175LifecycleForTests(ulong windowIdentity)
    {
        if (windowIdentity == 0u) return false;
        _window = windowIdentity;
        return TryInitializeC175Layout();
    }

    internal bool ResizeC175ForTests(int outerWidth, int outerHeight)
    {
        GuideXosManagedWindowResizeTransactionC174 transaction = new(
            in ResizeBoundsC175, _c175WindowWidth, _c175WindowHeight,
            outerWidth, outerHeight);
        if (!transaction.IsValid) return false;
        GuideXosManagedNotesLayoutC175 prior = _c175Layout;
        GuideXosManagedWindowResizeCandidateC174 candidate =
            transaction.Candidate;
        if (!TryCalculateC175Layout(in candidate,
                out GuideXosManagedNotesLayoutC175 next) ||
            !ApplyC175Layout(in next))
        {
            if (transaction.Complete(false, GuideXosResult.Success, false))
                return false;
            _ = ApplyC175Layout(in prior);
            return false;
        }
        return transaction.Complete(true, GuideXosResult.Success, true);
    }

    internal bool CloseC175LifecycleForTests()
    {
        if (_window == 0u) return false;
        _textArea.Blur();
        _window = 0u;
        _c175WindowWidth = 0;
        _c175WindowHeight = 0;
        return true;
    }

    internal GuideXosTextArea TextAreaForC175Tests => _textArea;
#endif
}
#endif
