stem.1 = 'one'
call Update
say stem.1
exit 0
Update:
  procedure expose stem.
  stem.1 = stem.1 || '-changed'
  return
