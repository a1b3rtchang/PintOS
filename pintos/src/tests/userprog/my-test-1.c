#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) { write(1, "HELLO WORLD\n", 12); }
