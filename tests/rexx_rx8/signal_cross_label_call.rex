say "Before call"
do i = 1 to 2
    call worker i
    say "Caller resumed" i
end
say "Caller loop resumed"
say hidden
exit 0

worker: procedure
    parse arg route
    hidden = "local only"
    do j = 1 to 3
        signal main_target
        say "Callee loop resumed"
    end
    return

main_target:
    say "Cross label" hidden route sigl
    return
