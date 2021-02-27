#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int fd = open("lorem.txt");
  char buf[1002];
  int num_char = read(fd, buf, 1004);
  msg("%d", num_char);
}
