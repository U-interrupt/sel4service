#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <allocman/bootstrap.h>
#include <allocman/vka.h>

#include <cpio/cpio.h>

#include <sel4/sel4.h>

#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/io.h>
#include <sel4platsupport/platsupport.h>

#include <sel4runtime.h>

#include <sel4utils/stack.h>

#include <simple-default/simple-default.h>
#include <simple/simple.h>

#include <vspace/vspace.h>

#include <vka/capops.h>
#include <vka/object.h>
#include <vka/vka.h>

#include <service/env.h>

/* Environment encapsulating allocation interfaces etc */
struct root_env env;

/* dimensions of virtual memory for the allocator to use */
#define ALLOCATOR_VIRTUAL_POOL_SIZE ((1 << seL4_PageBits) * 100)

/* static memory for the allocator to bootstrap with */
#define ALLOCATOR_STATIC_POOL_SIZE ((1 << seL4_PageBits) * 20)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];

/* static memory for virtual memory bootstrapping */
static sel4utils_alloc_data_t data;

extern char _cpio_archive[];
extern char _cpio_archive_end[];

#define CSPACE_SIZE_BITS 17

#define MAX_ARG_NUM 10

int untypedList_allocated
    [CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS]; /* information about each untyped */

/* Initialise our runtime environment */
static void init_env(root_env_t env) {
  int error;
  allocman_t *allocman;
  reservation_t virtual_reservation;

  /* create an allocator */
  allocman = bootstrap_use_current_simple(
      &env->simple, ALLOCATOR_STATIC_POOL_SIZE, allocator_mem_pool);
  if (allocman == NULL) {
    ZF_LOGF("Failed to create allocman");
  }

  /* create a vka (interface for interacting with the underlying allocator) */
  allocman_make_vka(&env->vka, allocman);

  /* create a vspace (virtual memory management interface). We pass
   * boot info not because it will use capabilities from it, but so
   * it knows the address and will add it as a reserved region */
  error = sel4utils_bootstrap_vspace_with_bootinfo_leaky(
      &env->vspace, &data, simple_get_pd(&env->simple), &env->vka,
      platsupport_get_bootinfo());
  if (error) {
    ZF_LOGF("Failed to bootstrap vspace");
  }

  /* fill the allocator with virtual memory */
  void *vaddr;
  virtual_reservation = vspace_reserve_range(
      &env->vspace, ALLOCATOR_VIRTUAL_POOL_SIZE, seL4_AllRights, 1, &vaddr);
  if (virtual_reservation.res == 0) {
    ZF_LOGF("Failed to provide virtual memory for allocator");
  }

  bootstrap_configure_virtual_pool(allocman, vaddr, ALLOCATOR_VIRTUAL_POOL_SIZE,
                                   simple_get_pd(&env->simple));

  error = sel4platsupport_new_io_ops(&env->vspace, &env->vka, &env->simple,
                                     &env->ops);
  ZF_LOGF_IF(error, "Failed to initialise IO ops");
}

/* Start a new process running client */
static void config_app(root_env_t env, struct proc_t *app,
                       const char *image_name, int affinity) {
  int error;
  sel4utils_process_config_t config;

  config =
      process_config_default_simple(&env->simple, image_name, seL4_MaxPrio - 1);
  config = process_config_mcp(config, seL4_MaxPrio);
  config = process_config_auth(config, simple_get_tcb(&env->simple));
  config = process_config_create_cnode(config, CSPACE_SIZE_BITS);
  config.sched_params.core = (seL4_Word)affinity;
  error = sel4utils_configure_process_custom(&app->proc, &env->vka,
                                             &env->vspace, config);
  ZF_LOGF_IF(error, "Failed to config process %s!", image_name);

  error = sel4utils_set_sched_affinity(&app->proc.thread, config.sched_params);
  ZF_LOGF_IF(error, "Failed to set process affinity to %d", affinity);

  /* create a frame that will act as the init data, we can then map that
   * in to target processes */
  app->init = (init_data_t)vspace_new_pages(&env->vspace, seL4_AllRights, 1,
                                            PAGE_BITS_4K);
  assert(app->init != NULL);
  app->init_vaddr = vspace_share_mem(&env->vspace, &app->proc.vspace, app->init,
                                     1, PAGE_BITS_4K, seL4_AllRights, 1);

  /* setup caps */
  app->init->stack_pages = CONFIG_SEL4UTILS_STACK_SIZE / PAGE_SIZE_4K;
  app->init->stack = app->proc.thread.stack_top - CONFIG_SEL4UTILS_STACK_SIZE;
  app->init->page_directory =
      sel4utils_copy_cap_to_process(&app->proc, &env->vka, app->proc.pd.cptr);
  app->init->root_cnode = SEL4UTILS_CNODE_SLOT;
  app->init->tcb = sel4utils_copy_cap_to_process(&app->proc, &env->vka,
                                                 app->proc.thread.tcb.cptr);
  app->init->cspace_size_bits = CSPACE_SIZE_BITS;
  app->init->free_slots.start = app->init->tcb + 3; // tcb, client_ep, server_ep
  app->init->free_slots.end = (1u << CSPACE_SIZE_BITS);
  app->init->magic = 0xdeadbeef;
}

static seL4_CPtr alloc_untyped(root_env_t env, struct proc_t *app,
                               seL4_Word size_bits) {
  seL4_BootInfo *info = platsupport_get_bootinfo();
  for (seL4_CPtr slot = info->untyped.start; slot != info->untyped.end;
       slot++) {
    seL4_UntypedDesc *desc = &info->untypedList[slot - info->untyped.start];
    if (!desc->isDevice && desc->sizeBits == size_bits &&
        !untypedList_allocated[slot - info->untyped.start]) {
      untypedList_allocated[slot - info->untyped.start] = 1;
      return sel4utils_copy_cap_to_process(&app->proc, &env->vka, slot);
    }
  }
  return seL4_CapNull;
}

void *main_continued(void *arg UNUSED) {
  int cores;
  char *argv[10];
  char string_args[10][WORD_STRING_SIZE];

  printf("\n");
  printf("sel4service rootserver\n");
  printf("======================\n");
  printf("\n");

  cores = simple_get_core_count(&env.simple);
  printf("Run on %d cores\n", cores);

  config_app(&env, &env.ramdisk, "ramdisk", 1);
  config_app(&env, &env.fs, "xv6fs", 2);
  config_app(&env, &env.app, "sqlite3", 3);

  env.app_fs_buf =
      vspace_new_pages(&env.vspace, seL4_AllRights, 2, CUSTOM_IPC_BUFFER_BITS);
  env.app.init->client_buf = NULL;
  env.app.init->server_buf =
      vspace_share_mem(&env.vspace, &env.app.proc.vspace, env.app_fs_buf, 2,
                       CUSTOM_IPC_BUFFER_BITS, seL4_AllRights, 1);
  env.fs.init->client_buf =
      vspace_share_mem(&env.vspace, &env.fs.proc.vspace, env.app_fs_buf, 2,
                       CUSTOM_IPC_BUFFER_BITS, seL4_AllRights, 1);

  env.fs_ram_buf =
      vspace_new_pages(&env.vspace, seL4_AllRights, 2, CUSTOM_IPC_BUFFER_BITS);
  env.fs.init->server_buf =
      vspace_share_mem(&env.vspace, &env.fs.proc.vspace, env.fs_ram_buf, 2,
                       CUSTOM_IPC_BUFFER_BITS, seL4_AllRights, 1);
  env.ramdisk.init->client_buf =
      vspace_share_mem(&env.vspace, &env.ramdisk.proc.vspace, env.fs_ram_buf, 2,
                       CUSTOM_IPC_BUFFER_BITS, seL4_AllRights, 1);
  env.ramdisk.init->server_buf = NULL;

#ifdef TEST_NORMAL
  vka_alloc_endpoint(&env.vka, &env.app_fs_ep);
  vka_alloc_endpoint(&env.vka, &env.fs_ram_ep);

  env.ramdisk.init->client_ep = sel4utils_copy_cap_to_process(
      &env.ramdisk.proc, &env.vka, env.fs_ram_ep.cptr);
  env.ramdisk.init->server_ep = seL4_CapNull;
  env.fs.init->client_ep =
      sel4utils_copy_cap_to_process(&env.fs.proc, &env.vka, env.app_fs_ep.cptr);
  env.fs.init->server_ep =
      sel4utils_copy_cap_to_process(&env.fs.proc, &env.vka, env.fs_ram_ep.cptr);
  env.app.init->client_ep = seL4_CapNull;
  env.app.init->server_ep = sel4utils_copy_cap_to_process(
      &env.app.proc, &env.vka, env.app_fs_ep.cptr);
#elif defined(TEST_POLL)
  /* shared spinlock at the start of app_fs_buf */
  env.app.init->client_lk = NULL;
  env.app.init->server_lk = (spinlock_t)env.app.init->server_buf;
  env.fs.init->client_lk = (spinlock_t)env.fs.init->client_buf;
  env.app.init->server_buf += sizeof(struct spinlock);
  env.fs.init->client_buf += sizeof(struct spinlock);
  initlock(env.app.init->server_lk);

  /* shared spinlock at the start of fs_ram_buf */
  env.fs.init->server_lk = (spinlock_t)env.fs.init->server_buf;
  env.ramdisk.init->client_lk = (spinlock_t)env.ramdisk.init->client_buf;
  env.ramdisk.init->server_lk = NULL;
  env.fs.init->server_buf += sizeof(struct spinlock);
  env.ramdisk.init->client_buf += sizeof(struct spinlock);
  initlock(env.fs.init->server_lk);
#elif defined(TEST_UINTR)
  vka_alloc_object(&env.vka, seL4_RISCV_UintrObject, seL4_UintrBits,
                   &env.app_uintr);
  vka_alloc_object(&env.vka, seL4_RISCV_UintrObject, seL4_UintrBits,
                   &env.fs_uintr);
  vka_alloc_object(&env.vka, seL4_RISCV_UintrObject, seL4_UintrBits,
                   &env.ram_uintr);

  /* ramdisk->fs: badge = 1 << 1; uintr -> fs: badge = 1 << 0 */
  cspacepath_t uintr_path, badged_uintr;
  vka_cspace_make_path(&env.vka, env.fs_uintr.cptr, &uintr_path);
  vka_cspace_alloc_path(&env.vka, &badged_uintr);
  vka_cnode_mint(&badged_uintr, &uintr_path, seL4_AllRights, 1);
  env.ramdisk.init->client_uintr = sel4utils_copy_cap_to_process(
      &env.ramdisk.proc, &env.vka, badged_uintr.capPtr);
  env.ramdisk.init->server_uintr = seL4_CapNull;
  env.fs.init->client_uintr =
      sel4utils_copy_cap_to_process(&env.fs.proc, &env.vka, env.app_uintr.cptr);
  env.fs.init->server_uintr =
      sel4utils_copy_cap_to_process(&env.fs.proc, &env.vka, env.ram_uintr.cptr);
  env.app.init->client_uintr = seL4_CapNull;
  env.app.init->server_uintr =
      sel4utils_copy_cap_to_process(&env.app.proc, &env.vka, env.fs_uintr.cptr);

  seL4_TCB_BindUintr(sel4utils_get_tcb(&env.app.proc.thread),
                     env.app_uintr.cptr);
  seL4_TCB_BindUintr(sel4utils_get_tcb(&env.fs.proc.thread), env.fs_uintr.cptr);
  seL4_TCB_BindUintr(sel4utils_get_tcb(&env.ramdisk.proc.thread),
                     env.ram_uintr.cptr);
#endif

  /* ramdisk can use 256 MB physical memory */
  seL4_CPtr ramdisk = alloc_untyped(&env, &env.ramdisk, 25);
  env.ramdisk.init->free_slots.start++;

  argv[0] = "./ramdisk";
  sel4utils_create_word_args(string_args, &argv[1], 2, env.ramdisk.init_vaddr,
                             ramdisk);
  sel4utils_spawn_process_v(&env.ramdisk.proc, &env.vka, &env.vspace, 3, argv,
                            1);

  argv[0] = "./xv6fs";
  sel4utils_create_word_args(string_args, &argv[1], 1, env.fs.init_vaddr);
  sel4utils_spawn_process_v(&env.fs.proc, &env.vka, &env.vspace, 2, argv, 1);


  argv[0] = "./sqlite-bench";
  argv[1] = "--benchmarks=readrandom";
  argv[2] = "--num=1000";
  sel4utils_create_word_args(string_args, &argv[3], 1, env.app.init_vaddr);
  sel4utils_spawn_process_v(&env.app.proc, &env.vka, &env.vspace, 4, argv, 1);

  return 0;
}

/* When the root task exists, it should simply suspend itself */
static void root_exit(int code) {
  // printf("sel4service rootserver suspends\n");
  seL4_TCB_Suspend(seL4_CapInitThreadTCB);
}

int main(void) {
  int error;
  void *res;
  seL4_BootInfo *info;

  /* Set exit handler */
  sel4runtime_set_exit(root_exit);

  info = platsupport_get_bootinfo();

  /* Check SMP */
  assert(info->nodeID == 0);

  /* Initialise libsel4simple, which abstracts away which kernel version we are
   * running on */
  simple_default_init_bootinfo(&env.simple, info);

  /* Initialise the environment - allocator, cspace manager, vspace manager */
  init_env(&env);

  /* Setup serial server */
  platsupport_serial_setup_simple(&env.vspace, &env.simple, &env.vka);

  /* Print bootinfo */
  simple_print(&env.simple);

  /* Switch to a bigger, safer stack with a guard page before starting the tests
   */
  printf("Switching to a safer, bigger stack... \n");
  fflush(stdout);

  /* Run sel4test-test related tests */
  error = sel4utils_run_on_stack(&env.vspace, main_continued, NULL, &res);

  return 0;
}