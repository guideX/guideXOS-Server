names = "a b.1 values."
next = "c"
a = 10
b.1 = 20
values.2 = 30
c = 40
direct = 5
call worker
say a b.1 values.2 c direct names
exit 0

worker: procedure expose direct (names) (next)
    a = a + 1
    b.1 = b.1 + 1
    values.2 = values.2 + 1
    c = c + 1
    direct = direct + 1
    names = "changed"
    return
