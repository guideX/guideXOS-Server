first = RANDOM(1,6,12345)
next = RANDOM(1,6)
reseeded = RANDOM(1,6,12345)
IF first <> reseeded THEN EXIT 10
IF first < 1 | first > 6 THEN EXIT 11
IF next < 1 | next > 6 THEN EXIT 12
SAY 'RX25_SEEDED_RANGE_RESEED_OK' first next reseeded

SIGNAL ON SYNTAX NAME invalid_seed
ignored = RANDOM(0,10,1000000000)
EXIT 13
invalid_seed:
SIGNAL OFF SYNTAX
SAY 'RX25_INVALID_SEED_CAUGHT'

SIGNAL ON SYNTAX NAME invalid_range
ignored = RANDOM(8,5)
EXIT 14
invalid_range:
SIGNAL OFF SYNTAX
SAY 'RX25_INVALID_RANGE_CAUGHT'

after_error = RANDOM(1,6)
IF after_error < 1 | after_error > 6 THEN EXIT 15
SAY 'RX25_POST_ERROR_RANDOM_RECOVERED' after_error
SAY 'RX25_RANDOM_SEQUENCE_DONE'
