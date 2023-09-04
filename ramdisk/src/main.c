#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch_stdio.h>
#include <sel4/sel4.h>

#include <service/syscall.h>

#define MAX_RAMDISK_SIZE 1048576
char RAMDISK[MAX_RAMDISK_SIZE];

static seL4_CPtr client_ep;
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
  printf("Start ramdisk driver\n");

  /* read in init data */
  init_data = (void *)atol(argv[1]);
  assert(init_data->magic == 0xdeadbeef);
  client_ep = init_data->client_ep;

  while (1) {
#ifdef TEST_NORMAL
    int ret = 0, blockno;
    seL4_MessageInfo_t info = seL4_Recv(client_ep, NULL);
    switch (seL4_MessageInfo_get_label(info)) {
    case DISK_INIT:
      break;
    case DISK_READ:
      blockno = seL4_GetMR(0);
      printf("[ramdisk] read %d\n", blockno);
      memmove(init_data->client_buf, RAMDISK + blockno * 512, 512);
      break;
    case DISK_WRITE:
      blockno = seL4_GetMR(0);
      printf("[ramdisk] write %d\n", blockno);
      memmove(RAMDISK + blockno * 512, init_data->client_buf, 512);
    default:
      ret = -EINVAL;
      ZF_LOGE("FS call unimplemented!");
      break;
    }
    seL4_SetMR(0, ret);
    seL4_Reply(info);
#endif
  }

  return 0;
}