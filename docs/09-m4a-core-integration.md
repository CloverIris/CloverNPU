# 09 · M4a：Core 集成与 RUN 命令

**状态：M4a 已完成（2026-10-18）**。5 级核已接入 `clover_npu_mem`；主机可以通过命令队列的 `RUN` 启动一个 C kernel，kernel 结果由 DMA 搬回 DRAM，再由 `SIGNAL/END` 产生 completion/IRQ。Tile engine 尚未实现。

## 1. 新增能力

| 能力 | 实现 |
|---|---|
| 核 boot 接口 | `core_start/core_entry/core_sp/core_arg/core_active` |
| 启动寄存器 | regfile boot 端口写 `sp`、`a0`、`ra=0` |
| kernel 返回 | `jalr zero, ra, 0` 且 `ra=0` → 核停机，`a0` 为状态 |
| CP `RUN` | 0x03；`src=entry`、`dst=args`、`dim0=stack_top`、`dim1=kernel_id` |
| CP 等待 | `ST_CORE_START` → `ST_CORE_WAIT` → 状态码；非零写错误 completion |
| scratchpad 仲裁 | core active 时 core 拥有端口；否则 DMA 拥有 |

## 2. 端到端路径

```text
host host_write(kernel bytes) -> AXI RAM
AXI-Lite: CONTROL/IRQ_ENABLE/QUEUE_*/COMPLETE_* -> QUEUE_TAIL
CP: fetch 64B command
  DMA_2D: kernel image DRAM 0x10004000 -> scratchpad 0
  RUN:    entry=0, sp=0x3FFF0, a0=0
  core:   kernel runs; writes 55 to scratchpad 0x30000; ret -> exit 0
  DMA_2D: 0x30000 -> DRAM 0x10005000
  SIGNAL/END: completion + IRQ
host: reads DRAM 0x5000 => 55; completion seq 1/2; last_exit_code 0
```

## 3. 测试

```bash
make rtl-npu-test
```

`tests/rtl/run_npu_core_tests.py`：

- 用 `cloverc --no-start --base 0` 编译 `tests/npu/kernel_sum.c`；
- host 把 kernel 写入 DRAM 0x4000；
- 5 条命令：DMA code、RUN、DMA result、SIGNAL、END；
- 检查 `core_active` 曾拉高、`last_exit_code==0`、结果 55、completion seq 1/2、无错误。

同时保留：

- `make rtl-core-test`：19 个 C 程序的 ISS/RTL 差分；
- `make rtl-mem-test`：scratchpad、AXI-CSR、DMA、CP、NPU memory top、6 个 core-from-scratchpad 程序；
- `make check`：以上全部 + 23 个 ISS 用例 + L0 smoke + ABI。

## 4. 已知限制

- Tile engine 尚未实现；kernel 只能做标量整数/指针运算，还不能做 MMA/VEC。
- kernel 参数 ABI 尚未被编译器/库使用；当前 kernel 无参数。
- 编译器没有预处理器/struct/switch/float，kernel 仍是无头文件 C。
- 核与 DMA 共享 scratchpad 端口但假设互斥；没有并发仲裁器。
- 没有 I-cache/D-cache；取指/数据都直接访问 scratchpad。
- CP 只支持 `NOP/DMA_2D/RUN/SIGNAL/END`；`WAIT`/`BARRIER`/错误码仍最小。
- `RUN` 成功不写 completion；错误写 completion。

## 5. 下一步（M4b）

1. `rtl/tile/*`：描述符 fetch/decode、16×16 MMA、16-lane VEC、CVT/REDUCE。
2. `clover_tile.h` 描述符与内建；编译器支持 `struct`/`#include` 以编译 kernel。
3. 在 ISS 中实现 tile 功能模型 + Python golden，先跑通 GEMM。
4. 再把 tile engine 接进 `clover_npu_mem`，用 `TILE.*` 驱动。
5. 主机 `libclover` 的 buffer/graph API。
