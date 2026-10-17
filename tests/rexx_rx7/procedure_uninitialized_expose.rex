call setShared
say shared
exit 0

setShared: procedure expose shared
    shared = 'initialized'
    return
