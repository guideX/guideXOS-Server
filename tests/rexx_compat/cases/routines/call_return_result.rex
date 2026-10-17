call Echo 'hello'
say result
exit 0
Echo:
  procedure
  parse arg value
  return value
