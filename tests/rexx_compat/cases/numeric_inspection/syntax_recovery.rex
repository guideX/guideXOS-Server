signal on syntax name trapped
say format('1',0)
exit 1
trapped:
say 'TRAPPED'
signal off syntax
say abs('-2')
