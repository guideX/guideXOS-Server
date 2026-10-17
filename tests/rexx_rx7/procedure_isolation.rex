x = 10
items.1 = 'caller'
call isolate
say x
say items.1
exit 0

isolate: procedure
    x = 99
    items.1 = 'local'
    return
