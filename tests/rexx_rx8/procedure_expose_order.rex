drop i
a.i = "unmodified reverse binding"
i = 2
a.2 = "original"
call ordered
say a.2
call reversed
say a.2
say a.i
exit 0

ordered: procedure expose i a.i
    a.i = "ordered"
    return

reversed: procedure expose a.i i
    drop i
    a.i = "reversed"
    return
