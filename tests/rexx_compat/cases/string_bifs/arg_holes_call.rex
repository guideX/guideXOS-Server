call inspect "alpha",,""
exit 0
inspect: procedure
  say arg()
  say arg(1)
  say arg(2,"O")
  say arg(2,"E")
  say arg(3,"O")
  say arg(3,"E")
  say arg(4)
  return