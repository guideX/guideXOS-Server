/* RX15 isolated guest acceptance: all eight conversion built-ins. */
say 'RX15_NATIVE_CONVERSIONS_START'
say c2x('ABC') c2x(X'00FF') c2d(X'81') c2d(X'81',1) c2d(X'81',2)
say d2x(129) d2x(129,4) d2x(-127,2) d2x(-127,4)
say c2x(d2c(129,2)) c2x(d2c(-1,4))
say x2d('FF') x2d('FF',2) x2d('FF',4) c2x(x2c('F'))
say b2x('11000011') b2x('10111') x2b('C3') x2b('7')
say 'RX15_NATIVE_CONVERSIONS_DONE'
exit 0
