call show_call_line
say 1 + choose()
exit 0

show_call_line: procedure expose sigl
    say sigl
    return

choose: procedure expose sigl
    signal value "FUNCTION_TARGET"
    return 0

function_target:
    say sigl
    return 40
