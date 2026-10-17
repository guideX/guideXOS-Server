say 'RX11_NATIVE_LOGIC_START'
say 0 & 0
say 0 & 1
say 1 & 0
say 1 & 1
say 0 | 0
say 0 | 1
say 1 | 0
say 1 | 1
say 0 && 0
say 0 && 1
say 1 && 0
say 1 && 1
say \ 0
say \ 1
signal on syntax name Trap
say 2 & 1
say 'RX11_NATIVE_RECOVERY_FAILED'
exit 9
Trap:
say 'RX11_NATIVE_SYNTAX_RECOVERED'
exit 0
