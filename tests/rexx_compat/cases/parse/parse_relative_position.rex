source = 'Clemens   Samuel    Mark Twain        '
parse var source last +10 first +10 pseudonym
say '[' || last || '][' || first || '][' || pseudonym || ']'
