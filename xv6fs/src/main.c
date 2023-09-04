#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch_stdio.h>
#include <sel4/sel4.h>

#include <service/env.h>

#include "defs.h"

static seL4_CPtr client_ep;
static seL4_CPtr server_ep;
static init_data_t init_data;

static struct client *curr_client = NULL;

struct client *curr(void) {
  return curr_client;
}

void __plat_putchar(int c);
static size_t write_buf(void *data, size_t count) {
  char *buf = data;
  for (int i = 0; i < count; i++) {
    __plat_putchar(buf[i]);
  }
  return count;
}

void disk_rw(struct buf *buf, int write) {
  if (write) {
    seL4_MessageInfo_t info = seL4_MessageInfo_new(DISK_WRITE, 0, 0, 1);
    memmove(init_data->server_buf, buf->data, BSIZE);
    seL4_SetMR(0, buf->blockno);
    info = seL4_Call(server_ep, info);
    if (seL4_GetMR(0))
      panic("Failed to write block");
  } else {
    seL4_MessageInfo_t info = seL4_MessageInfo_new(DISK_READ, 0, 0, 1);
    seL4_SetMR(0, buf->blockno);
    info = seL4_Call(server_ep, info);
    if (seL4_GetMR(0))
      panic("Failed to write block");
    memmove(buf->data, init_data->server_buf, BSIZE);
  }
}

int main(int argc, char **argv) {
  sel4muslcsys_register_stdio_write_fn(write_buf);
  printf("Start xv6fs server\n");

  /* read in init data */
  init_data = (void *)atol(argv[1]);
  assert(init_data->magic == 0xdeadbeef);
  client_ep = init_data->client_ep;
  server_ep = init_data->server_ep;

  curr_client = (struct client *)malloc(sizeof(struct client));
  curr_client->cwd = namei("/");

  binit();
  iinit();
  fileinit();
  fsinit(ROOTDEV);

  while (1) {
#ifdef TEST_NORMAL
    int ret;
    seL4_MessageInfo_t info = seL4_Recv(client_ep, NULL);
    switch (seL4_MessageInfo_get_label(info)) {
    case FS_OPEN:
      printf("[xv6fs] open path=%s flags=0x%lx\n", (char *)seL4_GetMR(0),
             seL4_GetMR(1));
      xv6fs_open();
      printf("PLJJFLSGJ\n");
      break;
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