# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(wgk) begin
kid: exit(5)
(wgk) -1
(wgk) end
wgk: exit(0)
EOF
pass;