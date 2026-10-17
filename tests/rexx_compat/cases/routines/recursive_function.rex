say Factorial(6)
exit 0
Factorial:
  procedure
  parse arg number
  if number <= 1 then return 1
  return number * Factorial(number - 1)
