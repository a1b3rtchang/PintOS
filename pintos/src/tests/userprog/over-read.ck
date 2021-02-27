# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(over-read) begin
(over-read) 1002
(over-read) end
over-read: exit(0)
EOF
pass;