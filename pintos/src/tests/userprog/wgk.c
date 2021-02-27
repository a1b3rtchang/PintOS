#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int exit_code = wait(exec("kid"));
  int uhoh = wait(exit_code);
  msg("%d", uhoh);
}
