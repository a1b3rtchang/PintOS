#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

// dolor sit amet, consectetur adipiscing elit. Donec tristique purus a quam consequat congue. Aenean sapien nulla, tincidunt ut tincidunt a, ultrices scelerisque lorem. Vivamus faucibus ante a ligula venenatis, nec aliquet libero maximus. Nulla facilisi. Aliquam aliquam condimentum purus, imperdiet porta elit fermentum ut. Nullam volutpat sem sit amet tortor pellentesque tincidunt. Duis tempus, lorem id scelerisque faucibus, lorem turpis vestibulum nisi, a ornare metus nulla eget magna. Aenean condimentum fringilla volutpat.\nDonec in cursus odio. Nunc dignissim rhoncus ligula et imperdiet. Integer mauris ex, cursus eu vehicula vel, consequat eu tortor. Nam vehicula eget leo et scelerisque. Aenean non elementum lectus, iaculis aliquam odio. Phasellus at tincidunt nunc. Nullam vel vulputate orci. Nam in elit ac turpis fermentum dapibus. Cras lacinia purus ac nisi sodales sodales. Cras ornare risus vitae nulla varius, at suscipit risus malesuada. Etiam scelerisque lacinia mi sed.\n";
void test_main(void) {
  char buf[13] = "Lorem ipsum\n";
  write(1, buf, 13);
}