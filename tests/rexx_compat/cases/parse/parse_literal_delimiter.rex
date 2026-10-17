source = 'left::middle::right'
parse var source first '::' second '::' third
say '[' || first || '][' || second || '][' || third || ']'
