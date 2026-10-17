signal on syntax name syntaxError
say substr("abc",0)
exit 1
syntaxError:
  say condition("C")
  say rc
  say sigl
  say condition("D")
  say "recovered"
  exit 0