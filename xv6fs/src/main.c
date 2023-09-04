#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch_stdio.h>
#include <sel4/sel4.h>

#include <service/env.h>

static seL4_CPtr client_ep;
static seL4_CPtr server_ep;
static init_data_t init_data;

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
  printf("Start xv6fs server\n");

  /* parse args */
  assert(argc == 4);
  client_ep = (seL4_CPtr)atoi(argv[1]);
  server_ep = (seL4_CPtr)atoi(argv[2]);

  /* read in init data */
  init_data = (void *)atol(argv[3]);
  assert(init_data->magic == 0xdeadbeef);

  return 0;
}