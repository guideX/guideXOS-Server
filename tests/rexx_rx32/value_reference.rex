signal on novalue name noValueHandler
name = 'alpha'
alpha = 'before'
say value(name)
say value('alpha','after')
say value('alpha')
say value('missing')
say value('missing','created')
say value('missing')
idx = '2'
items.2 = 'tail'
say value('items.idx')
binary = '4100FF80'X
say c2x(value('binary'))
say 'RX32_VALUE_CLI_DONE'
exit 0
noValueHandler:
say 'RX32_VALUE_NOVALUE_UNEXPECTED'
exit 8
