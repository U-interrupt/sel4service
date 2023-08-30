#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sel4runtime.h>

#include <allocman/bootstrap.h>
#include <allocman/vka.h>

#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/io.h>
#include <sel4platsupport/platsupport.h>

#include <sel4utils/stack.h>

#include <simple-default/simple-default.h>
#include <simple/simple.h>

#include "env.h"
#include "vspace/vspace.h"

/* Environment encapsulating allocation interfaces etc */
struct root_env env;

/* dimensions of virtual memory for the allocator to use */
#define ALLOCATOR_VIRTUAL_POOL_SIZE ((1 << seL4_PageBits) * 100)

/* static memory for the allocator to bootstrap with */
#define ALLOCATOR_STATIC_POOL_SIZE ((1 << seL4_PageBits) * 20)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];

/* static memory for virtual memory bootstrapping */
static sel4utils_alloc_data_t data;

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

void *main_continued(void *arg UNUSED) {
  printf("\n");
  printf("sel4service rootserver\n");
  printf("======================\n");
  printf("\n");
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

  /* Initialise libsel4simple, which abstracts away which kernel version we are
   * running on */
  simple_default_init_bootinfo(&env.simple, info);

  /* Initialise the environment - allocator, cspace manager, vspace manager */
  init_env(&env);

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