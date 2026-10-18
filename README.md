# CloverNPU

CloverNPU 是一个**面向研究与验证**的传统体制可编程 NPU。它不采用 GPGPU 的宽 SIMT/warp 模型，也不把卷积、ReLU 等算子做成硬连线宏；计算由标量控制核执行 C kernel，通过紧耦合的 tile 指令驱动可编程向量/矩阵引擎，配合 DMA、scratchpad 与显式片上缓存完成。

> 当前状态：**M4c-MMA + M6a 完成**。主机可驱动命令队列并 `RUN` C kernel；ISS 功能级 tile engine 已跑通动态 shape GEMM；`libclover` 仿真后端已跑通 `GEMM → VEC ADD` 两节点图；RTL 已有可综合 FP32 add/mul 与串行 16×16×16 MMA（16×16×16、8×8×8、accumulate 通过）。下一步是把 MMA 接入 core/NPU。

## 设计目标

- **传统 tile-based NPU**：主机 graph runtime 组图，NPU 命令队列执行；设备内是“标量控制核 + 可编程 tile 引擎 + DMA + scratchpad”。
- **非固化算子**：没有 conv/relu/softmax 专用硬件；kernel 用 Clover C 编写，由 MMA/VEC/DMA/WAIT 等原语组合出算子。
- **动态 shape 与图**：所有 tile 操作的 M/N/K/N 与 stride 在运行时由描述符给出，RTL 不按固定 shape 特化。
- **标准浮点存储格式**：FP16、BF16、FP8(E4M3/E5M2)、FP4(E2M1)，统一解包到 FP32 计算、FP32 累加，输出按目标格式 RNE 转换。
- **片上存储**：256 KB 多 bank scratchpad、32 KB I-cache、16 KB D-cache，DMA 与 cache 的维护责任明确。
- **模块化、可综合**：SystemVerilog RTL，Verilator 仿真、Yosys 可综合性检查；接口、描述符、ABI 全部文档化。

## 关键规格（v0.1）

| 项目 | 规格 |
|---|---|
| 标量核 | 自研 Clover-S，32 位定长指令，ILP32，5 级顺序流水（RTL + ISS） |
| 矩阵引擎 | 16 x 16 MAC，单条 MMA 处理 M,N,K <= 16 的 block |
| 向量引擎 | 16 lane，流式描述符驱动，N 由运行时给出 |
| 数值 | FP16/BF16/FP8-E4M3/FP8-E5M2/FP4-E2M1 存储；FP32 计算/累加 |
| 片上存储 | 256 KB scratchpad（16 bank，2R1W 64B，已实现）；I/D-cache 待接入 |
| 外部接口 | AXI4 master（128-bit data）/ AXI4-Lite slave（32-bit） |
| 命令接口 | 64 B 命令、DRAM 环形队列、doorbell + completion ring + IRQ |
| 主机 runtime | C API + JSON Graph IR，执行时解析动态 shape |
| 编译器 | C++17 自研 `cloverc`，当前 C 子集输出 flat image；ELF32/CLOV 在 M5 接入 |
| 仿真 | Verilator 5.x + cocotb/pytest + C++ ISS + Python golden |

## 仓库结构

```
docs/       架构、ISA、ABI、编译器、验证与路线图规范
include/    共享 ABI 头文件（host + kernel）
rtl/        SystemVerilog 可综合 RTL
sim/        Verilator/cocotb 测试、ISS、Python golden model
toolchain/  ISA 编解码、汇编器、ISS 核心库
compiler/   cloverc 编译器前端/后端/驱动
runtime/    主机侧 libclover、graph runtime（M6）
kernels/    用 Clover C 写的 GEMM/conv/elementwise 等 kernel
examples/   端到端示例与图定义
tools/      clover-as / clover-objdump
scripts/    环境安装、工具检查、综合检查
tests/      ABI、M2 垂直切片与后续回归
```

## 工具链

已在 macOS 15.7 / Apple M3 上验证：

| 工具 | 版本 |
|---|---|
| Verilator | 5.052 |
| Icarus Verilog | 13.0 |
| Yosys | 0.69 |
| cocotb | 2.1.0（项目 `.venv`） |
| CMake / Ninja | 4.1 / 1.x |
| Apple clang | 17.0 |

```bash
./scripts/setup.sh          # 创建 .venv、安装 Python 依赖、检查工具
./scripts/check_tools.sh    # 打印版本并检查最低版本
make smoke                  # 工具链最小闭环：Verilator + Icarus + cocotb + Yosys
make tools                  # 构建 cloverc / clover-as / clover-iss / clover-objdump
make m2-test                # 23 个汇编/C 程序的 ISS 端到端回归
make rtl-core-test          # 19 个 C 程序在 5 级 RTL 核上复现
make rtl-mem-test           # scratchpad + AXI-CSR + DMA + CP + NPU end-to-end
make rtl-npu-test           # host -> RUN C kernel -> DMA result -> completion
make tile-gemm-test         # dynamic-shape GEMM on the ISS tile engine
make runtime-graph-test     # GEMM -> VEC ADD graph via libclover sim
make rtl-tile-test          # FP32 add/mul + 16x16x16 MMA RTL
make rtl-mem-synth          # Yosys check of the M3/M4a blocks
```

## 设计文档

- [决策记录](docs/00-decisions.md)
- [总体架构](docs/01-architecture.md)
- [Clover-S ISA](docs/02-isa.md)
- [Clover ABI](docs/03-abi.md)
- [Clover C 编译器](docs/04-compiler.md)
- [验证与工具链](docs/05-verification.md)
- [路线图](docs/06-roadmap.md)
- [M2 状态（软件切片 + RTL 核）](docs/07-m2-status.md)
- [M3 状态（scratchpad + AXI + DMA + 命令队列）](docs/08-m3-status.md)
- [M4a 状态（core 集成 + RUN）](docs/09-m4a-core-integration.md)
- [M4b 状态（功能级 tile engine + GEMM）](docs/10-m4b-tile-software.md)
- [主机图 runtime（GEMM → VEC ADD）](docs/11-graph-runtime.md)
- [RTL Tile MMA（FP32 + 16×16×16）](docs/12-rtl-tile-mma.md)

## 现在就能跑的 C

```c
int fib(int n)
{
    if (n < 2)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void)
{
    return fib(10);   /* ISS 退出码 55 */
}
```

```bash
build/cmake/bin/cloverc fib.c -o fib.bin --asm fib.s
build/cmake/bin/clover-iss --image fib.bin --dump-regs
```

## 目标 kernel 的形态（M4/M5）

```c
#include <clover/clover_tile.h>

/* C[16x16] += A[16x16] * B[16x16], FP16 输入，FP32 累加 */
void gemm_block(const clover_mma_desc *d)
{
    __clover_mma(d);                 /* TILE.MMA */
    __clover_wait(CLOVER_WAIT_MMA);  /* TILE.WAIT */
}
```

> 当前编译器还没有预处理器与 struct 支持，这段 kernel 是 M4/M5 的目标形态；M2 先用无头文件的整数 C 打通执行链路。

真正的 GEMM 由 C 循环遍历 M/N/K block，并在 K 方向设置 `accumulate`，不是硬件固化算子。

## M3 的存储/总线/命令子系统

- `rtl/mem/clover_scratchpad.sv`：256 KB，16 bank × 16 KB，2 个 64 B 读端口 + 1 个 64 B 写端口，byte enable。
- `rtl/bus/clover_axi4lite_slave.sv`：32-bit AXI4-Lite slave，独立 AW/W 通道、B/R 响应。
- `rtl/top/clover_csr.sv`：全局 CSR（ID/VERSION/STATUS/CONTROL/IRQ/QUEUE/COMPLETE/CYCLE/SCRATCH/CAP）。
- `rtl/dma/clover_dma.sv`：2D DMA，DRAM ↔ scratchpad，16 B 单拍 AXI。
- `rtl/cmd/clover_cmd_proc.sv`：64 B 命令 ring、doorbell、DMA、completion、IRQ。
- `rtl/top/clover_axi_csr.sv`：AXI4-Lite + CSR 集成。
- `rtl/top/clover_npu_mem.sv`：AXI4-Lite + CP + DMA + 5 级核 + scratchpad + AXI4 master。
- `rtl/top/clover_core_mem.sv`：5 级核 + scratchpad 取指/读写适配器。
- `RUN`：`src=entry`、`dst=args`、`dim0=stack_top`；boot 时 `ra=0`，`ret` 到 0 即 kernel 返回。
- `kernels/gemm.c`：动态 M/N/K 的 16×16×16 block GEMM；ISS 功能级 tile engine 通过。
- `kernels/add.c` + `runtime/src/clover_sim.cpp`：`GEMM → VEC ADD` 两节点图。
- `rtl/fp/clover_fp_pkg.sv` + `rtl/tile/clover_tile_mma.sv`：可综合 FP32 add/mul 与串行 MMA。

## M2b 的 RTL 核

- `rtl/common/clover_pkg.sv`：参数、opcode/ALU/branch/dtype 枚举、CSR 地址、`ctrl_t`。
- `rtl/core/clover_alu.sv`：整数 ALU + 乘除。
- `rtl/core/clover_regfile.sv`：32 x 32 寄存器堆，x0 恒 0。
- `rtl/core/clover_decoder.sv`：完整标量指令 + TILE 指令译码。
- `rtl/core/clover_core.sv`：5 级流水、EX/MEM/WB 转发、load-use/WB-ID hazard、CSR、trap、ecall/uart。
- `sim/rtl/clover_core_tb.sv`：仿真内存模型（DRAM + scratchpad）。
- `tests/rtl/run_core_tests.py`：19 个 C 程序的 ISS/RTL 差分。

当前 RTL 核**尚未接入** I-cache/D-cache、AXI、DMA、命令队列与 tile engine；这些是 M3/M4。

## 非目标（v0.1）

- 不做 GPGPU 式线程/ warp / 大寄存器堆。
- 不做操作系统、MMU、虚拟内存、多租户。
- 不做训练反向传播、自动微分。
- 不做 PCIe/CXL 物理层；主机接口在仿真中由 AXI 事务模型承担。
- 不承诺时序收敛/流片；Yosys 只做可综合性检查。

## 许可

TBD（在进入对外发布前由项目所有者确定）。
