say "RX8 EXPOSE"
names = "shared a.2 stem."
shared = "before"
a.2 = "compound"
stem.1 = "stem"
call modify
say shared a.2 stem.1
say "RX8 SIGNAL"
destination = "DYNAMIC"
signal value destination
say "BAD value fallthrough"
dynamic:
    say "dynamic target"
    say sigl
signal finished
say "BAD signal fallthrough"
finished:
    do i = 1 to 5
        if i = 3 then signal after_loop
        say i
    end
    say "BAD active loop"
after_loop:
    say "loop terminated"
    say sigl
exit 0

modify: procedure expose (names)
    shared = "shared-ok"
    a.2 = "compound-ok"
    stem.1 = "stem-ok"
    return
