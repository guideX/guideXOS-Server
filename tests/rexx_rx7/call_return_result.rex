x = 10
call increase 5
say x
say result
call clearResult
say result
exit 0

increase:
    parse arg amount
    x = x + amount
    return x

clearResult:
    return
