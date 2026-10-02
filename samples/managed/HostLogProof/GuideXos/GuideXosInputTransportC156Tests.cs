#if HOSTLOGPROOF_C156_CONTROL_MODIFIER_SHORTCUTS
namespace HostLogProof;

internal static unsafe class GuideXosInputTransportC156Tests
{
    private static int s_cases;

    internal static bool Run(GuideXosHost host)
    {
        s_cases = 0;
        GuideXosLaunchContext context = new();
        bool result = Decode(context, KeyDown('z'), out _) &&
            Check(context.InputKind == GuideXosInputKind.KeyDown &&
                context.InputKeyCode == 'z' && !context.InputShift &&
                !context.InputControl) &&
            Decode(context, KeyDown('Z') | GxAbi.LaunchFlagInputShift,
                out _) &&
            Check(context.InputKeyCode == 'Z' && context.InputShift &&
                !context.InputControl) &&
            Decode(context, KeyDown('z') | GxAbi.LaunchFlagInputControl,
                out _) &&
            Check(context.InputKeyCode == 'z' && !context.InputShift &&
                context.InputControl) &&
            Decode(context, KeyDown('Z') | GxAbi.LaunchFlagInputShift |
                GxAbi.LaunchFlagInputControl, out _) &&
            Check(context.InputKeyCode == 'Z' && context.InputShift &&
                context.InputControl) &&
            Decode(context, KeyDown('q'), out _) &&
            Check(context.InputKeyCode == 'q' && !context.InputShift &&
                !context.InputControl) &&
            Decode(context, KeyChar('c') | GxAbi.LaunchFlagInputControl,
                out _) &&
            Check(context.InputKind == GuideXosInputKind.KeyChar &&
                context.InputCharacter == 'c' && context.InputControl) &&
            Decode(context, PointerMove(0x00C00000u), out _) &&
            Check(context.InputKind == GuideXosInputKind.PointerMove &&
                !context.InputShift && !context.InputControl) &&
            Decode(context, PointerDown(0x00400000u), out _) &&
            Check(context.InputKind == GuideXosInputKind.PointerDown &&
                !context.InputShift && !context.InputControl) &&
            Check(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Tab, shift: true, control: true).Shift &&
                GuideXosInputEvent.ForKeyDown(
                    GuideXosTextInputKey.Tab, shift: true, control: true).Control) &&
            Decode(context, KeyDown('r'), out _) &&
            Check(!context.InputControl && !context.InputShift);

        host.TryLog(result && s_cases == 10
            ? "C156-MODIFIER-DECODE cases=10 bits=PASS pointer=isolated lifecycle=stateless result=PASS"u8
            : "C156-MODIFIER-DECODE result=FAIL"u8);
        return result && s_cases == 10;
    }

    private static uint KeyDown(uint key) =>
        GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputKeyDown | key;

    private static uint KeyChar(uint character) =>
        GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputKeyChar | character;

    private static uint PointerDown(uint payload) =>
        GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputPointerDown | payload;

    private static uint PointerMove(uint payload) =>
        GxAbi.LaunchFlagInput | payload;

    private static bool Decode(GuideXosLaunchContext target, uint flags,
        out GuideXosLaunchContext decoded)
    {
        NativeGxAppContext native = default;
        native.size = (uint)sizeof(NativeGxAppContext);
        native.launchFlags = flags;
        return target.TryCopy(&native, 4u, out decoded);
    }

    private static bool Check(bool condition)
    {
        ++s_cases;
        return condition;
    }
}
#endif
