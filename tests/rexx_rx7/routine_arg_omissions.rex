call inspect "alpha",, "omega"
call empty
exit 0

inspect:
    say arg()
    say arg(1)
    say arg(2, "E")
    say arg(2, "O")
    say arg(3)
    return

empty:
    say arg()
    say arg(1, "E")
    say arg(1, "O")
    return
