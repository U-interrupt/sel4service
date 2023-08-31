#pragma once

#include <autoconf.h>
#include <sel4/bootinfo.h>
#include <sel4utils/process.h>
#include <simple/simple.h>
#include <vka/object.h>
#include <vka/vka.h>
#include <vspace/vspace.h>

struct root_env {
  /* An initialised vka that may be used by the test. */
  vka_t vka;

  /* virtual memory management interface */
  vspace_t vspace;

  /* abtracts over kernel version and boot environment */
  simple_t simple;

  /* IO ops for devices */
  ps_io_ops_t ops;

  /* Target client using POSIX syscalls */
  sel4utils_process_t app;

  /* xv6 filesystem server */
  sel4utils_process_t fs;

  /* RAM Disk device driver */
  sel4utils_process_t ramdisk;
};
typedef struct root_env *root_env_t;
