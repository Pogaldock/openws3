#include <stdio.h>
int test_main(void);
void main(void) { int r = test_main(); printf("EXIT %d\n", r); for (;;) ; }
