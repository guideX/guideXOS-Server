signal on novalue name Missing
step = 0
say firstMissing
exit 0
Missing:
step = step + 1
say step
if step = 1 then signal on novalue name Missing
if step = 2 then signal off novalue
say secondMissing
exit 0
