result = 'before'
call SetResult
say result
exit 0
SetResult:
  return 'after'
