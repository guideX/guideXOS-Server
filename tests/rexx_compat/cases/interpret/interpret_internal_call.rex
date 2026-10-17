source = 'call Add 2, 3'
interpret source
say result
exit 0
Add:
  procedure
  parse arg left, right
  return left + right
