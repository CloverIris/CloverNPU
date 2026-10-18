# 13 · RTL GEMM 端到端

**状态：M4c-Integration 已完成（2026-10-18）**。同一个 `kernels/gemm.c` 现在可以在 RTL 上跑完整动态 shape GEMM：

```text
AXI-Lite CSR -> doorbell -> CP queue -> DMA kernel/args/A/B -> scratchpad
  -> RUN -> core tile.mma / tile.wait -> 串行 MMA -> C
  -> DMA C -> DRAM -> SIGNAL/END -> completion + IRQ
```

测试 shape：`M=17, N=19, K=23`，逐元素与整数参考比较通过。

## 1. 集成结构

- `clover_core` 新增：
  - `tile_valid/tile_op/tile_desc/tile_busy`
  - `tile.mma/vec/dma` 在 EX 发描述符；`tile.wait` 在 `tile_busy` 时 stall。
- `clover_tile_mma` 接入 `clover_npu_mem`：
  - 描述符地址在 start 时锁存；
  - tile busy 时 scratchpad 端口优先级：tile > core > DMA；
  - core 在 tile busy 时 `imem_ready=0`、`dmem_ready=0`。
- CP `RUN`：
  - `src=entry`、`dst=args`、`dim0=stack_top`；
  - 等待 `core_halted`；非零状态写错误 completion。
- CP 队列大小 16，8 条命令（4 DMA + RUN + DMA + SIGNAL + END）。
- 结果 DMA 使用 `flags=1`（SP → DRAM）。

## 2. 测试

```bash
make rtl-gemm-test
```

`tests/rtl/run_npu_gemm_tests.py`：

1. `cloverc --no-start --base 0 --entry gemm` 编译 `kernels/gemm.c`，从 map 取 `gemm` 入口。
2. 把 kernel/args/A/B 写入 AXI RAM；
3. 8 条命令搬入 scratchpad、RUN、搬回 C；
4. 等待 queue head 8、completion head 2、CP idle；
5. 检查 `tile_error==0`、kernel exit 0、C 逐元素等于参考。

结果：

```text
PASS npu_rtl_gemm
```

## 3. 调试中修复的真实 bug

1. **scratchpad port A 地址隐式 1-bit**：`sp_a_addr` 漏声明，Verilator 自动建成 1-bit wire，PC 0x40 取到第 0 行指令，导致核递归执行/栈下溢。
2. **kernel-return sentinel 未检查 `rd==0`**：`call min_u` 的 `jalr ra,0(ra)` 目标为 0，被误判成 kernel 返回，GEMM 在第一次 `min_u` 调用时退出（exit=16）。
3. **tile descriptor 地址未锁存**：core 发完 `tile.mma` 后继续执行，`tile_desc` 变成后续指令的 rs1（0x3fff0），tile engine fetch 读到栈，magic 错误。
4. **结果 DMA 方向错误**：缺 `flags=1`，把 SP→DRAM 当成 DRAM→SP，C 搬回为 0。

## 4. 已知限制

- RTL 已支持 MMA 与 FP32 VEC；VEC 的 FP16/BF16/FP8/FP4 路径未实现。
- A/B 支持 FP4–FP16；C 仍为 FP32，低精度 C 输出未实现。
- 次正规输入/输出 flush 到 0；没有完整 IEEE 次正规舍入。
- 串行 1 MAC/cycle：17×19×23 的 8 个 block 约 18 万 cycles（仿真 184 µs）。
- 没有 per-tensor/per-channel scale、post-scale/bias 的 RTL。
- 没有 I/D-cache；没有 AXI protocol checker。
- RTL GEMM 只测了一个 shape；16×16×16 等尚未加入回归。
- 图 runtime 仍走 ISS，不是 RTL 命令队列后端。

## 5. 下一步

1. 把 16×16×16 / 其他 shape 加入 RTL GEMM 回归。
2. FP8/FP4 RTL 解包与 scale。
3. 图 runtime 增加 RTL/Verilator 后端，完成 RTL 图执行。
4. `CLOV`/manifest、编译器 `struct/#include/aligned(64)`。
5. RTL VEC 已完成，见 [RTL 图](14-rtl-graph.md)。
