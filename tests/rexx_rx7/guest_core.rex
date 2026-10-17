say "CALL RETURN"
x = 10
call add_value 5
say x
say result
say "FUNCTION"
say square(5)
say "PROCEDURE"
x = 10
call isolate
say x
say "EXPOSE"
shared = 10
call update
say shared
say "DO LOOP CALL"
loop_total = 0
do loop_index = 1 to 3
    call add_loop_index
end
say loop_total
exit 0

add_value:
    parse arg amount
    x = x + amount
    return x

square: procedure
    parse arg number
    return number * number

isolate: procedure
    x = 99
    return

update: procedure expose shared
    shared = shared + 1
    return

add_loop_index:
    loop_total = loop_total + loop_index
    return
