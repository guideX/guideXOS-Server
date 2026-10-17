delimiter = ' '
parse value 'alpha beta gamma' with first (delimiter) rest
say '[' || first || '][' || rest || ']'
