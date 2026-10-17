say recurse(0)
exit 0

recurse: procedure
    parse arg depth
    return recurse(depth + 1)
