#include <stdio.h>

#include <sel4/sel4.h>
#include <arch_stdio.h>

void __plat_putchar(int c);
static size_t write_buf(void *data, size_t count) {
  char *buf = data;
  for (int i = 0; i < count; i++) {
    __plat_putchar(buf[i]);
  }
  return count;
}

int main(int argc, char **argv) {
  sel4muslcsys_register_stdio_write_fn(write_buf);
  printf("Start ramdisk driver\n");

  return 0;
}