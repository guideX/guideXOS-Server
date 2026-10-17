call descend 3
say "top returned"
exit 0

descend: procedure
    parse arg depth
    if depth = 0 then signal finish
    call descend depth - 1
    return

finish:
    say arg(1)
    return
