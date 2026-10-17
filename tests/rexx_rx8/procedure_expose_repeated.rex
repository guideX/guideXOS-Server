names = "a A a"
a = 1
call worker
say a
exit 0

worker: procedure expose (names)
    a = a + 1
    return
