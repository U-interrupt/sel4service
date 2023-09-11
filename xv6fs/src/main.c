#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch_stdio.h>
#include <sel4/sel4.h>

#include <service/env.h>

#include "defs.h"

static init_data_t init_data;
#ifdef TEST_NORMAL
static seL4_CPtr client_ep;
static seL4_CPtr server_ep;
#elif defined(TEST_UINTR)
static int client_index;
static int server_index;
#endif

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

#ifdef TEST_NORMAL
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
      panic("Failed to read block");
    memmove(buf, init_data->server_buf, BSIZE);
  }
}
#elif defined(TEST_POLL)
void disk_rw(void *buf, int blockno, int write) {
  if (write) {
    seL4_Word *server_buf = init_data->server_buf;
    acquire(init_data->server_lk);
    server_buf[0] = DISK_WRITE;
    server_buf[1] = blockno;
    memmove(&server_buf[2], buf, BSIZE);
    release(init_data->server_lk);
    if (Call(server_buf))
      panic("Failed to write block");
  } else {
    seL4_Word *server_buf = init_data->server_buf;
    acquire(init_data->server_lk);
    server_buf[0] = DISK_READ;
    server_buf[1] = blockno;
    release(init_data->server_lk);
    Wait(server_buf);
    if (server_buf[1])
      panic("Failed to read block");
    memmove(buf, &server_buf[2], BSIZE);
    release(init_data->server_lk);
  }
}
#elif defined(TEST_UINTR)
static void CallBadged() {
  seL4_Word badge;
  seL4_UintrSend(server_index);
  while (1) {
    seL4_UintrNBRecv(&badge);
    /* write the pending bits back */
    if (badge & 1)
      uipi_write(1);
    if (badge & 2)
      break;
  }
}

void disk_rw(void *buf, int blockno, int write) {
  if (write) {
    seL4_Word *server_buf = init_data->server_buf;
    server_buf[0] = DISK_WRITE;
    server_buf[1] = blockno;
    memmove(&server_buf[2], buf, BSIZE);
    CallBadged();
    if (server_buf[1])
      panic("Failed to write block");
  } else {
    seL4_Word *server_buf = init_data->server_buf;
    server_buf[0] = DISK_READ;
    server_buf[1] = blockno;
    CallBadged();
    if (server_buf[1])
      panic("Failed to read block");
    memmove(buf, &server_buf[2], BSIZE);
  }
}
#endif

int main(int argc, char **argv) {
  sel4muslcsys_register_stdio_write_fn(write_buf);
  printf("Start xv6fs server\n");

  /* read in init data */
  init_data = (void *)atol(argv[1]);
  assert(init_data->magic == 0xdeadbeef);
  setup_init_data(init_data);

#ifdef TEST_NORMAL
  client_ep = init_data->client_ep;
  server_ep = init_data->server_ep;
#elif defined(TEST_UINTR)
  client_index = seL4_RISCV_Uintr_RegisterSender(init_data->client_uintr).index;
  server_index = seL4_RISCV_Uintr_RegisterSender(init_data->server_uintr).index;
#endif

  curr_client = (struct client *)malloc(sizeof(struct client));
  curr_client->cwd = namei("/");
  strcpy(curr_client->cwd_path, "/");

  /* initialize fs */
  binit();
  iinit();
  fileinit();
  fsinit(ROOTDEV);
  printf("[xv6fs] fs initialized successfully\n");

  while (1) {
    int label, ret;
#ifdef TEST_NORMAL
    seL4_MessageInfo_t info = seL4_Recv(client_ep, NULL);
    label = seL4_MessageInfo_get_label(info);
#elif defined(TEST_POLL)
    acquire(init_data->client_lk);
    argint(-1, &label);
    if (label == FS_RET) {
      release(init_data->client_lk);
      continue;
    }
#elif defined(TEST_UINTR)
    seL4_Word badge;
    seL4_UintrNBRecv(&badge);
    /* write the pending bits back */
    if (badge & 2)
      uipi_write(2);
    if ((badge & 1) == 0)
      continue;
    argint(-1, &label);
#endif
    switch (label) {
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
      break;
    }
    // printf("[xv6fs] FS call %d return %d\n", label, ret);
#ifdef TEST_NORMAL
    info = seL4_MessageInfo_new(seL4_MessageInfo_get_label(info), 0, 0, 1);
    seL4_SetMR(0, ret);
    seL4_Reply(info);
#elif defined(TEST_POLL)
    seL4_Word *buf = init_data->client_buf;
    buf[0] = FS_RET;
    buf[1] = ret;
    release(init_data->client_lk);
#elif defined(TEST_UINTR)
    seL4_Word *buf = init_data->client_buf;
    buf[0] = FS_RET;
    buf[1] = ret;
    seL4_UintrSend(client_index);
#endif
  }

  return 0;
}