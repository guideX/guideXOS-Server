call on error name commandError
interpret 'address KERNEL "failure command"'
say "resumed"
exit 0

commandError:
    say condition('c')
    say condition('s')
    say rc
    say sigl
    return
