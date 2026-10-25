using HostLogProof;

public static class NotesInputRoutingC188R8Regression
{
    public static bool Run()
    {
        bool keyboardControls = GuideXosShiftTabTransportTests.Run(null);
        bool pointerControls = GuideXosSecondaryPointerC136Tests.Run(null);
        bool scrollbarControls = GuideXosScrollBarC138Tests.Run(null);
        bool notesEditContract = NotesPointerAndCharacterContract();
        bool passed = keyboardControls && pointerControls &&
            scrollbarControls && notesEditContract;

        Console.WriteLine($"C188R8-INPUT-FOCUSED C129 cases=31 result={(keyboardControls ? "PASS" : "FAIL")}");
        Console.WriteLine($"C188R8-INPUT-FOCUSED C136 cases=36 result={(pointerControls ? "PASS" : "FAIL")}");
        Console.WriteLine($"C188R8-INPUT-FOCUSED C138 cases=62 result={(scrollbarControls ? "PASS" : "FAIL")}");
        Console.WriteLine($"C188R8-NOTES-EDIT focused-caret-char-event=7 result={(notesEditContract ? "PASS" : "FAIL")}");
        Console.WriteLine($"C188R8-INPUT-FOCUSED total=136 result={(passed ? "PASS" : "FAIL")}");
        return passed;
    }

    private static bool NotesPointerAndCharacterContract()
    {
        GuideXosTextArea area = new(256, 32, 4, 32);
        if (!area.SetUtf8("zero\none\ntwo"u8)) return false;

        bool unfocusedRejects = !area.IsFocused &&
            area.HandleCharacter('a') == GuideXosTextAreaEditResult.Ignored &&
            area.Text == "zero\none\ntwo";
        bool outsideDoesNotFocus = area.HandlePointerDown(
            39, 83, 20, 84, 8, 18) == GuideXosTextAreaEditResult.Ignored &&
            !area.IsFocused;

        // C175's client-local editor begins at (20, 84). The bounded guest
        // probe's (39, 115) point maps to row 1, column 2 in that editor.
        bool pointerFocus = area.HandlePointerDown(
            39, 115, 20, 84, 8, 18) == GuideXosTextAreaEditResult.Focused &&
            area.IsFocused && area.CaretLine == 1 && area.CaretIndex == 7;
        bool keydownDoesNotInsertCharacter =
            area.HandleKey((GuideXosTextInputKey)'a') ==
                GuideXosTextAreaEditResult.Rejected &&
            area.Text == "zero\none\ntwo";
        bool keyCharInsertsAtCaretOnce =
            area.HandleCharacter('a') == GuideXosTextAreaEditResult.Changed &&
            area.Text == "zero\nonae\ntwo" && area.CaretIndex == 8;
        bool invalidCharacterIsRejectedTransactionally =
            area.HandleCharacter('\u0001') ==
                GuideXosTextAreaEditResult.Rejected &&
            area.Text == "zero\nonae\ntwo" && area.CaretIndex == 8;

        return unfocusedRejects && outsideDoesNotFocus && pointerFocus &&
            keydownDoesNotInsertCharacter && keyCharInsertsAtCaretOnce &&
            invalidCharacterIsRejectedTransactionally && area.Length == 13;
    }
}
