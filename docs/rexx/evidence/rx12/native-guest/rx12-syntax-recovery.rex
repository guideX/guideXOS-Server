say 'RX12_NATIVE_SYNTAX_START'
signal on syntax name Trap
say substr('abc',0)
say 'RX12_NATIVE_SYNTAX_FAILED'
exit 91
Trap:
say 'SYNTAX:' condition('C')
say 'RC:' rc
say 'SIGL:' sigl
say 'DETAIL:' condition('D')
say 'RX12_NATIVE_SYNTAX_RECOVERED:' words('one two')
signal off syntax
exit 0