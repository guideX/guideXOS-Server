namespace HostLogProof;

public static class GuideXosShiftTabTransportTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        bool sequence = TransportSequence();
        bool skipped = DisabledAndHiddenControlsAreSkipped();
        bool modal = ModalRoutingIsStable();
        bool staleSpace = StaleSplitGestureIsConsumed();
        bool repeated = RepeatedTraversalIsDeterministic();
        bool result = sequence && skipped && modal && staleSpace && repeated;
        bool counted = s_caseCount == 31;
        host.TryLog(result && counted
            ? "C129-SHIFT-TAB-TESTS cases=31 result=PASS"u8
            : "C129-SHIFT-TAB-TESTS result=FAIL"u8);
        return result && counted;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static bool TransportSequence()
    {
        FourControlFixture fixture = new();
        GuideXosControlHost host = fixture.Host;
        bool initial = Check(host.ActiveIndex == -1 && host.ActiveControlId == 0);
        GuideXosControlHostResult reverseResult =
            host.HandleKey(GuideXosTextInputKey.Tab, true);
        bool reverse = Check(reverseResult == GuideXosControlHostResult.Traversed) &&
            Check(host.ActiveControlId == 40 && fixture.Document.IsFocused &&
                !fixture.Open.IsFocused);
        GuideXosControlHostResult forwardResult =
            host.HandleKey(GuideXosTextInputKey.Tab);
        bool forward = Check(forwardResult == GuideXosControlHostResult.Traversed) &&
            Check(host.ActiveControlId == 10 && fixture.Open.IsFocused);
        bool noTabCharacter = Check(
            host.HandleCharacter('\t') == GuideXosControlHostResult.Ignored &&
            host.ActiveControlId == 10);
        bool noPhantomActivation = Check(
            host.HandleCharacter('a') == GuideXosControlHostResult.Ignored &&
            host.ActiveControlId == 10 && fixture.Open.IsFocused);
        bool followingReverse = Check(
            host.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 40);
        bool followingForward = Check(
            host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 10);
        return initial && reverse && forward && noTabCharacter &&
            noPhantomActivation && followingReverse && followingForward;
    }

    private static bool DisabledAndHiddenControlsAreSkipped()
    {
        FourControlFixture fixture = new();
        GuideXosControlHost host = fixture.Host;
        fixture.Save.SetEnabled(false);
        fixture.SaveAs.SetVisible(false);
        bool focus = Check(host.TryFocus(10) == GuideXosControlHostResult.Focused);
        bool forward = Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 40);
        bool reverse = Check(host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 10);
        bool repeat = Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 40);
        return focus && forward && reverse && repeat;
    }

    private static bool ModalRoutingIsStable()
    {
        GuideXosControlHost main = new(4);
        GuideXosButton open = new(10, 20, 80, 24, "Open");
        main.TryRegisterButton(10, open);
        GuideXosControlHost modal = new(2);
        GuideXosButton picker = new(10, 20, 80, 24, "Picker");
        modal.TryRegisterButton(90, picker);
        bool focus = Check(main.TryFocus(10) == GuideXosControlHostResult.Focused);
        bool entered = Check(main.EnterModal(modal) && main.IsModalActive);
        bool forward = Check(modal.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && modal.ActiveControlId == 90);
        bool reverse = Check(modal.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && modal.ActiveControlId == 90);
        bool isolated = Check(main.ActiveIndex == -1 && !open.IsFocused);
        bool restored = Check(main.ExitModal() && main.ActiveControlId == 10 &&
            open.IsFocused);
        return focus && entered && forward && reverse && isolated && restored;
    }

    private static bool StaleSplitGestureIsConsumed()
    {
        GuideXosControlHost host = new(2);
        GuideXosButton first = new(10, 20, 80, 24, "First");
        GuideXosButton fallback = new(100, 20, 80, 24, "Fallback");
        host.TryRegisterButton(1, first);
        host.TryRegisterButton(2, fallback);
        bool focus = Check(host.TryFocus(1) == GuideXosControlHostResult.Focused);
        bool pending = Check(host.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored);
        first.SetVisible(false);
        bool stale = Check(host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && host.ActiveControlId != 1);
        bool recovered = Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 2);
        return focus && pending && stale && recovered;
    }

    private static bool RepeatedTraversalIsDeterministic()
    {
        GuideXosControlHost host = new(2);
        GuideXosButton first = new(10, 20, 80, 24, "First");
        GuideXosButton second = new(100, 20, 80, 24, "Second");
        host.TryRegisterButton(1, first);
        host.TryRegisterButton(2, second);
        bool result = true;
        for (int index = 0; index < 4; index++)
        {
            result &= Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed &&
                host.ActiveControlId == (index % 2 == 0 ? 1 : 2));
        }
        for (int index = 0; index < 4; index++)
        {
            result &= Check(host.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed &&
                host.ActiveControlId == (index % 2 == 0 ? 1 : 2));
        }
        return result;
    }

    private sealed class FourControlFixture
    {
        public readonly GuideXosControlHost Host = new(4);
        public readonly GuideXosButton Open =
            new(10, 20, 80, 24, "Open");
        public readonly GuideXosButton Save =
            new(100, 20, 80, 24, "Save");
        public readonly GuideXosButton SaveAs =
            new(190, 20, 88, 24, "Save As");
        public readonly GuideXosTextArea Document =
            new(64, 8, 3, 16);

        public FourControlFixture()
        {
            Host.TryRegisterButton(10, Open);
            Host.TryRegisterButton(20, Save);
            Host.TryRegisterButton(30, SaveAs);
            Host.TryRegisterTextArea(40, Document);
        }
    }
}
