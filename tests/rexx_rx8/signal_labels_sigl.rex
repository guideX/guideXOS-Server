/* SIGL counts physical source lines, including this comment. */

say "Before"; signal finished; say "Skipped on same source line"
say "Skipped"
finished: say "After"; say sigl
exit 0
