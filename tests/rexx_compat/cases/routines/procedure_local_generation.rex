value = 'outer'
call Local
say value
exit 0
Local:
  procedure
  value = 'inner'
  say value
  return
