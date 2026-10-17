/* RX12 classic string-processing demonstration */
message = "  guideXOS REXX scripting  "
message = strip(message)
say "Length:" length(message)
say "Words:" words(message)
say "Second:" word(message,2)
say "Upper:" translate(message)
say "Prefix:" substr(message,1,8)
say "REXX position:" pos("REXX",message)
exit 0