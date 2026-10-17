names = ""
a = 1
call worker
say a
exit 0

worker: procedure expose (names)
    a = 2
    return
