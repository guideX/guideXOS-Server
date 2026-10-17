items.1 = 'caller-one'
items.2 = 'caller-two'
call changeOne
say items.1
say items.2
exit 0

changeOne: procedure expose items.1
    items.1 = 'shared-one'
    items.2 = 'local-two'
    return
