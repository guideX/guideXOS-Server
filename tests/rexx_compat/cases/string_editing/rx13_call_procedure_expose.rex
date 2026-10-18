value = "one   two"
call edit
say value
exit 0
edit: procedure expose value
value = subword(value,1,1)||"|"||subword(value,2)
return

