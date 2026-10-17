names = 'first second'
first = 'A'
second = 'B'
call Change
say first || second
exit 0
Change:
  procedure expose (names)
  first = first || '1'
  second = second || '2'
  return
