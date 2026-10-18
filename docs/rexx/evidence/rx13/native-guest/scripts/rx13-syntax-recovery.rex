say 'RX13_NATIVE_SYNTAX_START'
signal on syntax name Trap
say left('x',-1)
say 'RX13_NATIVE_SYNTAX_FAILED'
exit 91
Trap:
say 'RX13_NATIVE_SYNTAX_RECOVERED:' words('one two')
signal off syntax
exit 0
