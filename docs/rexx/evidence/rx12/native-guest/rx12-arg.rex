say 'RX12_NATIVE_ARG_START'
say 'Top-level ARG():' arg()
call inspect 'alpha',,'gamma'
call inspect 'alpha','','gamma'
call inspect
say 'ARG_FUNCTION:' argquery('x',,'z')
say 'RX12_NATIVE_ARG_DONE'
exit 0
inspect: procedure
  say 'ARG():' arg()
  say 'ARG1:' arg(1)
  say 'ARG2O:' arg(2,'O')
  say 'ARG2E:' arg(2,'E')
  say 'ARG3:' arg(3)
  return
argquery: procedure
  return arg()||':'||arg(2,'O')