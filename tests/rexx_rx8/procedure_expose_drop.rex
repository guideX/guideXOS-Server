x = "present"
call worker
say x
exit 0

worker: procedure expose x
    drop x
    return
