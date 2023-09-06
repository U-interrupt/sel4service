// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <arch_stdio.h>
#include <sel4/sel4.h>
#include <service/env.h>
#include <service/syscall.h>

#include "bench.h"

// Comma-separated list of operations to run in the specified order
//   Actual benchmarks:
//
//   fillseq       -- write N values in sequential key order in async mode
//   fillseqsync   -- write N/100 values in sequential key order in sync mode
//   fillseqbatch  -- batch write N values in sequential key order in async mode
//   fillrandom    -- write N values in random key order in async mode
//   fillrandsync  -- write N/100 values in random key order in sync mode
//   fillrandbatch -- batch write N values in sequential key order in async mode
//   overwrite     -- overwrite N values in random key order in async mode
//   fillrand100K  -- write N/1000 100K values in random order in async mode
//   fillseq100K   -- write N/1000 100K values in sequential order in async mode
//   readseq       -- read N times sequentially
//   readrandom    -- read N times in random order
//   readrand100K  -- read N/1000 100K values in sequential order in async mode
char *FLAGS_benchmarks;

// Number of key/values to place in database
int FLAGS_num;

// Number of read operations to do.  If negative, do FLAGS_num reads.
int FLAGS_reads;

// Size of each value
int FLAGS_value_size;

// Print histogram of operation timings
bool FLAGS_histogram;

// Print raw data
bool FLAGS_raw;

// Arrange to generate values that shrink to this fraction of
// their original size after compression
double FLAGS_compression_ratio;

// Page size. Default 1 KB.
int FLAGS_page_size;

// Number of pages.
// Default cache size = FLAGS_page_size * FLAGS_num_pages = 4 MB.
int FLAGS_num_pages;

// If true, do not destroy the existing database.  If you set this
// flag and also specify a benchmark that wants a fresh database, that
// benchmark will fail.
bool FLAGS_use_existing_db;

// If true, we allow batch writes to occur
bool FLAGS_transaction;

// If true, we enable Write-Ahead Logging
bool FLAGS_WAL_enabled;

// Use the db with the following name.
char *FLAGS_db;

void init() {
  // Comma-separated list of operations to run in the specified order
  //   Actual benchmarks:
  //
  //   fillseq       -- write N values in sequential key order in async mode
  //   fillseqsync   -- write N/100 values in sequential key order in sync mode
  //   fillseqbatch  -- batch write N values in sequential key order in async
  //   mode fillrandom    -- write N values in random key order in async mode
  //   fillrandsync  -- write N/100 values in random key order in sync mode
  //   fillrandbatch -- batch write N values in sequential key order in async
  //   mode overwrite     -- overwrite N values in random key order in async
  //   mode fillrand100K  -- write N/1000 100K values in random order in async
  //   mode fillseq100K   -- write N/1000 100K values in sequential order in
  //   async mode readseq       -- read N times sequentially readrandom    --
  //   read N times in random order readrand100K  -- read N/1000 100K values in
  //   sequential order in async mode
  FLAGS_benchmarks = "fillseq,"
                     "fillseqsync,"
                     "fillseqbatch,"
                     "fillrandom,"
                     "fillrandsync,"
                     "fillrandbatch,"
                     "overwrite,"
                     "overwritebatch,"
                     "readrandom,"
                     "readseq,"
                     "fillrand100K,"
                     "fillseq100K,"
                     "readseq,"
                     "readrand100K,";
  FLAGS_num = 1000000;
  FLAGS_reads = -1;
  FLAGS_value_size = 100;
  FLAGS_histogram = false;
  FLAGS_raw = false, FLAGS_compression_ratio = 0.5;
  FLAGS_page_size = 1024;
  FLAGS_num_pages = 4096;
  FLAGS_use_existing_db = false;
  FLAGS_transaction = true;
  FLAGS_WAL_enabled = true;
  FLAGS_db = NULL;
}

void print_usage(const char *argv0) {
  fprintf(stderr, "Usage: %s [OPTION]...\n", argv0);
  fprintf(stderr, "SQLite3 benchmark tool\n");
  fprintf(stderr, "[OPTION]\n");
  fprintf(stderr, "  --benchmarks=[BENCH]\t\tspecify benchmark\n");
  fprintf(stderr, "  --histogram={0,1}\t\trecord histogram\n");
  fprintf(stderr, "  --raw={0,1}\t\t\toutput raw data\n");
  fprintf(stderr, "  --compression_ratio=DOUBLE\tcompression ratio\n");
  fprintf(stderr, "  --use_existing_db={0,1}\tuse existing database\n");
  fprintf(stderr, "  --num=INT\t\t\tnumber of entries\n");
  fprintf(stderr, "  --reads=INT\t\t\tnumber of reads\n");
  fprintf(stderr, "  --value_size=INT\t\tvalue size\n");
  fprintf(stderr, "  --no_transaction\t\tdisable transaction\n");
  fprintf(stderr, "  --page_size=INT\t\tpage size\n");
  fprintf(stderr, "  --num_pages=INT\t\tnumber of pages\n");
  fprintf(stderr, "  --WAL_enabled={0,1}\t\tenable WAL\n");
  fprintf(stderr, "  --db=PATH\t\t\tpath to location databases are created\n");
  fprintf(stderr, "  --help\t\t\tshow this help\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "[BENCH]\n");
  fprintf(stderr,
          "  fillseq\twrite N values in sequential key order in async mode\n");
  fprintf(stderr, "  fillseqsync\twrite N/100 values in sequential key order "
                  "in sync mode\n");
  fprintf(stderr, "  fillseqbatch\tbatch write N values in sequential key "
                  "order in async mode\n");
  fprintf(stderr,
          "  fillrandom\twrite N values in random key order in async mode\n");
  fprintf(
      stderr,
      "  fillrandsync\twrite N/100 values in random key order in sync mode\n");
  fprintf(stderr, "  fillrandbatch\tbatch write N values in random key order "
                  "in async mode\n");
  fprintf(
      stderr,
      "  overwrite\toverwrite N values in random key order in async mode\n");
  fprintf(stderr, "  fillrand100K\twrite N/1000 100K values in random order in "
                  "async mode\n");
  fprintf(stderr, "  fillseq100K\twirte N/1000 100K values in sequential order "
                  "in async mode\n");
  fprintf(stderr, "  readseq\tread N times sequentially\n");
  fprintf(stderr, "  readrandom\tread N times in random order\n");
  fprintf(stderr, "  readrand100K\tread N/1000 100K values in sequential order "
                  "in async mode\n");
}

static seL4_CPtr server_ep;
static init_data_t init_data;

/* start at a virtual address below KERNEL_RESERVED_START */
/* this address is a hack to vspace, do not change it !*/
#define MEM_BASE 0x20000000

int main(int argc, char **argv) {
  /* =========================== */

  /* parse args */
  init_data = (void *)atol(argv[argc - 2]);
  assert(init_data->magic == 0xdeadbeef);

  seL4_CPtr ram = (seL4_Word)atol(argv[argc - 1]);
  int error = seL4_Untyped_Retype(ram, seL4_RISCV_Mega_Page, seL4_LargePageBits,
                                  init_data->root_cnode, 0, 0,
                                  init_data->free_slots.start, 2);
  ZF_LOGF_IF(error, "Failed to allocate frames");
  seL4_Word vaddr = MEM_BASE;
  for (seL4_CPtr slot = init_data->free_slots.start;
       slot < init_data->free_slots.start + 2; slot++) {
    error =
        seL4_RISCV_Page_Map(slot, init_data->page_directory, vaddr,
                            seL4_AllRights, seL4_RISCV_Default_VMAttributes);
    ZF_LOGF_IF(error, "Failed map pages %d", error);
    vaddr += (1u << seL4_LargePageBits);
  }

  init_syscall_table(init_data->server_ep, init_data);

  /* =========================== */

  init();

  char *default_db_path = malloc(sizeof(char) * 1024);
  strcpy(default_db_path, "./");

  for (int i = 1; i < argc - 2; i++) {
    double d;
    int n;
    char junk;
    if (starts_with(argv[i], "--benchmarks=")) {
      FLAGS_benchmarks = argv[i] + strlen("--benchmarks=");
    } else if (sscanf(argv[i], "--histogram=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_histogram = n;
    } else if (sscanf(argv[i], "--raw=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_raw = n;
    } else if (sscanf(argv[i], "--compression_ratio=%lf%c", &d, &junk) == 1) {
      FLAGS_compression_ratio = d;
    } else if (sscanf(argv[i], "--use_existing_db=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_use_existing_db = n;
    } else if (sscanf(argv[i], "--num=%d%c", &n, &junk) == 1) {
      FLAGS_num = n;
    } else if (sscanf(argv[i], "--reads=%d%c", &n, &junk) == 1) {
      FLAGS_reads = n;
    } else if (sscanf(argv[i], "--value_size=%d%c", &n, &junk) == 1) {
      FLAGS_value_size = n;
    } else if (!strcmp(argv[i], "--no_transaction")) {
      FLAGS_transaction = false;
    } else if (sscanf(argv[i], "--page_size=%d%c", &n, &junk) == 1) {
      FLAGS_page_size = n;
    } else if (sscanf(argv[i], "--num_pages=%d%c", &n, &junk) == 1) {
      FLAGS_num_pages = n;
    } else if (sscanf(argv[i], "--WAL_enabled=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_WAL_enabled = n;
    } else if (strncmp(argv[i], "--db=", 5) == 0) {
      FLAGS_db = argv[i] + 5;
    } else if (!strcmp(argv[i], "--help")) {
      print_usage(argv[0]);
      exit(0);
    } else {
      fprintf(stderr, "Invalid flag '%s'\n", argv[i]);
      exit(1);
    }
  }

  /* Choose a location for the test database if none given with --db=<path>  */
  if (FLAGS_db == NULL)
    FLAGS_db = default_db_path;

  benchmark_init();
  benchmark_run();
  benchmark_fini();

  return 0;
}
