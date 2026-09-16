namespace HostLogProof;

/// <summary>Small C116 text-input regression probe reused by later phases.</summary>
public static class GuideXosTextInputTests
{
    public static bool Run(GuideXosHost host)
    {
        GuideXosTextInput input = new(5, "filename");
        bool unfocused = input.HandleCharacter('A') ==
            GuideXosTextInputEditResult.Ignored && input.Length == 0;
        input.Focus();
        bool typed = true;
        for (char value = 'A'; value <= 'E'; value++)
        {
            typed &= input.HandleCharacter(value) ==
                GuideXosTextInputEditResult.Changed;
        }
        bool overflow = input.HandleCharacter('F') ==
            GuideXosTextInputEditResult.Rejected && input.Value == "ABCDE";
        bool movement = input.HandleKey(GuideXosTextInputKey.Left) !=
                GuideXosTextInputEditResult.Ignored &&
            input.HandleKey(GuideXosTextInputKey.Right) !=
                GuideXosTextInputEditResult.Ignored;
        bool delete = input.HandleKey(GuideXosTextInputKey.Left) !=
                GuideXosTextInputEditResult.Ignored &&
            input.HandleKey(GuideXosTextInputKey.Delete) ==
                GuideXosTextInputEditResult.Changed && input.Value == "ABCD";
        bool backspace = input.HandleCharacter('E') ==
                GuideXosTextInputEditResult.Changed &&
            input.HandleKey(GuideXosTextInputKey.Backspace) ==
                GuideXosTextInputEditResult.Changed && input.Value == "ABCD";

        GuideXosTextInput pointer = new(16);
        bool pointerFocus = pointer.HandlePointerDown(0, 0) ==
            GuideXosTextInputEditResult.Focused && pointer.IsFocused;
        GuideXosTextInput submit = new(16);
        submit.Focus();
        bool enter = submit.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosTextInputEditResult.Submitted && submit.IsSubmitted &&
            !submit.IsFocused;
        GuideXosTextInput cancel = new(16);
        cancel.Focus();
        bool escape = cancel.HandleKey(GuideXosTextInputKey.Escape) ==
            GuideXosTextInputEditResult.Cancelled && cancel.IsCancelled &&
            !cancel.IsFocused;
        bool result = unfocused && typed && overflow && movement && delete &&
            backspace && pointerFocus && enter && escape;
        if (host != null)
        {
            host.TryLog(result
                ? "C116-NEGATIVE bounds=PASS edit=PASS focus=PASS submit=PASS cancel=PASS result=PASS"u8
                : "C116-NEGATIVE result=FAIL"u8);
        }
        return result;
    }
}
