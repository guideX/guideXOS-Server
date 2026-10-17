names = "newvalue"
call worker
say newvalue
exit 0

worker: procedure expose (names)
    newvalue = "created"
    return
