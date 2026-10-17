signal on novalue name Missing
count = 0
say absentVariable
say 'unreachable'
Missing:
count = count + 1
say condition('S')
if count = 1 then signal on novalue name Missing
if count = 2 then signal off novalue
say anotherMissing
exit 0
