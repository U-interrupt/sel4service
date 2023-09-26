# sel4service

This porject is for building test environment and running real-world applications on seL4.

## Plans

- [x] Run filesystem server and ramdisk server to support sqlite3.
- [x] Compare different inter-core communication and synchronization methods on QEMU.
    - [x] seL4 IPC (sync + opcode) + shared memory (data)
    - [x] spinlock (sync) + shared memory (opcode + data)
    - [x] seL4 uintr (sync) + shared memory (opcode + data)
- [x] Do all above on FPGA.

## Results

（Rocket Chip，num = 1000，key = 16B，value = 100B）

|               | seL4 IPC (micros/op) | poll lock (micros/op) | poll uintc (micros/op) |
| ------------- | -------------------- | --------------------- | ---------------------- |
| fillseq       | 32611.048            | 27411.044             | 27029.828              |
| fillseqsync   | 9586.000             | 4108.500              | 3709.400               |
| fillseqbatch  | 1037.084             | 795.634               | 776.934                |
| fillrandom    | 39442.468            | 33608.046             | 33229.776              |
| fillrandsync  | 9593.800             | 4157.000              | 3782.500               |
| fillrandbatch | 1116.557             | 904.425               | 883.866                |
| readseq       | 407.964              | 418.905               | 415.755                |
| readrandom    | 410.304              | 421.408               | 418.362                |

（QEMU，num = 1000，key = 16B，value = 100B）

|               | seL4 IPC (micros/op) | poll lock (micros/op) | poll uintc (micros/op) |
| ------------- | -------------------- | --------------------- | ---------------------- |
| fillseq       | 3877.617             | 851.981               | 769.621                |
| fillseqsync   | 3855.800             | 790.400               | 743.100                |
| fillseqbatch  | 182.400              | 67.958                | 61.189                 |
| fillrandom    | 4154.966             | 1039.253              | 939.349                |
| fillrandsync  | 4000.100             | 858.200               | 844.700                |
| fillrandbatch | 180.446              | 73.998                | 59.294                 |
| readseq       | 29.088               | 29.359                | 27.027                 |
| readrandom    | 31.240               | 36.844                | 29.962                 |

