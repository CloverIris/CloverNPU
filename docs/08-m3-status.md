# 08 · M3 状态：存储、AXI、DMA 与命令队列

**状态：M3b 已完成（2026-10-18）**。主机可以通过 AXI4-Lite 配置 CSR、写 queue tail 触发 doorbell；NPU 从 AXI 内存取 64 B 命令、驱动 2D DMA、写 completion、拉 IRQ。5 级核也可以直接从 scratchpad 取指/读写。

## 1. 已实现

| 模块 | 文件 | 能力 |
|---|---|---|
| Scratchpad | `rtl/mem/clover_scratchpad.sv` | 256 KB，16 bank × 4096 word；2 个 64 B 读端口 + 1 个 64 B 写端口；64-bit byte enable |
| AXI4-Lite slave | `rtl/bus/clover_axi4lite_slave.sv` | 32-bit；AW/W 独立握手；B/R 响应；4 KiB CSR 窗口 |
| Global CSR | `rtl/top/clover_csr.sv` | ID/VERSION/STATUS/CONTROL/IRQ/QUEUE/COMPLETE/CYCLE/ERROR/SCRATCH/CAP；queue tail doorbell |
| AXI-CSR top | `rtl/top/clover_axi_csr.sv` | slave + CSR 集成 |
| Core+mem top | `rtl/top/clover_core_mem.sv` | 核从 scratchpad 取指/读写；64 B 行 → 32-bit lane 适配；loader 端口 |
| AXI4 DMA | `rtl/dma/clover_dma.sv` | 2D copy，DRAM ↔ scratchpad，16 B 单拍 AXI，行/stride |
| Command processor | `rtl/cmd/clover_cmd_proc.sv` | 64 B 命令 ring、doorbell、DMA、completion、IRQ、abort |
| NPU memory top | `rtl/top/clover_npu_mem.sv` | AXI4-Lite + CSR + CP + DMA + scratchpad + AXI4 master mux |
| 测试 | `tests/rtl/run_mem_tests.py`、`run_core_mem_tests.py` | scratchpad、AXI-CSR、DMA、CP、NPU end-to-end、core-from-scratchpad |

## 2. Scratchpad 接口

- 字节地址 `[17:0]`，64 B 端口忽略低 6 位。
- 端口 A/B：`req/addr -> rdata`，组合读（M3a 简化；后续可加 1 周期寄存器）。
- 写端口：`req/addr/data/be -> ready`。
- 16 个 bank 每个 16 KiB；一条 64 B 行由 16 个 bank 各出一个 32-bit word。
- 两个读端口可以同拍读不同行。

## 3. Core+scratchpad 适配

- `RESET_PC = 0`，代码/数据/栈都在 scratchpad（栈顶 `0x0003FFF0`）。
- 取指：`a_addr = {pc[17:6],6'b0}`，按 `pc[5:2]` 选 lane。
- 数据读：`b_addr = {dmem_addr[17:6],6'b0}`，按 `dmem_addr[5:2]` 选 lane。
- 数据写：按 `dmem_size/addr[1:0]` 生成 lane 内 byte enable，并左移数据到对应 lane。
- loader 端口用于测试/未来主机初始化：64 B 行写，`loader_we` 优先于核写。

## 4. M3b：AXI4 master、DMA、命令队列

- `clover_dma`：单拍 16 B AXI4 读写；2D 行/stride；源/目的/stride 必须 16 B 对齐，`bytes_per_row` 必须 16 B 倍数；错误描述符置 `error` 并停机。
- `clover_cmd_proc`：
  - 从 `queue_base + head*64` 取 4 个 16 B beat；
  - 支持 `NOP`、`DMA_2D`、`SIGNAL`、`END`；未知命令写错误 completion；
  - 成功 DMA 不写 completion；`SIGNAL`/`END`/错误写 16 B completion；
  - completion 后拉 IRQ；`queue_head`/`completion_head` 按 size 回绕。
- `clover_npu_mem`：AXI4-Lite CSR + CP + DMA + scratchpad；CP 与 DMA 共享 AXI4 master，CP 活动时优先。
- 主机路径：AXI-Lite 写 `CONTROL/IRQ_ENABLE/QUEUE_*/COMPLETE_*` → 写 `QUEUE_TAIL` doorbell → NPU 取命令/搬数据/写 completion → `IRQ_STATUS` 可读可清。

## 5. 验证

```bash
make rtl-mem-lint
make rtl-mem-synth
make rtl-mem-test
```

- Scratchpad：全 64 B 写/读、byte enable、两行并行读、最后一行 `0x3FFC0`。
- AXI-CSR：ID/VERSION/STATUS、CONTROL、QUEUE_BASE/SIZE/TAIL、doorbell、IRQ RW1C、RO 寄存器、byte strobe、abort/soft-reset pulse。
- DMA：2D 行/stride 往返、目标数据逐 16 B 比对、错误对齐被拒绝。
- Command processor：4 条命令（DMA→DMA→SIGNAL→END），completion ring 2 项（seq 1/2），IRQ 至少 2 次，queue/completion head 正确。
- NPU memory top：AXI-Lite 配置 + doorbell + DMA + completion + IRQ 全链路，数据与 completion 逐字比对。
- Core-from-scratchpad：`add`、`fib`、`hello`、`array`、`ptrs`、`bigarray` 六个 C 程序全部通过。
- Yosys 对 scratchpad、AXI-CSR、DMA、CP、NPU top、core+mem 均 `check -assert` 通过。

## 6. 已知限制

- AXI4 master 目前只发单拍 16 B INCR；没有 burst 合并。
- DMA 要求源/目的/stride 16 B 对齐、`bytes_per_row` 为 16 B 倍数；不做非对齐拆分。
- Scratchpad 读是组合逻辑，不是同步 SRAM；没有 bank 冲突检测。
- 没有 I-cache/D-cache；没有 AXI protocol checker / 随机 backpressure 回归。
- `clover_npu_mem` 还没有接 5 级核；core 目前通过 `clover_core_mem` 独立验证。
- 没有 `RUN` 命令；DMA/命令队列先打通，kernel launch 在 M4/M5。
- CSR 的 `STATUS` 只反映 CP/DMA busy；错误码/计数仍是最小实现。

## 7. 下一步

1. 把 5 级核接入 `clover_npu_mem`，增加 `RUN/WAIT` 命令。
2. Tile engine（M4）：MMA/VEC/CVT/REDUCE + 描述符。
3. AXI protocol checker、随机 backpressure、DMA burst。
4. `libclover` 仿真后端与 host → DMA → core 的 C 程序端到端。
