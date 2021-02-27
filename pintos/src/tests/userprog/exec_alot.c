#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

int main(void) {
  for (int i = 0; i < 1000; i++) {
    wait(exec("do-nothing"));
  }
}
