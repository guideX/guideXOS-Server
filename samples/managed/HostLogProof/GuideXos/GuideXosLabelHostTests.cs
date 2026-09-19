namespace HostLogProof;

/// <summary>
/// Host/application probes proving that labels remain outside the interactive
/// registration, routing, and focus model.
/// </summary>
public static class GuideXosLabelHostTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        s_caseCount = 0;
        GuideXosLabel label = new(20, 66, 160, "Path: /system/apps/NOTES.TXT");
        GuideXosButton open = new(20, 220, 90, 28, "Open");
        GuideXosButton save = new(120, 220, 90, 28, "Save");
        GuideXosButton saveAs = new(220, 220, 100, 28, "Save As");
        GuideXosCheckBox checkBox = new(330, 220, 128, 28, "Show path", true);
        GuideXosTextArea document = new(256, 72, 4, 48);
        document.SetText("doc");
        GuideXosControlHost controlHost = new(5);
        bool registered = Check(controlHost.TryRegisterButton(1, open) ==
                GuideXosControlHostResult.Registered) &&
            Check(controlHost.TryRegisterButton(2, save) ==
                GuideXosControlHostResult.Registered) &&
            Check(controlHost.TryRegisterButton(3, saveAs) ==
                GuideXosControlHostResult.Registered) &&
            Check(controlHost.TryRegisterCheckBox(4, checkBox) ==
                GuideXosControlHostResult.Registered) &&
            Check(controlHost.TryRegisterTextArea(5, document) ==
                GuideXosControlHostResult.Registered);
        bool noFocus = Check(registered && controlHost.RegistrationCount == 5 &&
            controlHost.ActiveControlId == 0);
        bool traversal = Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 1) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 2) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 3) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 4) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 5);
        bool forwardWrap = Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 1);
        bool reverse = Check(controlHost.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 5);
        bool labelUpdate = Check(label.SetText("Path: /system/apps/SECOND.TXT") &&
            controlHost.ActiveControlId == 5 && label.Text.EndsWith("SECOND.TXT"));
        label.SetVisible(false);
        bool hiddenFocus = Check(!label.Visible && controlHost.ActiveControlId == 5);
        label.SetVisible(true);
        bool shownFocus = Check(label.Visible && controlHost.ActiveControlId == 5);
        bool labelFocusRejected = Check(controlHost.TryFocus(99) ==
            GuideXosControlHostResult.Rejected && controlHost.ActiveControlId == 5);
        bool labelPointerRejected = Check(controlHost.FocusAndRoutePointer(99, 24, 72) ==
            GuideXosControlHostResult.Rejected && controlHost.ActiveControlId == 5);
        controlHost.TryFocus(1);
        bool enterIsolation = Check(controlHost.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosControlHostResult.Activated && label.Text.EndsWith("SECOND.TXT"));
        bool spaceIsolation = Check(controlHost.HandleCharacter(' ') ==
            GuideXosControlHostResult.Activated && label.Text.EndsWith("SECOND.TXT"));
        bool geometryIsolation = Check(label.TrySetBounds(28, 66, 192) &&
            controlHost.ActiveControlId == 1 && open.IsFocused);
        bool renderIsolation = Check(surface == null ||
            label.Render(surface) == GuideXosResult.Success);
        bool result = registered && noFocus && traversal && forwardWrap && reverse &&
            labelUpdate && hiddenFocus && shownFocus && labelFocusRejected &&
            labelPointerRejected && enterIsolation && spaceIsolation &&
            geometryIsolation && renderIsolation && s_caseCount == 22;
        if (host != null)
        {
            host.TryLog(result
                ? "C122-LABEL-HOST-TESTS cases=22 result=PASS"u8
                : "C122-LABEL-HOST-TESTS cases=22 result=FAIL"u8);
        }
        return result;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }
}
