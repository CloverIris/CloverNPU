# CloverNPU

CloverNPU 是一个**面向研究与验证**的传统体制可编程 NPU。它不采用 GPGPU 的宽 SIMT/warp 模型，也不把卷积、ReLU 等算子做成硬连线宏；计算由标量控制核执行 C kernel，通过紧耦合的 tile 指令驱动可编程向量/矩阵引擎，配合 DMA、scratchpad 与显式片上缓存完成。

> 当前状态：**M1 设计基线（v0.1）**。工具链已安装验证；架构、ISA、ABI、编译器与验证规范已冻结；RTL/编译器实现从 M2 开始。

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
| 标量核 | 自研 Clover-S，32 位定长指令，ILP32，5 级顺序流水 |
| 矩阵引擎 | 16 x 16 MAC，单条 MMA 处理 M,N,K <= 16 的 block |
| 向量引擎 | 16 lane，流式描述符驱动，N 由运行时给出 |
| 数值 | FP16/BF16/FP8-E4M3/FP8-E5M2/FP4-E2M1 存储；FP32 计算/累加 |
| 片上存储 | 256 KB scratchpad（16 bank）+ 32 KB I-cache + 16 KB D-cache |
| 外部接口 | AXI4 master（128-bit data）/ AXI4-Lite slave（32-bit） |
| 命令接口 | 64 B 命令、DRAM 环形队列、doorbell + completion ring + IRQ |
| 主机 runtime | C API + JSON Graph IR，执行时解析动态 shape |
| 编译器 | C++17 自研 `cloverc`，扩展 C99 子集，输出 ELF32/CLOV image |
| 仿真 | Verilator 5.x + cocotb/pytest + C++ ISS + Python golden |

## 仓库结构

```
docs/       架构、ISA、ABI、编译器、验证与路线图规范
rtl/        SystemVerilog 可综合 RTL
sim/        Verilator/cocotb 测试、ISS、Python golden model
compiler/   cloverc 编译器、汇编器、链接器
runtime/    主机侧 libclover、graph runtime
kernels/    用 Clover C 写的 GEMM/conv/elementwise 等 kernel
examples/   端到端示例与图定义
tools/      objdump、pack、调试辅助工具
scripts/    环境安装、工具检查、综合检查
tests/      跨组件测试与回归
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
```

## 设计文档

- [决策记录](docs/00-decisions.md)
- [总体架构](docs/01-architecture.md)
- [Clover-S ISA](docs/02-isa.md)
- [Clover ABI](docs/03-abi.md)
- [Clover C 编译器](docs/04-compiler.md)
- [验证与工具链](docs/05-verification.md)
- [路线图](docs/06-roadmap.md)

## 一个最小 kernel 的样子

```c
#include <clover/clover_tile.h>

/* C[16x16] += A[16x16] * B[16x16], FP16 输入，FP32 累加 */
void gemm_block(const clover_mma_desc *d)
{
    __clover_mma(d);
    __clover_wait(CLOVER_WAIT_MMA);
}
```

真正的 GEMM 由 C 循环遍历 M/N/K block，并在 K 方向设置 `accumulate`，不是硬件固化算子。

## 非目标（v0.1）

- 不做 GPGPU 式线程/ warp / 大寄存器堆。
- 不做操作系统、MMU、虚拟内存、多租户。
- 不做训练反向传播、自动微分。
- 不做 PCIe/CXL 物理层；主机接口在仿真中由 AXI 事务模型承担。
- 不承诺时序收敛/流片；Yosys 只做可综合性检查。

## 许可

TBD（在进入对外发布前由项目所有者确定）。
