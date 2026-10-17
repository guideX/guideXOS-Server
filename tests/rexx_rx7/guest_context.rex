say "PARSE ARG"
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
