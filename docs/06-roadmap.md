# 06 · 路线图

路线图按“每个里程碑都能独立验证”的原则拆分。M2 之后的顺序允许调整，但 **ABI 与描述符布局在 M1 冻结**，后续变更必须走版本升级。

## M0 · 工具链与仓库（已完成）

- Homebrew 安装 Verilator 5.052、Icarus 13.0、Yosys 0.69。
- 项目 `.venv` 安装 cocotb 2.1.0、numpy、pytest。
- 建立目录骨架、`scripts/setup.sh`、`scripts/check_tools.sh`。
- `make smoke` 跑通 Verilator / Icarus / cocotb / Yosys。

**验收**：`make smoke` 退出码 0；`scripts/check_tools.sh` 全部通过。

## M1 · 设计基线与 ABI（已完成）

- 冻结架构、ISA、ABI、编译器、验证规范。
- 冻结描述符/命令/kernel args/CLOV 布局。
- 建立 RTL/编译器/runtime 目录与构建入口。
- 完成工具链 smoke test。
- 提交初始 git 基线。

**验收**：

- 文档中所有结构体大小、偏移、magic 自洽。
- 头文件（M2 建立）与文档逐字段一致。
- 评审通过：没有未决的“会影响布局”的问题。

## M2 · 标量核 + ISS + 汇编器 + 最小 C 编译器

目标：**让一段整数 C 程序在 ISS 上跑出正确结果，再让同一程序在 RTL 核上复现。**

### M2a · 软件垂直切片（已完成，2026-10-18）

- Clover-S ISA 编码/解码/反汇编。
- `clover-as`：两遍汇编器、标签、伪指令、flat image。
- `clover-iss`：32 位顺序执行、CSR、trap、syscall、内存映射、trace。
- `cloverc`：`int/char/short/long`、指针、数组、全局变量、函数/递归、if/while/for/do、break/continue、字符串、内建 syscall。
- 23 个端到端测试（汇编 3 + C 19 + 负例 1），见 [M2 状态](07-m2-status.md)。
- `clover-objdump` 可反汇编并标注符号。

### M2b · RTL 标量核（已完成，2026-10-18）

- `clover_pkg.sv`（工具兼容的 typedef/localparam 头）、regfile、ALU、decoder、LSU、CSR、trap、5 级流水。
- EX/MEM/WB 转发、load-use bubble、WB→ID hazard stall、branch redirect flush。
- `clover_core_tb.sv` 仿真内存模型；19 个 C 程序与 ISS 退出码/串口输出一致。
- Yosys `read_verilog -sv` + `hierarchy` + `proc` + `opt` + `check -assert` 通过。

**验收**：

- 19 个 C 程序（递归、数组、指针、全局变量、6 参数、大栈帧）在 ISS 与 RTL 上一致。
- `make rtl-lint`、`make rtl-synth`、`make rtl-core-test` 全绿。
- 尚未接入 cache/AXI/DMA/命令队列/tile engine；这些进入 M3/M4。
- 随机指令差分（10 万条）留到 M3 前补强。

## M3 · 存储、总线、DMA、命令队列

目标：**主机能通过 AXI-Lite 配置，通过 AXI4 搬数据，NPU 按命令队列执行。**

### M3a · 存储/CSR 底座（已完成，2026-10-18）

- Scratchpad 256 KB、16 bank、2R1W、64 B 端口、byte enable。
- AXI4-Lite slave + 全局 CSR（queue/doorbell/IRQ/completion 寄存器）。
- 5 级核从 scratchpad 取指/读写（64 B 行 → 32-bit lane 适配）。
- 6 个 C 程序通过 core-from-scratchpad 测试；Yosys check 通过。
- 详见 [M3a 状态](08-m3-status.md)。

### M3b · DMA、AXI4 master、命令队列（已完成，2026-10-18）

- AXI4 master（128-bit，单拍 INCR，读/写通道）。
- DMA 2D（DRAM ↔ scratchpad，行/stride，16 B 对齐）。
- Command processor：ring、doorbell、DMA、completion、IRQ、abort。
- `clover_npu_mem`：AXI4-Lite CSR + CP + DMA + scratchpad + AXI4 master。
- Host → AXI-Lite → doorbell → command queue → DMA → completion/IRQ 全链路通过。
- I-cache/D-cache 与 AXI protocol checker 尚未实现，延后到 M3c/M4。

**验收**：

- 2D DMA 在 DRAM/scratchpad 之间往返 bit-exact：通过。
- 命令 ring 的 doorbell/completion/IRQ：通过（4 条命令、2 条 completion）。
- 错误对齐被拒绝并上报：通过。
- AXI 随机 backpressure 与 ring 回绕 1000 条：尚未覆盖。

## M4 · Core 集成 + Tile engine

### M4a · Core 集成 + RUN（已完成，2026-10-18）

- 5 级核 boot：`core_start/entry/sp/arg`，regfile 写 `sp/a0/ra=0`。
- kernel 返回 sentinel：`ret` 到地址 0 → 核停机，`a0` 为状态。
- CP `RUN` 命令：entry/args/stack；等待 `core_halted`；错误写 completion。
- `clover_npu_mem` 接入核；core active 时独占 scratchpad 端口，否则 DMA。
- host → DMA kernel image → RUN → kernel 写结果 → DMA result → SIGNAL/END 通过。
- 详见 [M4a 状态](09-m4a-core-integration.md)。

### M4b · 功能级 Tile Engine + GEMM（已完成，2026-10-18）

- 编译器 tile 内建：`__clover_mma/vec/dma/wait/fence`。
- ISS 功能级 MMA/VEC/DMA，支持 FP16/BF16/FP8/FP4 解包、FP32 累加。
- `kernels/gemm.c` 动态 M/N/K；`16x16x16` 与 `17x19x23` bit-exact。
- 详见 [M4b 状态](10-m4b-tile-software.md)。

### M4c · RTL Tile Engine

#### M4c-MMA（已完成，2026-10-18）

- 可综合 FP32 add/mul（2000 组随机 normal 对比 numpy）。
- 串行 16×16×16 MMA：描述符 fetch、FP16/BF16、accumulate/transpose/ReLU、C FP32。
- 16×16×16、8×8×8、accumulate RTL 测试通过；Yosys check 通过。
- 详见 [RTL Tile MMA](12-rtl-tile-mma.md)。

#### M4c-Integration（已完成，2026-10-18）

- `tile.mma/tile.wait` 接入 core；tile busy 时让出 scratchpad 端口。
- 完整 RTL GEMM：CP 队列 + DMA + core + 串行 MMA + completion/IRQ。
- `M=17,N=19,K=23` 通过；修复 port A 隐式 1-bit、kernel-return rd 检查、descriptor 锁存、结果 DMA 方向四个 bug。
- 详见 [RTL GEMM](13-rtl-gemm.md)。

#### M4d · RTL VEC + 两节点图（已完成，2026-10-18）

- FP32 VEC ADD/MUL/RELU；256 lanes 独立测试通过。
- MMA/VEC 接入 NPU；GEMM → VEC ADD 两节点图 17×19×23 通过。
- 详见 [RTL 图](14-rtl-graph.md)。

#### M4e · 多 shape、FP8/FP4 与 RTL 图后端（下一步）

目标：**描述符驱动的 MMA/VEC/CVT/REDUCE 在 RTL 上正确。**

- FP32 加法/乘法/比较、FP16/BF16/FP8/FP4 解包/打包。
- 16x16 MAC 阵列，M/N/K <= 16，转置、accumulate、scale、post-op。
- 16 lane 向量引擎，所有 VEC op，stride/broadcast，reduce 树。
- Tile 描述符 fetch/decode/validate、错误码、busy/计数。
- 与 Python golden bit-exact。

**验收**：

- 所有 dtype 的 MMA 边界用例（M/N/K=1/15/16）bit-exact。
- 转置/accumulate/per-channel scale 正确。
- VEC 所有 op 与 reduce 树 bit-exact。
- 错误描述符（magic/对齐/越界/dtype）被拒绝且不挂死。

## M5 · 扩展 C 编译器 + kernel 库

目标：**用 C 写 GEMM/conv/elementwise，动态 shape，在 ISS 上端到端正确。**

- 扩展 C99 子集：指针、数组、struct/union/enum、switch/goto、初始化器、预处理器。
- 寄存器分配、ABI lowering、ELF 链接、CLOV 打包。
- `libclover`（Clover 目标）：memcpy/memset、64 位 helper、softfloat。
- `kernels/gemm.c`、`conv2d.c`、`elementwise.c`、`softmax.c`。
- `clover-kernel` 元数据与 manifest。

**验收**：

- GEMM：M/N/K ∈ {1,15,16,17,31,64,127} 与 golden bit-exact。
- conv2d：im2col + GEMM 组合，stride/padding 动态。
- softmax：max/sum/exp 近似，误差在文档容差内。
- 编译器通过 50+ 负例与 100+ 正例。

## M6 · 图 runtime + RTL 端到端

### M6a · Simulation graph runtime（已完成，2026-10-18）

- `libclover` 仿真后端：设备/buffer/拷贝/kernel image/graph。
- `GEMM → VEC ADD` 两节点图；动态 shape `16x16x16`、`17x19x23` 通过。
- 仍直接调用 ISS，不经过 AXI/命令队列；CLOV/JSON 图解析后续。
- 详见 [图 runtime](11-graph-runtime.md)。

### M6b · 完整图 runtime（下一步）

目标：**主机组图，NPU 执行，动态 shape，RTL 与 golden 一致。**

- `libclover` Graph IR（JSON）、shape inference、内存规划、命令编码。
- 2-3 节点图：DMA → GEMM → elementwise → DMA。
- Verilator 顶层仿真 + 共享内存后端。
- RTL 与 ISS 的 kernel 级 co-sim。
- 示例：`examples/gemm_dynamic/`、`examples/mlp/`。

**验收**：

- 同一图在 ISS 与 RTL 上输出 bit-exact。
- 改变 batch/M/N/K 不需要重编译 kernel。
- 图执行失败时能定位到 command/seq/kernel。

## M7 · 加固与发布

- 覆盖率（定向 + 随机），失败复现脚本。
- Yosys 全量 `check`，缩减参数 `synth` 门禁。
- 性能计数器与 benchmark（GEMM/conv）。
- 文档补齐：移植指南、添加新 dtype/op 的步骤、FAQ。
- 版本 v0.1.0 tag。

**验收**：

- `make check` 在干净环境一键通过。
- 所有公开 ABI 结构体有 `_Static_assert`。
- 已知限制清单完整。

## 建议的实现顺序（垂直切片）

不要先写完整 RTL 再写软件。建议：

1. `clover_pkg.sv` + ISS + 汇编器 + 最小 compiler（M2）。
2. 用 ISS 跑通 GEMM 的 C 代码（先不管 RTL tile）。
3. 写 MMA PE + array，用 cocotb 直接注入描述符比对 golden（M4 的一部分）。
4. 把 core 的 `tile.mma` 接到 array，跑单 block。
5. 补 DMA/CP/AXI，跑 host → NPU → host。
6. 扩编译器与 kernel 库，跑动态 shape。
7. 最后接图 runtime 与 RTL co-sim。

这样每个阶段都有可运行产物，避免“大爆炸集成”。

## 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| FP32 加法/乘法 RTL 舍入错误 | 全链路数值不一致 | 先写独立 FP 单元 + 穷举/随机 vs SoftFloat；再集成 |
| 16x16 FP32 阵列仿真慢 | 回归时间失控 | 参数化 `NUM_PE_*`；CI 用 4x4/8x8；夜间跑 16x16 |
| 编译器范围蔓延 | 软件迟迟不能用 | 以 kernel 需求驱动；每加一个特性配 3 个测试；M2 只做最小集 |
| AXI 协议 corner case | 集成失败 | 自建 protocol checker + 随机 backpressure；先小 burst |
| Cache/DMA 一致性 | 偶发数据错误 | 默认张量走 scratchpad；文档化 fence；测试专门覆盖 |
| 动态 shape 边界 | 越界/漏算 | 统一 mask 逻辑；M/N/K=1/15/16/17 必测 |
| 工具版本漂移 | 环境不可复现 | `check_tools.sh` 最低版本；记录实测版本；CI 固定 |

## 工作量粗估

以“可运行的 v0.1”为目标，不含流片与物理设计：

| 阶段 | 主要工作 | 粗估 |
|---|---|---|
| M2 | 核 + ISS + 汇编 + 最小 C | 大 |
| M3 | 存储/总线/DMA/CP | 大 |
| M4 | FP + MMA + VEC | 很大 |
| M5 | 编译器 + kernel 库 | 很大 |
| M6 | 图 runtime + co-sim | 中 |
| M7 | 加固/文档 | 中 |

这是研究项目，不按商业 IP 的进度承诺；每个里程碑以“可验证”而不是“代码写完”为准。
