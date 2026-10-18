# 08 · M3a 状态：scratchpad + AXI4-Lite/CSR

**状态：M3a 已完成（2026-10-18）**。存储与 CSR 底座已经可综合、可单测，并且 5 级核可以直接从 scratchpad 取指/读写。DMA、AXI4 master、命令队列与 completion ring 尚未实现。

## 1. 已实现

| 模块 | 文件 | 能力 |
|---|---|---|
| Scratchpad | `rtl/mem/clover_scratchpad.sv` | 256 KB，16 bank × 4096 word；2 个 64 B 读端口 + 1 个 64 B 写端口；64-bit byte enable |
| AXI4-Lite slave | `rtl/bus/clover_axi4lite_slave.sv` | 32-bit；AW/W 独立握手；B/R 响应；4 KiB CSR 窗口 |
| Global CSR | `rtl/top/clover_csr.sv` | ID/VERSION/STATUS/CONTROL/IRQ/QUEUE/COMPLETE/CYCLE/ERROR/SCRATCH/CAP；queue tail doorbell |
| AXI-CSR top | `rtl/top/clover_axi_csr.sv` | slave + CSR 集成 |
| Core+mem top | `rtl/top/clover_core_mem.sv` | 核从 scratchpad 取指/读写；64 B 行 → 32-bit lane 适配；loader 端口 |
| 测试 | `tests/rtl/run_mem_tests.py`、`run_core_mem_tests.py` | scratchpad、AXI-CSR、core-from-scratchpad |

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

## 4. 验证

```bash
make rtl-mem-lint
make rtl-mem-synth
make rtl-mem-test
```

- Scratchpad：全 64 B 写/读、byte enable、两行并行读、最后一行 `0x3FFC0`。
- AXI-CSR：ID/VERSION/STATUS、CONTROL、QUEUE_BASE/SIZE/TAIL、doorbell、IRQ RW1C、RO 寄存器、byte strobe、abort/soft-reset pulse。
- Core-from-scratchpad：`add`、`fib`、`hello`、`array`、`ptrs`、`bigarray` 六个 C 程序全部通过（退出码/串口一致）。
- Yosys 对 scratchpad、AXI-CSR、core+mem 三个 top 均 `check -assert` 通过。

## 5. 已知限制

- Scratchpad 读是组合逻辑，不是同步 SRAM；M3b 可加 1 周期读延迟。
- 没有 bank 冲突检测；当前假设 64 B 对齐访问。
- 没有 I-cache/D-cache、AXI4 master、DMA、命令队列、completion ring。
- CSR 的 `STATUS` 只反映 `status_busy` 输入；`QUEUE_HEAD/COMPLETE_HEAD` 固定 0。
- AXI4-Lite 没有地址越界检查；窗口由上层解码。
- 没有 AXI protocol checker / 随机 backpressure 回归。

## 6. 下一步（M3b）

1. AXI4 master（128-bit，INCR burst，读写通道）。
2. DMA 2D（DRAM ↔ scratchpad，行/stride）。
3. Command processor：ring fetch、doorbell、completion、IRQ、abort。
4. Core + DMA + CP 的 `clover_npu` 顶层。
5. `libclover` 仿真后端与一条 host → DMA → scratchpad → core 的端到端测试。
