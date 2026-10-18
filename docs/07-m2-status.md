# 07 · M2 状态：软件垂直切片 + RTL 核

**状态：M2 已完成（2026-10-18）**。C 源码可以编译为 Clover-S、在 ISS 上执行，也可以在同一套 flat image 上跑 5 级 RTL 核；19 个 C 程序通过 ISS/RTL 差分。

## 1. 已实现

| 组件 | 文件 | 能力 |
|---|---|---|
| ISA 编解码 | `toolchain/src/isa.cpp` | 全部标量指令 + TILE 指令的编码/解码/反汇编 |
| 汇编器 | `toolchain/src/assembler.cpp` | 两遍扫描、标签、伪指令、`.text/.rodata/.data/.bss`、flat image |
| ISS | `toolchain/src/iss.cpp` | 32 位顺序执行、CSR、trap、syscall、内存映射、trace |
| 编译器 | `compiler/src/compiler.cpp` | C 子集 → Clover-S 汇编 → flat image |
| 驱动 | `compiler/tools/cloverc.cpp` | `-S`、`-o`、`--asm`、`--map`、`--no-start` |
| 测试 | `tests/m2/run_tests.py` | 23 个正/负例，见 §6 |
| RTL ALU | `rtl/core/clover_alu.sv` | 整数 ALU + MUL/MULH/MULHSU/MULHU/DIV/DIVU/REM/REMU |
| RTL regfile | `rtl/core/clover_regfile.sv` | 32 x 32，x0 恒 0 |
| RTL decoder | `rtl/core/clover_decoder.sv` | 标量 + TILE 指令译码，`ctrl_t` 控制束 |
| RTL core | `rtl/core/clover_core.sv` | 5 级流水、forwarding、load-use/WB-ID hazard、CSR、trap、ecall/uart |
| RTL TB | `sim/rtl/clover_core_tb.sv` | DRAM + scratchpad 仿真内存模型 |
| RTL 测试 | `tests/rtl/run_core_tests.py` | 19 个 C 程序的 ISS/RTL 差分 |

## 2. 工具用法

```bash
make tools                     # cmake + ninja 构建全部宿主工具
build/cmake/bin/cloverc -S fib.c -o fib.s
build/cmake/bin/cloverc fib.c -o fib.bin --asm fib.s --map fib.map
build/cmake/bin/clover-iss --image fib.bin --dump-regs
build/cmake/bin/clover-objdump --map fib.map --length 64 fib.bin

build/cmake/bin/clover-as sum.s -o sum.bin --map sum.map --listing sum.lst
build/cmake/bin/clover-iss --image sum.bin --trace
```

RTL 核：

```bash
make rtl-lint                 # Verilator lint
make rtl-synth                # Yosys read/proc/check
make rtl-core-test            # 19 个 C 程序在 RTL 上复现
```

### `cloverc`

```
cloverc [options] input.c
  -S                 输出汇编
  -o FILE            输出（默认 a.bin；-S 时 a.s）
  --asm FILE         同时保留汇编
  --map FILE         符号表
  --base ADDR        加载基址（默认 0x10000000）
  --entry SYMBOL     入口符号（默认 _start）
  --no-start         不生成 _start 包装
  --dump-tokens      调试：打印 token 流
```

### `clover-as`

```
clover-as [options] input.s
  -o, --output FILE  输出 flat image（默认 a.bin）
  --base ADDR        加载基址
  --entry SYMBOL     入口符号（默认 _start）
  --map FILE         符号表
  --listing FILE     反汇编清单
```

### `clover-iss`

```
clover-iss --image FILE [options]
  --base ADDR        加载基址
  --entry ADDR       入口 PC
  --stack ADDR       初始 sp（默认 0x0003fff0）
  --bss N            image 之后清零 N 字节
  --max-steps N      指令上限
  --trace            指令 trace
  --dump-regs        退出时打印寄存器
  --echo             实时回显 putchar
  --quiet            不打印 summary
```

## 3. 当前 C 子集

### 已支持

- 类型：`void`、`_Bool`、`char`/`unsigned char`、`short`、`int`、`long`（32 位）、指针、数组（含多维）。
- 声明：局部/全局变量、函数定义与原型、`static`、`extern`（忽略语义）、`const`/`volatile`（忽略语义）。
- 表达式：算术、位运算、比较、逻辑、赋值/复合赋值、条件表达式、逗号、`sizeof`、强制转换、前置/后置 `++/--`、取地址/解引用。
- 语句：`if/else`、`while`、`do/while`、`for`、`break`、`continue`、`return`、块、空语句。
- 函数：递归、最多 8 个寄存器参数、`int` 返回值、直接调用。
- 字符串字面量、全局初始化器（整数/字符串指针）。
- 内建：`__clover_exit`、`__clover_putchar`、`__clover_cycle`、`__clover_break`。

### 尚未支持（后续里程碑）

- 预处理器：`#include`、`#define`、条件编译目前被跳过；不能直接包含 `clover_tile.h`。
- `struct/union/enum/typedef`、位域。
- `switch/case`、`goto/label`。
- `float/double` 运算、`long long`。
- 可变参数、函数指针、VLA、初始化器列表（数组/结构体）。
- 内联汇编、`__attribute__`。

## 4. 代码生成约定

- 每函数固定使用 `s0` 作为帧指针；`ra`/`s0` 保存在 `-4(s0)`/`-8(s0)`。
- 局部对象从 `s0-12` 向下分配；数组按 `s0 - offset - size` 取基址。
- 表达式求值使用 `a0` + 帧内临时槽（64 个）；调用参数暂存到独立 outgoing 区（8 个）。
- 栈 16 B 对齐；大帧（> 2047 B）用 `li t0, frame; sub sp, sp, t0`。
- 目录/寻址使用绝对地址；代码固定链接在 `0x1000_0000`，暂不支持 PIC。
- 所有函数返回后由 `_start` 执行 `exit(main())`。

## 5. M2 的镜像格式（bootstrap）

当前 **尚未** 生成 ABI 文档里的 ELF32/CLOV，而是使用工具链内部的 flat image：

- `text` 从 `base` 开始；
- `rodata` 16 B 对齐后紧跟；
- `data` 16 B 对齐后紧跟；
- `bss` 不进入 image，由 `cloverc` 报告的 `bss_size` 表示；ISS 可用 `--bss N` 清零。

这是为了让 M2 尽快闭环。M5 会实现 ELF32 重定位、链接器和 CLOV header，届时 flat image 仅作为调试格式保留。

## 6. 测试

```bash
make tools
make m2-test
```

当前 23 个 ISS 用例：

- 汇编：`sum`、`hello`、`call/ret`。
- C：加法、递归 Fibonacci、循环求和、字符串输出、全局/局部数组、全局变量、`char`、指针、局部数组指针、6 参数、逻辑短路、除法/取模、`while`、`do/while`、`break/continue`、函数原型、多维数组、大栈帧（600 元素数组）。
- 负例：调用未定义函数必须编译失败。
- 每个正例还会用 `clover-objdump` 检查入口符号。

RTL 差分：上述 19 个 C 正例全部在 `clover_core_tb` 上复现，退出码与串口输出和 ISS 一致。`bigarray` 约 155k 周期，是当前最长的单测。

## 7. 已知限制

- TILE 指令在 ISS 中目前是 no-op（`tile.wait` 返回 0）；tile engine 的功能模型在 M4 加入。
- ISS 的 cache/MMIO 只是地址映射桩；没有时序。
- `div/rem` 在 RTL 中是组合逻辑（`/`、`%`）；综合面积会较大，后续可换多周期除法器。
- RTL 核没有 I-cache/D-cache、AXI、DMA、命令队列；testbench 用直接映射的仿真内存。
- RTL 核的 CSR/trap 只覆盖 `mstatus/mtvec/mepc/mcause/mtval/mscratch/cycle/instret` 与非法指令/断点/ecall/misalign 的停机路径；完整异常向量留到 M3。
- TILE 指令在 RTL/ISS 中都是 no-op（`tile.wait` 返回 0）。
- 编译器没有预处理器，kernel 头文件还不能直接包含。
- 尚未做随机指令差分；当前是 19 个定向 C 程序差分。

## 8. 下一步（M3）

1. Scratchpad 256 KB、16 bank、2R1W、仲裁。
2. I-cache/D-cache 与 AXI4/AXI4-Lite。
3. DMA 2D、command processor、completion ring。
4. `libclover` 仿真后端。
5. 随机指令差分与 AXI protocol checker。
