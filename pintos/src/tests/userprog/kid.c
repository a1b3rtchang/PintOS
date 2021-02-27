#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

int main(void) { exit(exec("loop")); }
