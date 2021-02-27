# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(over-write) begin
Lorem ipsum
(over-write) end
over-write: exit(0)
EOF
pass;