signal on syntax name invalid_sourceline
say sourceline(0)
exit 71

invalid_sourceline:
signal off syntax
say 'RX22_NATIVE_INVALID_SOURCELINE_CAUGHT'
signal on syntax name invalid_errortext
say errortext(100)
exit 72

invalid_errortext:
signal off syntax
say 'RX22_NATIVE_INVALID_ERRORTEXT_CAUGHT'
say 'RX22_NATIVE_POST_ERROR_RESPONSIVE:' reverse('ABC')
