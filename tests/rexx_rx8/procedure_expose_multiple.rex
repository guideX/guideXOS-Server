direct_before = 1
first_list = "a"
second_list = "b"
a = 10
b = 20
direct_after = 30
call modify
say direct_before a b direct_after
exit 0

modify: procedure expose direct_before (first_list) (second_list) direct_after
    direct_before = direct_before + 1
    a = a + 1
    b = b + 1
    direct_after = direct_after + 1
    return
