#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <arch_stdio.h>
#include <sel4/sel4.h>
#include <sel4utils/mapping.h>
#include <sel4utils/util.h>
#include <sel4utils/vspace.h>

#include <service/env.h>
#include <service/syscall.h>

#define BSIZE 1024
#define MAX_RAMDISK_SIZE 256 * 1024 * 1024

/* start at a virtual address below KERNEL_RESERVED_START */
/* this address is a hack to vspace, do not change it !*/
#define RAMDISK_BASE 0x20000000

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
  setup_init_data(init_data);
  client_ep = init_data->client_ep;

  seL4_CPtr ram = (seL4_Word)atol(argv[2]);
  int error = seL4_Untyped_Retype(ram, seL4_RISCV_Mega_Page, seL4_LargePageBits,
                                  init_data->root_cnode, 0, 0,
                                  init_data->free_slots.start, 128);
  ZF_LOGF_IF(error, "Failed to allocate frames");
  seL4_Word vaddr = RAMDISK_BASE;
  for (seL4_CPtr slot = init_data->free_slots.start;
       slot < init_data->free_slots.start + 128; slot++) {
    error =
        seL4_RISCV_Page_Map(slot, init_data->page_directory, vaddr,
                            seL4_AllRights, seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error, "Failed map pages %d", error);
    vaddr += (1u << seL4_LargePageBits);
  }

  while (1) {
    int ret = 0, blockno, label = 0;
#ifdef TEST_NORMAL
    seL4_MessageInfo_t info = seL4_Recv(client_ep, NULL);
    switch (seL4_MessageInfo_get_label(info)) {
    case DISK_INIT:
      printf("[ramdisk] initialize xv6fs \n");
      break;
    case DISK_READ:
      blockno = seL4_GetMR(0);
      // printf("[ramdisk] read %d\n", blockno);
      memmove(init_data->client_buf, (void *)RAMDISK_BASE + blockno * BSIZE,
              BSIZE);
      break;
    case DISK_WRITE:
      blockno = seL4_GetMR(0);
      // printf("[ramdisk] write %d\n", blockno);
      memmove((void *)RAMDISK_BASE + blockno * BSIZE, init_data->client_buf,
              BSIZE);
      break;
    default:
      ret = -EINVAL;
      ZF_LOGE("FS call unimplemented!");
      break;
    }
    seL4_SetMR(0, ret);
    seL4_Reply(info);
#elif defined(TEST_POLL)
    seL4_Word *buf;
    acquire(init_data->client_lk);
    argint(-1, &label);
    if (!label) {
      release(init_data->client_lk);
      continue;
    }
    switch (label) {
    case DISK_INIT:
      printf("[ramdisk] initialize xv6fs \n");
      break;
    case DISK_READ:
      buf = init_data->client_buf;
      blockno = buf[1];
      // printf("[ramdisk] read %d\n", blockno);
      memmove(&buf[2], (void *)RAMDISK_BASE + blockno * BSIZE,
              BSIZE);
      break;
    case DISK_WRITE:
      buf = init_data->client_buf;
      blockno = buf[1];
      // printf("[ramdisk] write %d\n", blockno);
      memmove((void *)RAMDISK_BASE + blockno * BSIZE, &buf[2],
              BSIZE);
      break;
    default:
      ret = -EINVAL;
      break;
    }
    buf[0] = 0;
    buf[1] = ret;
    release(init_data->client_lk);
#endif
  }

  return 0;
}