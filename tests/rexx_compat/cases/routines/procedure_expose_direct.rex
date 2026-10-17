value = 1
call Increment
say value
exit 0
Increment:
  procedure expose value
  value = value + 1
  return
