signal on syntax name valueError
alpha = 'responsive'
bad = value('')
say 'RX32_VALUE_INVALID_UNEXPECTED_RETURN'
exit 91
valueError:
signal off syntax
say 'RX32_VALUE_INVALID_RECOVERED:'||value('alpha')
exit 0
