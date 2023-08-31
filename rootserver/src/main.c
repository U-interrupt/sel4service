#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <allocman/bootstrap.h>
#include <allocman/vka.h>

#include <cpio/cpio.h>

#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/io.h>
#include <sel4platsupport/platsupport.h>

#include <sel4runtime.h>

#include <sel4utils/stack.h>

#include <simple-default/simple-default.h>
#include <simple/simple.h>

#include <vspace/vspace.h>

#include "env.h"
#include "sched.h"
#include "sel4/simple_types.h"
#include "sel4utils/thread.h"

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
static void run_app(root_env_t env, sel4utils_process_t *app, const char *image_name, int argc,
                    char *argv[], int resume, int affinity) {
  int error;
  sel4utils_process_config_t config;

  config =
      process_config_default_simple(&env->simple, image_name, seL4_MaxPrio - 1);
  config = process_config_mcp(config, seL4_MaxPrio);
  config = process_config_auth(config, simple_get_tcb(&env->simple));
  config = process_config_create_cnode(config, CSPACE_SIZE_BITS);
  config.sched_params.core = (seL4_Word)affinity;
  error = sel4utils_configure_process_custom(app, &env->vka, &env->vspace,
                                             config);
  
  ZF_LOGF_IF(error != 0, "Failed to config process %s!", image_name);
  error = sel4utils_spawn_process_v(app, &env->vka, &env->vspace, argc,
                                    argv, resume);
  ZF_LOGF_IF(error != 0, "Failed to start process %s!", image_name);
}

void *main_continued(void *arg UNUSED) {
  char *argv[2];

  printf("\n");
  printf("sel4service rootserver\n");
  printf("======================\n");
  printf("\n");

  argv[0] = "./xv6fs";
  run_app(&env, &env.fs, "xv6fs", 1, argv, 1, 1);

  argv[0] = "./ramdisk";
  run_app(&env, &env.ramdisk, "ramdisk", 1, argv, 1, 2);

  argv[0] = "./sqlite-bench";
  argv[1] = "--benchmarks=readseq";
  run_app(&env, &env.app, "sqlite3", 2, argv, 1, 3);

  return 0;
}

/* When the root task exists, it should simply suspend itself */
static void root_exit(int code) { seL4_TCB_Suspend(seL4_CapInitThreadTCB); }

int main(void) {
  int error;
  void *res;
  seL4_BootInfo *info;

  /* Set exit handler */
  sel4runtime_set_exit(root_exit);

  info = platsupport_get_bootinfo();

  /* Check SMP */
  assert(info->nodeID == 0);
  assert(info->numNodes == 4);

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
  printf("Switching to a safer, bigger stack... ");
  fflush(stdout);

  /* Run sel4test-test related tests */
  error = sel4utils_run_on_stack(&env.vspace, main_continued, NULL, &res);

  return 0;
}