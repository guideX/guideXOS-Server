signal on syntax name trapped
say x2d('GG')
exit 99
trapped:
signal off syntax
say 'RX15_CONVERSION_RECOVERED' c2x(d2c(-1,4))
