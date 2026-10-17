call later
exit 0

first:
    return 1

later:
    call first
    say result
    return
