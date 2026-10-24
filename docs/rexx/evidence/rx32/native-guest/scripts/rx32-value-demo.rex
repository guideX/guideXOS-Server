name = 'absent'
defaultValue = value(name)
if defaultValue <> 'ABSENT' then exit 1
say 'RX32_VALUE_DEFAULT_BOUNDARY='||defaultValue
alpha = 'before'
oldValue = value('alpha','after')
if oldValue <> 'before' then exit 2
if value('alpha') <> 'after' then exit 3
oldValue = value('alpha','')
if oldValue <> 'after' then exit 4
if length(alpha) <> 0 then exit 5
say 'RX32_VALUE_EMPTY_ASSIGNMENT_LENGTH='||length(alpha)
idx = '3'
items.3 = 'tail'
if value('items.idx') <> 'tail' then exit 6
say 'RX32_VALUE_COMPOUND='||value('items.idx')
say 'RX32_VALUE_DEMO_DONE'
exit 0
