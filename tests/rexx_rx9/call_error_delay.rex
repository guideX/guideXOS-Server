call seed
call on error name handleError
address KERNEL "positive"
say "resumed"
say seenC
say seenD
say seenI
say seenS
say seenLine
say result
say length(condition("C"))
exit 0

seed:
return "OLD"

handleError:
seenC = condition("C")
seenD = condition("D")
seenI = condition("I")
seenS = condition("S")
seenLine = sigl
address KERNEL "positive"
return "IGNORED"
