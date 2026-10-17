call once
exit 0

once:
    do
        say "inside"
        call nested
    end
    return

nested:
    say "nested"
    return
