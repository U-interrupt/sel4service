#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch_stdio.h>
#include <sel4/sel4.h>

#include <service/env.h>

#include "defs.h"
#include "service/syscall.h"

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

void disk_rw(void *buf, int blockno, int write) {
  if (write) {
    seL4_MessageInfo_t info = seL4_MessageInfo_new(DISK_WRITE, 0, 0, 1);
    memmove(init_data->server_buf, buf, BSIZE);
    seL4_SetMR(0, blockno);
    info = seL4_Call(server_ep, info);
    if (seL4_GetMR(0))
      panic("Failed to write block");
  } else {
    seL4_MessageInfo_t info = seL4_MessageInfo_new(DISK_READ, 0, 0, 1);
    seL4_SetMR(0, blockno);
    info = seL4_Call(server_ep, info);
    if (seL4_GetMR(0))
      panic("Failed to write block");
    memmove(buf, init_data->server_buf, BSIZE);
  }
}

int main(int argc, char **argv) {
  sel4muslcsys_register_stdio_write_fn(write_buf);
  printf("Start xv6fs server\n");

  /* read in init data */
  init_data = (void *)atol(argv[1]);
  assert(init_data->magic == 0xdeadbeef);
  setup_init_data(init_data);
  client_ep = init_data->client_ep;
  server_ep = init_data->server_ep;

  curr_client = (struct client *)malloc(sizeof(struct client));
  curr_client->cwd = namei("/");
  strcpy(curr_client->cwd_path, "/");

  /* initialize fs */
  binit();
  iinit();
  fileinit();
  fsinit(ROOTDEV);
  printf("[xv6fs] fs initialized successfully\n");

  while(1);

  while (1) {
#ifdef TEST_NORMAL
    int ret;
    seL4_MessageInfo_t info = seL4_Recv(client_ep, NULL);
    switch (seL4_MessageInfo_get_label(info)) {
#elif defined(TEST_POLL)
    int label, ret;
    acquire(init_data->client_lk);
    argint(-1, &label);
    if (label == FS_RET) {
      release(init_data->client_lk);
      continue;
    }
    switch (label) {
#endif
    case FS_OPEN:
      ret = xv6fs_open();
      break;
    case FS_CLOSE:
      ret = xv6fs_close();
      break;
    case FS_FSTAT:
      ret = xv6fs_fstat();
      break;
    case FS_GETCWD:
      ret = xv6fs_getcwd();
      break;
    case FS_LSTAT:
      ret = xv6fs_lstat();
      break;
    case FS_READ:
      ret = xv6fs_read();
      break;
    case FS_WRITE:
      ret = xv6fs_write();
      break;
    case FS_PREAD:
      ret = xv6fs_pread();
      break;
    case FS_PWRITE:
      ret = xv6fs_pwrite();
      break;
    case FS_LSEEK:
      ret = xv6fs_lseek();
      break;
    case FS_UNLINK:
      ret = xv6fs_unlink();
      break;
    default:
      ret = -EINVAL;
      ZF_LOGE("FS call unimplemented!");
      break;
    }
#ifdef TEST_NORMAL
    // printf("[xv6fs] return %d\n", ret);
    info = seL4_MessageInfo_new(seL4_MessageInfo_get_label(info), 0, 0, 1);
    seL4_SetMR(0, ret);
    seL4_Reply(info);
#elif defined(TEST_POLL)
    seL4_Word *buf = init_data->client_buf;
    buf[0] = FS_RET;
    buf[1] = ret;
    release(init_data->client_lk);
#endif
  }

  return 0;
}