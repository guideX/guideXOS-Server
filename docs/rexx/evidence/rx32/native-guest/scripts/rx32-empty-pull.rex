say 'RX32_EMPTY_PULL_BEGIN'
queue 'consumed-before-empty-read'
parse pull discarded
pull missing
say 'RX32_EMPTY_PULL_UNEXPECTED_RETURN'
exit 90
