say "CALL AND RETURN"
value = 10
call increase 5
say value
say result

say "INTERNAL FUNCTION"
say square(5)

say "PROCEDURE ISOLATION"
value = 10
call isolate
say value

say "PROCEDURE EXPOSE"
shared = 10
call update
say shared

say "ROUTINE PARSE ARG"
call display "hello world", "second argument"

say "RECURSION"
say factorial(5)

say "NUMERIC RESTORE"
numeric digits 9
numeric fuzz 0
numeric form scientific
call change_settings
say digits()
say fuzz()
say form()
exit 0

increase:
    parse arg amount
    value = value + amount
    return value

square: procedure
    parse arg number
    return number * number

isolate: procedure
    value = 99
    return

update: procedure expose shared
    shared = shared + 1
    return

display:
    parse arg first, second
    say first
    say second
    return

factorial: procedure
    parse arg number
    if number = 0 then return 1
    return number * factorial(number - 1)

change_settings:
    numeric digits 12
    numeric fuzz 2
    numeric form engineering
    return
