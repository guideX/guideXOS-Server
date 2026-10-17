call inspect "present",, "last"
exit 0

inspect:
    say arg(1, "exists")
    say arg(2, "omitted")
    say arg(3, "eanything")
    return
