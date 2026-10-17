signal on novalue name missingValue
call worker
say missingAfterReturn
say "not executed"
exit 0

worker:
signal off novalue
return

missingValue:
say condition("C")
say condition("I")
say sigl
exit 0
