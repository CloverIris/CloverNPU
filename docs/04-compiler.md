# 04 · Clover C 编译器

`cloverc` 是 CloverNPU 的自研 C 编译器，C++17 实现，不依赖 LLVM/GCC。它把**扩展 C99 子集**编译为 Clover-S 机器码，并生成 ELF32 目标文件与 CLOV 镜像。

目标不是替代 GCC，而是让 kernel 作者用熟悉的 C 语法表达：

- 动态 shape 的循环与地址计算；
- 描述符填充；
- tile 原语调用；
- 多个原语的组合（GEMM、conv、softmax…）。

## 1. 工具链组成

| 工具 | 作用 |
|---|---|
| `cloverc` | 预处理、编译、汇编、链接驱动 |
| `clover-as` | 汇编器（也可由 `cloverc` 内部调用） |
| `clover-ld` | 链接器，解析符号/重定位，布局段 |
| `clover-objdump` | 反汇编 + 符号/重定位查看 |
| `clover-pack` | ELF → CLOV 扁平镜像 |
| `clover-iss` | 指令级模拟器（C++，见验证文档） |

## 2. 语言范围

### 2.1 支持

- 类型：`void`, `_Bool`, `char`, `short`, `int`, `long`, `long long`, 有/无符号，`float`（存储 + softfloat 运算），指针，数组，`struct`，`union`，`enum`，`typedef`。
- 限定符：`const`, `volatile`, `restrict`（作为优化提示）。
- 存储类：`static`, `extern`, `register`（忽略）, `auto`。
- 表达式：全部 C99 算术/位/逻辑/比较/赋值/复合赋值/条件/逗号/`sizeof`/强制转换/前置后置自增自减。
- 语句：`if/else`, `while`, `do/while`, `for`, `switch/case/default`, `break`, `continue`, `return`, `goto/label`, 复合语句，声明。
- 初始化：标量、数组、结构体、联合体、字符串、指定初始化器（`.field =`, `[i] =`）。
- 函数：原型、定义、递归、`static`，最多 8 个寄存器参数 + 栈参数。
- 预处理器：`#include`, `#define`（对象/函数宏）, `#undef`, `#if/#ifdef/#ifndef/#elif/#else/#endif`, `#pragma once`, `#error`, `#line`, `#`/`##`。
- 属性：`__attribute__((aligned(n)))`（n <= 64）, `section`, `unused`, `noreturn`。
- 内建：`__builtin_memcpy`, `__builtin_memset`, `__builtin_expect`, `__builtin_clz`。

### 2.2 不支持（v0.1）

- 可变参数 `...` / `va_list`。
- 函数指针（语法保留，语义报错）。
- VLA（变长数组）。
- 位域。
- `double` 运算（类型可声明，运算报错）。
- `_Complex`, `_Atomic`, `thread_local`。
- 内联汇编（后续以 `__clover_*` 内建替代）。
- 动态内存分配（`malloc/free` 不提供）。
- 构造函数/析构函数、异常、C++。

### 2.3 浮点约定

- `float` 是 32 位存储类型，ABI 按整数位模式传递。
- 标量 `float` 算术/比较/转换在 M5 通过 `libclover` 的 softfloat helper 实现：
  - `__clover_fadd`, `__clover_fsub`, `__clover_fmul`, `__clover_fdiv`
  - `__clover_fcmp*`, `__clover_f2i`, `__clover_i2f`
- tile engine 的 FP16/BF16/FP8/FP4 运算不经过标量 FPU；kernel 用描述符调用。
- 编译器的常量折叠对 `float` 用宿主 `float`，必须与 softfloat 的 RNE 语义一致；不确定时不做折叠。

## 3. 编译流水线

```mermaid
flowchart LR
    SRC[".c/.h"] --> PP["preprocessor"]
    PP --> LEX["lexer"]
    LEX --> PARSE["parser"]
    PARSE --> SEMA["sema / type check"]
    SEMA --> IR["typed 3-address IR"]
    IR --> OPT["O0/O1 passes"]
    OPT --> RA["linear-scan regalloc"]
    RA --> CG["Clover-S codegen"]
    CG --> ASM["asm"]
    ASM --> OBJ["ELF32 .o"]
    OBJ --> LINK["clover-ld"]
    LINK --> ELF["ELF32 executable"]
    ELF --> PACK["clover-pack"]
    PACK --> CLOV[".clov + manifest"]
```

### 3.1 前端

- 预处理器与 lexer 分离；保留 source location 供诊断。
- parser 递归下降，生成 AST；表达式用 precedence climbing。
- sema 做类型检查、隐式转换插入、常量求值、作用域/链接属性、结构体布局。
- 诊断格式：`file:line:col: error: message`，带源码行与 caret。

### 3.2 IR

- 三地址码，基本块 + CFG。
- 类型化虚拟寄存器；内存访问显式 `load/store`。
- 每个函数一个 IR；全局变量作为符号。
- 指令：`const`, `arith`, `cmp`, `cast`, `load`, `store`, `br`, `condbr`, `call`, `ret`, `phi`（v0.1 不建 SSA，phi 预留）。
- 内存模型：普通 load/store；`volatile` 标记不参与优化。

### 3.3 优化（O1）

- 常量折叠/传播。
- 代数化简（`x+0`, `x*1`, 强度削减）。
- 死代码消除。
- 基本块合并/不可达块删除。
- 局部公共子表达式。
- 简单内联（小函数、`static inline`）。
- 循环不变量外提（保守）。
- 不做自动向量化：tile 操作由程序员显式表达。

### 3.4 寄存器分配

- 线性扫描，32 个 GPR。
- 固定保留：`x0`, `sp`, `gp`, `tp`；`s0` 在需要帧指针时保留。
- 可分配约 24-27 个。
- 溢出槽 4 B；64 位值占两个槽，8 B 对齐。
- 调用点按 ABI 插入保存/恢复。
- `tile.wait` 的返回状态按普通 `rd` 处理。

### 3.5 代码生成

- 所有算术走 32 位；`long long` 用寄存器对 + 运行时 helper。
- 大立即数：`lui` + `addi`；符号地址：`auipc` + `addi`。
- 函数调用：`auipc ra, %pcrel_hi(sym)` + `jalr ra, ra, %pcrel_lo`。
- 分支：B-type，范围不足时插入 `jal` trampoline。
- `switch`：稀疏用比较链，密集用 jump table（`.rodata`）。
- 结构体复制：`__builtin_memcpy`。
- 64 位乘除、浮点：`libclover` helper。
- 栈帧：
  - 计算局部 + spill + 传参区大小；
  - 16 B 对齐（有 64 B 对齐对象时 64 B）；
  - 保存 `ra` 与使用的 callee-saved；
  - `s0` 作为帧指针（可选）。

## 4. 内建与头文件

公开头文件：

```
include/clover/clover.h         主机 runtime API（host 编译）
include/clover/clover_tile.h    kernel 侧描述符/内建（cloverc 编译）
include/clover/clover_types.h   dtype 枚举、常量
```

### 4.1 描述符类型

```c
struct clover_mma_desc {
    uint32_t magic;
    uint32_t flags;
    uint32_t m, n, k;
    uint32_t a_addr, b_addr, c_addr;
    uint32_t a_stride, b_stride, c_stride;
    uint32_t scale_a_addr, scale_b_addr;
    uint32_t post_scale, post_bias;
    uint32_t reserved;
} __attribute__((aligned(64)));
```

字段布局必须与 [ABI §6](03-abi.md) 完全一致；头文件里用 `_Static_assert(sizeof(...) == 64)` 锁定。

### 4.2 内建函数

| 内建 | 生成 |
|---|---|
| `__clover_mma(const struct clover_mma_desc *d)` | `tile.mma a0` |
| `__clover_vec(const struct clover_vec_desc *d)` | `tile.vec a0` |
| `__clover_dma(const struct clover_dma_desc *d)` | `tile.dma a0` |
| `__clover_wait(uint32_t mask)` | `tile.wait a0, a0, x0`，返回状态 |
| `__clover_wait_timeout(uint32_t mask, uint32_t t)` | `tile.wait a0, a0, a1` |
| `__clover_fence(void)` | `tile.fence` |
| `__clover_cache(uint32_t op, uint32_t addr)` | `cache.*` |
| `__clover_csr_read(uint32_t csr)` | `csrrs` |
| `__clover_csr_write(uint32_t csr, uint32_t v)` | `csrrw` |
| `__clover_cycle(void)` | 读 `cycle` |
| `__clover_break(void)` | `ebreak` |

内建参数必须是标量或指针；编译器负责装入 `a0/a1` 并发出指令。

### 4.3 便捷封装

`clover_tile.h` 提供 `static inline` 封装，避免每个 kernel 手写描述符：

```c
static inline int clover_mma_block(const struct clover_mma_desc *d)
{
    __clover_mma(d);
    return __clover_wait(CLOVER_WAIT_MMA);
}
```

真正的 GEMM 循环在 `kernels/gemm.c`，不在头文件里。

## 5. 汇编器

### 5.1 输入

- `.s` 文件，UTF-8。
- 标签、局部标签（`.L...`）、符号。
- 指示符见 [ISA §11.4](02-isa.md)。
- 表达式：符号 +/- 常量、`%hi`/`%lo`/`%pcrel_hi`/`%pcrel_lo`。

### 5.2 输出

- ELF32 `ET_REL`。
- `.text`, `.rodata`, `.data`, `.bss`。
- `.symtab`, `.strtab`, `.shstrtab`。
- `.rela.text` 等重定位段。

### 5.3 两遍扫描

1. 第一遍：计算段大小、记录标签、收集符号与重定位。
2. 第二遍：编码指令、回填分支/立即数、写 ELF。

分支范围不足时由汇编器报错（编译器负责插 trampoline）。

## 6. 链接器

`clover-ld`：

- 输入多个 ELF32 `.o` + 一个可选的链接脚本。
- 默认布局：
  - `.text` 起始 `0x1000_0000`
  - `.rodata` 之后
  - `.data` 之后
  - `.bss` 之后
- 合并同名段，解析全局/局部符号，应用重定位。
- 检查未定义符号、重复定义、越界重定位。
- 输出 `ET_EXEC` ELF32 + `map` 文件（可选）。
- 不提供动态链接、共享库、弱符号（v0.1）。

默认链接脚本（内置）：

```
SECTIONS {
  . = 0x10000000;
  .text   : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data   : { *(.data*) }
  .bss    : { *(.bss*) *(COMMON) }
}
```

## 7. 驱动与选项

```
cloverc [options] file...
  -c                 只编译/汇编，不链接
  -S                 输出汇编
  -E                 只预处理
  -o <file>          输出
  -I <dir>           头文件路径
  -D <name>[=val]    宏
  -U <name>          取消宏
  -O0 / -O1          优化级别（默认 -O1）
  -g                 生成行号注释（汇编）
  -Wall / -Werror    警告
  --target=clover-s  目标（唯一）
  --dump-ast         打印 AST
  --dump-ir          打印 IR
  --dump-asm         打印汇编
  --emit=obj|asm|clov
  -nostdlib          不链接 libclover
```

退出码：0 成功，1 编译错误，2 命令行错误，3 内部错误。

## 8. 运行时库

`libclover.a`（Clover 目标码）提供：

- `memcpy`, `memset`, `memmove`, `memcmp`
- 64 位乘除/取模
- softfloat：`fadd/fsub/fmul/fdiv/fcmp/f2i/i2f`
- `__clover_kernel_exit`
- 基础数学近似（`expf`, `tanhf`, `sqrtf`）可选，用多项式实现

这些函数本身用 Clover C 编写，由 `cloverc` 编译；不依赖宿主 libc。

## 9. 示例

### 9.1 单 block MMA

```c
#include <clover/clover_tile.h>

void mma_one(const struct clover_kernel_args *args)
{
    struct clover_mma_desc d;
    d.magic = CLOVER_MMA_MAGIC;
    d.flags = CLOVER_MMA_ACCUMULATE;
    d.m = 16; d.n = 16; d.k = 16;
    d.a_addr = args->scalar[0];
    d.b_addr = args->scalar[1];
    d.c_addr = args->scalar[2];
    d.a_stride = 32;  /* FP16: 16 elements */
    d.b_stride = 32;
    d.c_stride = 64;  /* FP32: 16 elements */
    d.scale_a_addr = 0;
    d.scale_b_addr = 0;
    d.post_scale = 0x3F800000; /* 1.0f */
    d.post_bias = 0;
    __clover_mma(&d);
    (void)__clover_wait(CLOVER_WAIT_MMA);
}
```

### 9.2 动态 shape 的 GEMM 主循环

```c
void gemm_kernel(const struct clover_kernel_args *args)
{
    const struct clover_tensor *A = &args->tensors[0];
    const struct clover_tensor *B = &args->tensors[1];
    const struct clover_tensor *C = &args->tensors[2];
    uint32_t M = A->dim[0], K = A->dim[1], N = B->dim[1];

    for (uint32_t m0 = 0; m0 < M; m0 += 16) {
        for (uint32_t n0 = 0; n0 < N; n0 += 16) {
            for (uint32_t k0 = 0; k0 < K; k0 += 16) {
                struct clover_mma_desc d;
                fill_mma(&d, A, B, C, m0, n0, k0);
                d.flags |= (k0 != 0) ? CLOVER_MMA_ACCUMULATE : 0;
                __clover_mma(&d);
            }
            (void)__clover_wait(CLOVER_WAIT_MMA);
        }
    }
}
```

`fill_mma` 由 `kernels/gemm.c` 实现，负责边界 M/N/K 裁剪与 stride 计算。

## 10. 测试策略

| 层 | 内容 |
|---|---|
| 单元 | lexer token、parser AST、sema 类型、常量折叠、寄存器分配 |
| 汇编 | 指令编码 golden、伪指令展开、重定位 |
| 链接 | 多目标文件、符号解析、段布局 |
| 执行 | `clover-iss` 运行 C 程序，比对 stdout/返回值 |
| 差分 | 同一整数 C 程序分别用宿主 clang 与 cloverc 编译，结果一致 |
| 端到端 | GEMM/conv kernel 在 ISS 与 RTL 上结果一致 |
| 负例 | 类型错误、未定义符号、越界分支、非法内建参数 |

编译器自身用 CMake + CTest 构建；测试用例在 `compiler/tests/`。

## 11. 目录

```
compiler/
  CMakeLists.txt
  include/clover/
    diagnostic.h source.h token.h lexer.h preprocessor.h
    ast.h parser.h type.h sema.h
    ir.h opt.h regalloc.h codegen.h
    target.h assembler.h linker.h driver.h
  src/
    lexer.cpp preprocessor.cpp parser.cpp sema.cpp
    ir.cpp opt.cpp regalloc.cpp codegen.cpp
    assembler.cpp linker.cpp pack.cpp driver.cpp
  tests/
    unit/ golden/ run/ negative/
```
