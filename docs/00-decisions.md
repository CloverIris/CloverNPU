# 00 · 决策记录（ADR）

本文件记录已经冻结的设计选择。标记为 **确认** 的条目来自项目所有者；标记为 **默认** 的条目是为保持规范完整而设定的工程默认值，可在不破坏 ABI 的前提下调整。

## D-001 项目定位

- **状态**：确认
- **决策**：CloverNPU 是研究/验证型 NPU，不追求产品化面积、功耗与流片时序；但 RTL 必须保持可综合、可单元测试、接口可集成。
- **影响**：允许使用较大的 16x16 FP32 内积阵列；允许用 Python 做 golden model；不引入厂商 IP。

## D-002 计算体制

- **状态**：确认
- **决策**：传统 tile-based 体制，采用“32 位标量控制核 + 紧耦合可编程向量/矩阵引擎 + DMA + scratchpad”，不做 GPGPU/SIMT。
- **影响**：没有 warp、线程上下文、宽 SIMT 寄存器堆；并行度来自 16x16 MAC 阵列与 16 lane 向量单元。

## D-003 图与动态 shape 的执行位置

- **状态**：确认
- **决策**：主机 `libclover` 负责组图、shape 推导、内存规划与命令编码；NPU 命令处理器执行 64 B 命令的环形队列。动态 shape 在入队时解析。
- **影响**：设备侧不需要图解释器；RTL 只处理命令与描述符。主机往返次数由命令队列摊薄。

## D-004 标量核

- **状态**：确认
- **决策**：自研 Clover-S 32 位 RISC，32 个 GPR，32 位定长指令，ILP32，5 级顺序流水，机器模式，无 MMU。
- **影响**：ABI、汇编器、编译器后端全部自控；不依赖 RISC-V 工具链；需要自带 `clover-as`/`clover-ld`/`clover-objdump`。

## D-005 加速器编程模型

- **状态**：确认
- **决策**：紧耦合自定义指令。标量核执行 `TILE.MMA` / `TILE.VEC` / `TILE.DMA` / `TILE.WAIT`，操作数是指向 64 B 描述符的地址；描述符放在 scratchpad。
- **影响**：编译器只需生成标量代码 + 少量 tile 指令；描述符由 C 结构体填充。动态 shape 全部落在描述符字段里。

## D-006 首版计算规模

- **状态**：确认
- **决策**：16x16 MAC 阵列 + 16 lane 向量单元；所有规模参数化，首版默认取该配置。
- **影响**：单条 MMA 是 16x16x16 block，不是整张 GEMM；M/N/K 可以小于 16，由 mask 处理边界。整图 GEMM 由 C 循环组合。

## D-007 数值格式

- **状态**：确认
- **决策**：存储格式支持 FP16、BF16、FP8 E4M3、FP8 E5M2、FP4 E2M1；计算统一解包到 FP32，乘加为 FP32，累加器 FP32；输出按目标格式 RNE 转换。支持 per-tensor 与 per-channel scale。
- **影响**：
  - 低精度输入的两个操作数乘积在 FP32 中精确（FP16 11x11、BF16 8x8、FP8 4x4、FP4 2x2 位），因此“先乘后加”与 FMA 的差别只在加法舍入。
  - v0.1 不做 OCP MX 块缩放，也不做 FP4/FP8 的打包双倍吞吐；这些是后续优化。
  - 这不是“原生低精度 MAC”，文档与论文中必须明确。

## D-008 存储层次

- **状态**：确认
- **决策**：256 KB scratchpad + 32 KB I-cache + 16 KB D-cache。
- **默认细节**：
  - scratchpad：16 bank x 16 KB，2 读 1 写，每端口 64 B/cycle。
  - I-cache：4-way，64 B line，32 KB。
  - D-cache：4-way，64 B line，16 KB，write-through，no-write-allocate。
  - DMA 不经过 D-cache；软件在 DMA 触碰 cached 区域前后执行 fence/flush/invalidate。
  - 张量与描述符默认放 scratchpad；代码与图级中间张量默认放 DRAM。

## D-009 总线接口

- **状态**：确认
- **决策**：直接实现 AXI4 master 与 AXI4-Lite slave，不做“原生接口 + 包装”。
- **默认细节**：
  - AXI4：128-bit data，32-bit address，4-bit ID，INCR burst，最大 16 beat。
  - AXI4-Lite：32-bit，用于全局 CSR、doorbell、IRQ。
  - 单时钟域，`clk`/`rst_n`。

## D-010 RTL 与验证栈

- **状态**：确认
- **决策**：SystemVerilog + Verilator + cocotb/pytest；Yosys 做可综合性检查。
- **影响**：RTL 不使用仿真专用构造；不允许 `initial` 中的可综合逻辑；时钟/复位显式。

## D-011 编译器

- **状态**：确认
- **决策**：C++17 全自研 `cloverc`；扩展 C99 子集；自带预处理器、lexer、parser、sema、IR、寄存器分配、汇编器、链接器。
- **默认细节**：
  - 不做 C++、不做 varargs、不做函数指针（v0.1）、不做动态内存分配。
  - `float` 进入类型系统，但标量浮点运算通过 `libclover` 的 softfloat helper 实现（M5 起）；`double` v0.1 不支持。
  - 对象格式 ELF32（`e_machine = 0x0C10`）；最终镜像由 `clover-pack` 打成 CLOV。

## D-012 工具链

- **状态**：确认
- **决策**：Homebrew 安装 Verilator/Icarus/Yosys；Python 依赖放项目 `.venv`。
- **实测版本**：Verilator 5.052、Icarus 13.0、Yosys 0.69、cocotb 2.1.0。

## D-013 首版不做的内容

- **状态**：确认
- 多核、集群、NoC。
- 虚拟内存/MMU、用户态、抢占式调度。
- 训练/反向传播。
- PCIe/CXL 物理接口。
- 原生 FP4/FP8 双倍吞吐、MX block scaling。
- 动态电压频率、功耗管理。

## D-014 M2a 的临时镜像格式

- **状态**：临时（M5 替换）
- **决策**：M2a 的 `cloverc`/`clover-as` 输出 flat image（text + rodata + data，bss 单独报告），不生成 ABI 文档中的 ELF32/CLOV。
- **理由**：先打通“C → 汇编 → ISS → 退出码”的最小闭环，避免在编译器尚未稳定时同时实现重定位/链接器/镜像打包。
- **约束**：M5 必须实现 ELF32 重定位、`clover-ld` 与 `clover-pack`；flat image 只能作为调试格式保留。

## D-015 RTL 公共定义不使用 SystemVerilog package

- **状态**：确认（工具兼容性）
- **决策**：`rtl/common/clover_pkg.sv` 是带 include guard 的编译单元级 typedef/localparam 头文件，不是 `package`。
- **理由**：Yosys 0.69 的 SystemVerilog 前端不支持 `package`/`import`；Verilator 5.052 两者都支持。
- **约束**：所有 RTL 模块通过 `` `include "clover_pkg.sv" `` 获取类型；不要改成 `package`，否则 Yosys 门禁失效。

## 待冻结（不阻塞当前实现）

这些问题在进入对应里程碑前必须给出答案，当前先按默认值实现：

| 编号 | 问题 | 当前默认 |
|---|---|---|
| O-01 | 中断模型：单 IRQ 还是 completion queue 轮询？ | 单 IRQ + completion ring |
| O-02 | 是否支持 64 位地址？ | 否，32 位 |
| O-03 | D-cache 写策略？ | write-through, no-write-allocate |
| O-04 | 是否允许 unaligned load/store？ | 否，触发 misaligned 异常 |
| O-05 | 描述符/命令的校验和？ | v0.1 不做 |
| O-06 | 编译器是否支持 bitfield？ | v0.1 不支持 |
| O-07 | 是否引入 INT8/INT4？ | v0.1 不做，接口保留 dtype 枚举 |
| O-08 | 目标频率？ | 500 MHz 名义值，不做时序收敛 |
