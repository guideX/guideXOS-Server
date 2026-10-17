signal on novalue name Missing
call Check
say absentAfterReturn
exit 0
Check:
  procedure
  signal off novalue
  return
Missing:
say 'handled'
exit 0
