signal on syntax name trapped
say format('1',0)
say 'RX14_NATIVE_SYNTAX_FAILED'
exit 91
trapped:
signal off syntax
say 'RX14_NATIVE_SYNTAX_RECOVERED:' abs('-2')
exit 0
