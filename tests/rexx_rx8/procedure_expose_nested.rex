x = 1
names = "x"
call outer
say x
exit 0

outer: procedure expose (names)
    call inner
    return

inner: procedure expose x
    x = x + 1
    return
