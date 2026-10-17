say square(5)
say factorial(5)
exit 0

square: procedure
    parse arg value
    return value * value

factorial: procedure
    parse arg n
    if n = 0 then return 1
    return n * factorial(n - 1)
