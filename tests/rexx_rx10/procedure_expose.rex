shared = 'before'
call helper 5
say shared
say result
exit 0

helper: procedure expose shared
    parse arg number
    code = 'shared = "after"; say arg(1)'
    interpret code
    return number
