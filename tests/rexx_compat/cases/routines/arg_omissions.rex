call Inspect 'alpha',, 'omega'
exit 0
Inspect:
  say arg()
  say arg(1)
  say arg(2, 'E')
  say arg(2, 'O')
  say arg(3)
  return
