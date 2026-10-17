call display 'hello world',, 'last'
call outer 'outer'
exit 0

display:
    parse arg first, second, third
    say first
    say second
    say third
    say arg()
    say arg(2, 'E')
    return

outer:
    parse arg value
    call inner 'inner'
    say arg(1)
    return

inner:
    parse arg value
    say arg(1)
    return
