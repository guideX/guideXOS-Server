say 'RX37_SAY_OUTPUT_BEGIN'
say 'RX37_SAY_118_BEGIN'
say copies('A', 118)
say 'RX37_SAY_118_END'
say 'RX37_SAY_119_BEGIN'
say copies('B', 119)
say 'RX37_SAY_119_END'
say 'RX37_SAY_120_BEGIN'
say copies('C', 120)
say 'RX37_SAY_120_END'
say 'RX37_SAY_EXPLICIT_CRLF_BEGIN'
say 'A' || x2c('0D0A') || 'B'
say 'RX37_SAY_OUTPUT_DONE'
