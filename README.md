# sel4service

This porject is for building test environment and running real-world applications on seL4.

## Plans

- [x] Run filesystem server and ramdisk server to support sqlite3.
- [x] Compare different inter-core communication and synchronization methods on QEMU.
    - [x] seL4 IPC (sync + opcode) + shared memory (data)
    - [x] spinlock (sync) + shared memory (opcode + data)
    - [x] seL4 uintr (sync) + shared memory (opcode + data)
- [ ] Do all above on FPGA.

