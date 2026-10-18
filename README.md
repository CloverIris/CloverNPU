# CloverNPU

**面向研究与验证的可编程 NPU 原型**

[![GitHub repository](https://img.shields.io/badge/GitHub-CloverIris%2FCloverNPU-181717?logo=github)](https://github.com/CloverIris/CloverNPU)

CloverNPU 是一个以 tile 运算为核心的 NPU 软硬件协同项目。标量控制核运行 kernel，通过指令驱动可编程矩阵与向量引擎，并由命令队列、DMA 和片上 scratchpad 协同完成任务。项目提供 SystemVerilog RTL、指令集模拟器、编译工具链、主机 runtime 与端到端验证环境。

项目重点是探索清晰、可验证的 NPU 架构与软硬件接口；目前仍处于研究原型阶段，不代表已完成产品化或流片验证。

## 当前进展

| 子系统 | 状态 | 已验证内容 |
|---|---|---|
| 标量 ISA、汇编器、ISS 与 C 编译器 | 可用 | 整数 C 程序的编译与 ISS 执行 |
| Clover-S RTL 核、scratchpad、DMA 与命令处理器 | 可用 | 仿真、差分测试及 Yosys 可综合性检查 |
| Tile MMA / VEC RTL | 持续开发 | FP4–FP16 输入路径；MMA per-tensor / per-channel scale；VEC per-tensor scale |
| RTL GEMM 与两节点图 | 可用 | GEMM shape `16×16×16`、`17×19×23`；`GEMM → VEC ADD` |
| 主机 graph runtime | ISS 仿真后端可用 | 动态 shape 的 GEMM → VEC ADD |

低精度输出、通用 RTL 图 runtime、CLOV 打包与更完整的编译器支持仍在开发中。各模块的细节及限制见[路线图](docs/06-roadmap.md)和对应状态文档。

## 架构与规格

| 项目 | 规格 |
|---|---|
| 标量核 | Clover-S，32 位定长指令，ILP32，5 级顺序流水（RTL + ISS） |
| 矩阵引擎 | 16 x 16 MAC，单条 MMA 处理 M,N,K <= 16 的 block |
| 向量引擎 | 16 lane，流式描述符驱动，N 由运行时给出 |
| 数值 | FP16/BF16/FP8-E4M3/FP8-E5M2/FP4-E2M1 存储；FP32 计算/累加 |
| 片上存储 | 256 KB scratchpad（16 bank，2R1W 64 B）；I/D-cache 尚未接入 |
| 外部接口 | AXI4 master（128-bit data）/ AXI4-Lite slave（32-bit） |
| 命令接口 | 64 B 命令、DRAM 环形队列、doorbell + completion ring + IRQ |
| 主机 runtime | C API 与显式张量图；当前由 ISS 仿真后端执行，尚无 JSON / CLOV 图解析 |
| 编译器 | C++17 `cloverc`，当前 C 子集输出 flat image；ELF32/CLOV 尚未接入 |
| 仿真 | Verilator 5.x + cocotb/pytest + C++ ISS + Python golden |

## 快速开始

### 获取代码

```bash
git clone https://github.com/CloverIris/CloverNPU.git
cd CloverNPU
```

### 安装与检查工具

环境准备脚本会创建项目 Python 虚拟环境、安装 `requirements.txt` 中的依赖，并检查所需工具。缺少 Verilator、Icarus Verilog 或 Yosys 时，macOS 环境下脚本会尝试通过 Homebrew 安装。

```bash
./scripts/setup.sh
```

主要依赖包括 C++17 编译器、CMake 3.20 或更高版本、Ninja、Python 3、Verilator、Icarus Verilog、Yosys，以及 Python requirements。

### 构建与运行验证

```bash
make tools             # 构建 cloverc、汇编器、ISS 与反汇编器
make m2-test           # 运行汇编器 / ISS / 编译器回归
make rtl-gemm-test     # 运行 RTL GEMM 端到端测试
make rtl-graph-test    # 运行 RTL GEMM → VEC ADD 测试
```

运行完整验证集合：

```bash
make check
```

`make check` 会执行工具检查、ABI、仿真、RTL lint / 综合检查及相关回归，耗时和依赖多于上述单项测试。所有命令均从仓库根目录执行。

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
tools/      clover-as / clover-objdump / clover-pack
scripts/    环境安装、工具检查、综合检查
tests/      ABI、M2 垂直切片与后续回归
```

## 文档

- [决策记录](docs/00-decisions.md)
- [总体架构](docs/01-architecture.md)
- [Clover-S ISA](docs/02-isa.md)
- [Clover ABI](docs/03-abi.md)
- [Clover C 编译器](docs/04-compiler.md)
- [验证与工具链](docs/05-verification.md)
- [路线图与未完成事项](docs/06-roadmap.md)
- [M2 状态（软件切片 + RTL 核）](docs/07-m2-status.md)
- [M3 状态（scratchpad + AXI + DMA + 命令队列）](docs/08-m3-status.md)
- [M4a 状态（core 集成 + RUN）](docs/09-m4a-core-integration.md)
- [M4b 状态（功能级 tile engine + GEMM）](docs/10-m4b-tile-software.md)
- [主机图 runtime（GEMM → VEC ADD）](docs/11-graph-runtime.md)
- [RTL Tile MMA（FP32 + 16×16×16）](docs/12-rtl-tile-mma.md)
- [RTL GEMM 端到端](docs/13-rtl-gemm.md)
- [RTL 两节点图（GEMM → VEC ADD）](docs/14-rtl-graph.md)

## 示例：运行 C 程序

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

该示例返回值为 `55`，可通过 ISS 的寄存器输出确认。

## Tile kernel 接口示意

```c
#include <clover/clover_tile.h>

/* C[16x16] += A[16x16] * B[16x16], FP16 输入，FP32 累加 */
void gemm_block(const clover_mma_desc *d)
{
    __clover_mma(d);                 /* TILE.MMA */
    __clover_wait(CLOVER_WAIT_MMA);  /* TILE.WAIT */
}
```

以上代码展示目标 kernel 接口形态。当前编译器尚不支持预处理器与 `struct`；已支持的 C 子集及当前限制请以[编译器文档](docs/04-compiler.md)为准。

GEMM 通过 kernel 中的循环遍历 M/N/K 分块，并组合 MMA、VEC、DMA、WAIT 等指令完成；硬件不固化为专用 GEMM 算子。

## 范围与限制

- 项目面向架构研究和功能验证，不承诺时序收敛、芯片流片或生产级性能。
- 当前主机图 runtime 运行于 ISS 仿真后端；RTL 图测试覆盖固定的两节点命令序列，不是通用图执行后端。
- 暂不提供操作系统、MMU、虚拟内存、多租户、PCIe/CXL 物理层或训练反向传播支持。
- 具体已知限制随模块状态维护，详见[路线图](docs/06-roadmap.md)及各子系统文档。

## 许可

尚未指定。项目正式对外发布前，请由项目所有者确定并添加许可证。
