call recurse 3
say result
exit 0

recurse: procedure
    parse arg number
    if number = 0 then return 0
    code = 'call recurse ' || (number - 1)
    interpret code
    return result + 1
