# 03 · Clover ABI

本文件定义 CloverNPU 的软件-硬件契约：C ABI、内存映射、描述符、命令、kernel 参数、镜像格式与主机 runtime API。任何 RTL、编译器、runtime 的改动如果触碰这些布局，必须先改本文档并升 ABI 版本。

**ABI 版本：v0.1（M1 冻结）**

## 1. 基本数据模型

| 类型 | 大小 | 对齐 | 说明 |
|---|---|---|---|
| `char` | 1 | 1 | 有符号 |
| `unsigned char` | 1 | 1 | |
| `short` | 2 | 2 | |
| `int` | 4 | 4 | |
| `long` | 4 | 4 | ILP32 |
| `long long` | 8 | 8 | |
| 指针 | 4 | 4 | 32 位物理地址 |
| `_Bool` | 1 | 1 | 0/1 |
| `enum` | 4 | 4 | int |
| `float` | 4 | 4 | v0.1 存储/softfloat |
| `double` | 8 | 8 | v0.1 不支持运算 |

- little-endian。
- 结构体按成员自然对齐，整体对齐取最大成员对齐；尾部填充到对齐倍数。
- 数组元素连续，元素间无填充。
- 位域 v0.1 不支持。
- 函数指针、可变参数 v0.1 不支持。

## 2. 寄存器与调用约定

### 2.1 寄存器角色

| 寄存器 | 角色 | 保存者 |
|---|---|---|
| `x0` | 0 | — |
| `x1 ra` | 返回地址 | caller |
| `x2 sp` | 栈指针 | callee |
| `x3 gp` | 全局指针（保留） | — |
| `x4 tp` | 保留 | — |
| `x5-x7 t0-t2` | 临时 | caller |
| `x8 s0/fp` | 帧指针/保存 | callee |
| `x9 s1` | 保存 | callee |
| `x10-x17 a0-a7` | 参数/返回 | caller |
| `x18-x27 s2-s11` | 保存 | callee |
| `x28-x31 t3-t6` | 临时 | caller |

### 2.2 参数传递

- 整数/指针参数依次放 `a0-a7`。
- 64 位标量放偶数对齐的寄存器对（`a0:a1`、`a2:a3`…）；放不下则走栈。
- 聚合类型（struct/union）：
  - 大小 <= 8 B 且对齐 <= 4：按字放入寄存器。
  - 其他：调用者在栈上复制，传递指针。
- 返回值：
  - 标量：`a0`；64 位：`a0:a1`。
  - 聚合 <= 8 B：`a0:a1`。
  - 聚合 > 8 B：调用者提供隐藏指针，作为第一个参数放入 `a0`，函数返回该指针。
- `float` 按 32 位整数位模式传递，不使用独立浮点寄存器。
- 可变参数不支持。

### 2.3 栈

- 栈向低地址增长。
- 调用边界 `sp` 16 B 对齐。
- 无 red zone。
- 函数帧布局（从高到低）：调用者帧 → 返回地址/保存寄存器 → 局部变量 → 传参区。
- 带 64 B 对齐局部变量的函数，帧对齐提升到 64 B；调用者需保证。
- `s0/fp` 在需要时指向帧基址（prologue 后的旧 `sp`）。

### 2.4 调用者/被调用者保存

- caller-saved：`ra, t0-t6, a0-a7`。
- callee-saved：`sp, s0-s11`。
- `gp/tp` 保留，编译器不使用。

### 2.5 特殊约定

- `volatile` 访问不得合并、删除或重排；用于 MMIO。
- 编译器不得假设 `sp` 之外的栈内存有效。
- 函数名不做 name mangling。
- 保留标识符前缀 `__clover_`、`clover_`（后者用于公开 API）。

## 3. Kernel 入口 ABI

kernel 是普通 C 函数：

```c
void kernel(const struct clover_kernel_args *args);
```

command processor 设置：

- `a0 = args`（scratchpad 地址，1 KB 对齐）
- `sp = stack_top`（16 B 对齐）
- `ra = exit_trampoline`
- `PC = entry`

kernel 返回时执行 `ret` 到 trampoline；trampoline 执行 `ecall`，`a7=0`（exit），`a0` 为状态码。CP 将状态码写入 completion entry。

kernel 必须：

- 不返回局部变量地址；
- 不依赖未初始化的 `.bss` 以外内存；
- 不调用未链接的符号；
- 不假设 scratchpad 之外的地址可写。

## 4. 内存映射

### 4.1 设备地址空间

| 区域 | 起始 | 结束 | 大小 | 属性 |
|---|---|---|---|---|
| Scratchpad | `0x0000_0000` | `0x0003_FFFF` | 256 KB | 直接访问，不可缓存 |
| Tile CSRs | `0x0004_0000` | `0x0004_0FFF` | 4 KB | MMIO |
| DMA CSRs | `0x0005_0000` | `0x0005_0FFF` | 4 KB | MMIO |
| CP CSRs | `0x0006_0000` | `0x0006_0FFF` | 4 KB | MMIO |
| Debug/trace | `0x0007_0000` | `0x0007_FFFF` | 64 KB | MMIO |
| Global CSR | `0x0008_0000` | `0x0008_FFFF` | 64 KB | AXI4-Lite |
| DRAM | `0x1000_0000` | `0xFFFF_FFFF` | ~3.75 GB | 可缓存，AXI4 |

仿真默认 DRAM 窗口 256 MB（`0x1000_0000-0x1FFF_FFFF`），超出报 access fault。

### 4.2 Scratchpad 预留

| 区域 | 偏移 | 大小 | 用途 |
|---|---|---|---|
| 通用 workspace | `0x0000_0000` | 224 KB | 张量 tile、描述符 |
| Kernel args | `0x0003_8000` | 1 KB | `clover_kernel_args` |
| 保留 | `0x0003_8400` | 15 KB | 未来扩展 |
| Stack | `0x0003_C000` | 16 KB | 控制核栈 |

该划分由 runtime 与链接脚本共同遵守。kernel 的 `scratch_bytes` 不能超过 224 KB；栈需求不能超过 16 KB。

### 4.3 全局 CSR（AXI4-Lite，`0x0008_0000`）

| 偏移 | 名称 | 访问 | 说明 |
|---|---|---|---|
| `0x000` | `ID` | RO | `0x564F4C43`（"CLOV"） |
| `0x004` | `VERSION` | RO | `[15:0]` minor，`[31:16]` major |
| `0x008` | `STATUS` | RO | 见下 |
| `0x00C` | `CONTROL` | RW | 见下 |
| `0x010` | `IRQ_STATUS` | RW1C | 见下 |
| `0x014` | `IRQ_ENABLE` | RW | 见下 |
| `0x018` | `QUEUE_BASE` | RW | 64 B 对齐 |
| `0x01C` | `QUEUE_SIZE` | RW | 命令数，2 的幂 |
| `0x020` | `QUEUE_HEAD` | RO | 设备读指针 |
| `0x024` | `QUEUE_TAIL` | RW | 主机写指针；写入即 doorbell |
| `0x028` | `COMPLETE_BASE` | RW | completion ring 基址 |
| `0x02C` | `COMPLETE_SIZE` | RW | 项数，2 的幂 |
| `0x030` | `COMPLETE_HEAD` | RO | 设备写指针 |
| `0x034` | `COMPLETE_TAIL` | RW | 主机读指针 |
| `0x038` | `CYCLE_LO` | RO | 周期计数低 32 位 |
| `0x03C` | `CYCLE_HI` | RO | 周期计数高 32 位 |
| `0x040` | `ERROR_CODE` | RO | 最近错误 |
| `0x044` | `ERROR_ADDR` | RO | 错误地址/命令索引 |
| `0x048` | `ABORT` | WO | 写 1 清队列并停止 |
| `0x04C` | `SCRATCH_BASE` | RO | `0x0000_0000` |
| `0x050` | `SCRATCH_SIZE` | RO | `0x0004_0000` |
| `0x054` | `CAP0` | RO | 功能位 |
| `0x058` | `CAP1` | RO | 保留 |
| `0x05C` | `SOFT_RESET` | WO | 写 1 软复位 |

`STATUS` 位：

| bit | 名称 |
|---|---|
| 0 | `IDLE` |
| 1 | `BUSY` |
| 2 | `CMD_ERROR` |
| 3 | `DMA_ERROR` |
| 4 | `TILE_ERROR` |
| 5 | `AXI_ERROR` |
| 6 | `QUEUE_EMPTY` |
| 7 | `QUEUE_FULL` |
| 8 | `IRQ` |

`CONTROL` 位：

| bit | 名称 |
|---|---|
| 0 | `ENABLE` |
| 1 | `IRQ_ENABLE` |
| 2 | `ABORT` |
| 3 | `SOFT_RESET` |

`IRQ_STATUS` / `IRQ_ENABLE` 位：

| bit | 名称 |
|---|---|
| 0 | `COMPLETE` |
| 1 | `ERROR` |
| 2 | `DMA_DONE` |
| 3 | `TILE_DONE` |
| 4 | `AXI_ERROR` |

`ERROR_CODE`：

| 值 | 含义 |
|---|---|
| 0 | none |
| 1 | bad command |
| 2 | bad queue |
| 3 | DMA error |
| 4 | tile error |
| 5 | AXI error |
| 6 | timeout |
| 7 | illegal address |

### 4.4 Tile / DMA / CP CSR

Tile（`0x0004_0000`）：

| 偏移 | 名称 | 说明 |
|---|---|---|
| `0x000` | `STATUS` | bit0 busy, bit1 error, `[7:4]` queue depth |
| `0x004` | `ERROR` | 错误码 |
| `0x008` | `BUSY_CYCLES` | busy 周期 |
| `0x00C` | `MMA_COUNT` | MMA 次数 |
| `0x010` | `VEC_ELEMS` | 向量元素数 |
| `0x014` | `LAST_DESC` | 最近描述符地址 |

DMA（`0x0005_0000`）：

| 偏移 | 名称 | 说明 |
|---|---|---|
| `0x000` | `STATUS` | bit0 busy, bit1 error, `[7:4]` outstanding |
| `0x004` | `ERROR` | 错误码 |
| `0x008` | `BYTES_MOVED` | 累计字节数 |

CP（`0x0006_0000`）：

| 偏移 | 名称 | 说明 |
|---|---|---|
| `0x000` | `STATUS` | bit0 busy, `[7:4]` queue depth |
| `0x004` | `PC` | 当前命令地址 |
| `0x008` | `SEQ` | 当前序号 |
| `0x00C` | `ERROR` | 错误码 |

## 5. 命令格式

命令 64 B，64 B 对齐，little-endian。

```c
struct clover_cmd {
    uint32_t opcode;
    uint32_t flags;
    uint32_t src;
    uint32_t dst;
    uint32_t dim0;
    uint32_t dim1;
    uint32_t dim2;
    uint32_t stride0;
    uint32_t stride1;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t seq;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t crc;        /* v0.1 必须为 0 */
};
```

| opcode | 名称 | 字段语义 |
|---|---|---|
| `0x01` | `NOP` | — |
| `0x02` | `DMA_2D` | `src`, `dst`, `dim0=bytes_per_row`, `dim1=rows`, `stride0=src_stride`, `stride1=dst_stride`, `flags[1:0]` 方向 |
| `0x03` | `RUN` | `src=entry`, `dst=args`, `dim0=stack_top`, `dim1=kernel_id`, `arg0=flags`, `arg1=timeout` |
| `0x04` | `WAIT` | `dim0=mask`, `dim1=timeout` |
| `0x05` | `SIGNAL` | `dim0=irq`（0/1），`seq` 写入 completion |
| `0x06` | `WRITE_REG` | `src=addr`, `dst=value` |
| `0x07` | `PREFETCH` | `src=addr`, `dim0=bytes` |
| `0x08` | `END` | 等待所有引擎 idle |
| `0x10` | `BARRIER` | `dim0=mask` |
| `0x11` | `DEBUG` | `src=addr`, `dst=value` |

`DMA_2D` 方向：

| flags[1:0] | 源 → 目的 |
|---|---|
| 0 | DRAM → scratchpad |
| 1 | scratchpad → DRAM |
| 2 | scratchpad → scratchpad |
| 3 | DRAM → DRAM（v0.1 不支持，报错） |

`RUN.flags`：

| bit | 名称 | 说明 |
|---|---|---|
| 0 | `WAIT_DMA` | 启动前等待之前 DMA（默认 1） |
| 1 | `PROFILE` | 记录周期到 completion |

`WAIT.mask` 与 tile mask 相同：bit0 MMA，bit1 VEC，bit2 DMA，bit3 all。

### 5.1 Completion entry

16 B：

```c
struct clover_completion {
    uint32_t seq;
    uint32_t status;     /* 0=ok, 1=error, 2=timeout, 3=aborted */
    uint32_t cycles;
    uint32_t error_code;
};
```

### 5.2 队列语义

- `QUEUE_BASE` 64 B 对齐，`QUEUE_SIZE` 为 2 的幂（单位：命令）。
- `head` 由设备递增，`tail` 由主机递增；索引按 `size` 取模。
- 主机写 `QUEUE_TAIL` 即 doorbell。
- 队列满时主机等待 completion，不得覆盖未完成命令。
- completion ring 同理；主机通过 `COMPLETE_TAIL` 归还空间。
- 所有地址与计数在 doorbell 前必须对设备可见（真实系统中需 host barrier；仿真后端忽略）。

## 6. Tile 描述符

所有描述符 64 B 对齐，位于 scratchpad。地址字段为设备字节地址。

### 6.1 Magic

| 描述符 | 常量 | 内存字节 |
|---|---|---|
| MMA | `0x31414D4D` | `"MMA1"` |
| VEC | `0x31434556` | `"VEC1"` |
| DMA | `0x31414D44` | `"DMA1"` |

### 6.2 dtype 枚举

| 值 | 类型 |
|---|---|
| 0 | FP32 |
| 1 | FP16 |
| 2 | BF16 |
| 3 | FP8 E4M3 |
| 4 | FP8 E5M2 |
| 5 | FP4 E2M1 |
| 6 | INT32 |
| 7 | 保留 |

### 6.3 MMA 描述符

```c
struct clover_mma_desc {
    uint32_t magic;        /* 0x31414D4D */
    uint32_t flags;
    uint32_t m;            /* 1..16 */
    uint32_t n;            /* 1..16 */
    uint32_t k;            /* 1..16 */
    uint32_t a_addr;
    uint32_t b_addr;
    uint32_t c_addr;
    uint32_t a_stride;     /* bytes between stored rows */
    uint32_t b_stride;
    uint32_t c_stride;
    uint32_t scale_a_addr;
    uint32_t scale_b_addr;
    uint32_t post_scale;   /* FP32 bits */
    uint32_t post_bias;    /* FP32 bits */
    uint32_t reserved;
};
```

`flags`：

| bit | 名称 | 说明 |
|---|---|---|
| 0 | `ACCUMULATE` | 读入 C 并累加；否则 C 从 0 开始 |
| 1 | `TRANSPOSE_A` | A 按 K×M 存储 |
| 2 | `TRANSPOSE_B` | B 按 N×K 存储 |
| 3 | `RELU` | 输出前 ReLU |
| 4 | `SATURATE` | 输出转换饱和（默认 1） |
| `7:5` | `A_TYPE` | dtype |
| `10:8` | `B_TYPE` | dtype |
| `13:11` | `C_TYPE` | dtype |
| 14 | `A_SCALE_EN` | |
| 15 | `B_SCALE_EN` | |
| 16 | `A_SCALE_PER_CHANNEL` | 长度 M 的 FP32 数组 |
| 17 | `B_SCALE_PER_CHANNEL` | 长度 N 的 FP32 数组 |
| 18 | `POST_SCALE_EN` | |
| 19 | `POST_BIAS_EN` | |
| `31:20` | 保留 | 0 |

运算顺序：

```
acc = ACCUMULATE ? fp32(C[m,n]) : 0
for k:
    a = unpack(A[m,k]) * (A_SCALE_EN ? sa : 1)
    b = unpack(B[k,n]) * (B_SCALE_EN ? sb : 1)
    acc = fp32_add(acc, fp32_mul(a, b))
if POST_SCALE_EN: acc *= post_scale
if POST_BIAS_EN:  acc += post_bias
if RELU:          acc = max(acc, 0)
C[m,n] = convert(acc, C_TYPE)
```

`a_stride`/`b_stride`/`c_stride` 始终是“存储布局中相邻行”的字节距离；转置时行对应逻辑上的 K 或 N 维。

### 6.4 VEC 描述符

```c
struct clover_vec_desc {
    uint32_t magic;        /* 0x31434556 */
    uint32_t flags;
    uint32_t n;            /* element count */
    uint32_t a_addr;
    uint32_t b_addr;
    uint32_t c_addr;
    uint32_t a_stride;     /* bytes */
    uint32_t b_stride;
    uint32_t c_stride;
    uint32_t src_type;
    uint32_t dst_type;
    uint32_t scale_a;      /* FP32 bits */
    uint32_t scale_b;
    uint32_t post_scale;
    uint32_t post_bias;
    uint32_t reserved;
};
```

`flags`：

| 位 | 名称 |
|---|---|
| `3:0` | `OP`：0 ADD, 1 SUB, 2 MUL, 3 MAX, 4 MIN, 5 RELU, 6 ABS, 7 NEG, 8 FMA, 9 CVT, 10 REDUCE_SUM, 11 REDUCE_MAX |
| 4 | `SATURATE` |
| 5 | `RELU` |
| 6 | `BROADCAST_A`（A_stride 视为 0） |
| 7 | `BROADCAST_B` |
| `31:8` | 保留 |

- 一元操作忽略 `b_addr/b_stride`。
- `src_type/dst_type` 为 dtype 枚举；CVT 用 `src_type` 读、`dst_type` 写。
- `scale_a/scale_b/post_scale/post_bias` 为 FP32 位模式；默认 `1.0f` / `0.0f`。
- REDUCE 输出单个标量到 `c_addr`，`dst_type` 必须为 FP32。
- INT32 运算忽略 scale 字段。

### 6.5 DMA 描述符

```c
struct clover_dma_desc {
    uint32_t magic;        /* 0x31414D44 */
    uint32_t flags;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t bytes_per_row;
    uint32_t num_rows;
    uint32_t src_stride;
    uint32_t dst_stride;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t reserved5;
    uint32_t reserved6;
    uint32_t reserved7;
};
```

`flags[1:0]` 方向同 `DMA_2D`。`src_stride`/`dst_stride` 是行间字节距离。

## 7. Kernel 参数

```c
struct clover_tensor {
    uint32_t addr;         /* device byte address */
    uint32_t dtype;
    uint32_t ndim;         /* 1..4 */
    uint32_t flags;        /* bit0: scratchpad; bit1: const */
    uint32_t dim[4];       /* elements */
    uint32_t stride[4];    /* bytes */
    uint32_t scale_addr;   /* 0 if none */
    uint32_t reserved;
};  /* 56 bytes */

struct clover_kernel_args {
    uint32_t magic;        /* 0x4752414B ("KARG") */
    uint32_t kernel_id;
    uint32_t n_tensors;
    uint32_t n_scalars;
    uint32_t scalar[16];
    struct clover_tensor tensors[16];
    uint32_t reserved[12];
};  /* 1024 bytes, 64-byte aligned */
```

偏移：

| 字段 | 偏移 | 大小 |
|---|---|---|
| magic | `0x000` | 4 |
| kernel_id | `0x004` | 4 |
| n_tensors | `0x008` | 4 |
| n_scalars | `0x00C` | 4 |
| scalar[16] | `0x010` | 64 |
| tensors[16] | `0x050` | 896 |
| reserved[12] | `0x3D0` | 48 |
| 合计 | `0x400` | 1024 |

`dim` 是元素数，`stride` 是字节数。`ndim=0` 表示标量（地址指向 4 B）。

## 8. 镜像与目标文件

### 8.1 ELF32

- `ELFCLASS32`, `ELFDATA2LSB`, `EV_CURRENT`。
- `e_machine = 0x0C10`（Clover）。
- `ET_REL`（`.o`）与 `ET_EXEC`（链接结果）。
- 段：`.text`, `.rodata`, `.data`, `.bss`。
- 符号表/字符串表：标准 ELF32。
- 重定位：

| 类型 | 值 | 公式 |
|---|---|---|
| `R_CLOVER_NONE` | 0 | — |
| `R_CLOVER_32` | 1 | `S + A` |
| `R_CLOVER_PCREL` | 2 | `S + A - P` |
| `R_CLOVER_HI20` | 3 | `(S + A + 0x800) >> 12` |
| `R_CLOVER_LO12_I` | 4 | `(S + A) & 0xFFF`，I-type |
| `R_CLOVER_LO12_S` | 5 | `(S + A) & 0xFFF`，S-type |
| `R_CLOVER_32_PCREL` | 6 | `S + A - P`（32 位） |

### 8.2 CLOV 镜像

`clover-pack` 把 ELF 转成扁平镜像，64 B header：

```c
struct clov_header {
    uint32_t magic;        /* 0x564F4C43 ("CLOV") */
    uint32_t version;      /* 1 */
    uint32_t header_size;  /* 64 */
    uint32_t flags;
    uint32_t entry;
    uint32_t load_base;    /* 默认 0x1000_0000 */
    uint32_t text_off;
    uint32_t text_size;
    uint32_t rodata_off;
    uint32_t rodata_size;
    uint32_t data_off;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t symtab_off;
    uint32_t symtab_size;
    uint32_t checksum;     /* v0.1 = 0 */
};
```

段按 64 B 对齐依次存放。runtime 加载到 `load_base + off`，并把 `[bss_start, bss_start+bss_size)` 清零。

### 8.3 Kernel manifest

与 CLOV 同目录的 JSON（可选，供 runtime 注册）：

```json
{
  "name": "gemm",
  "entry": "gemm_kernel",
  "args_size": 1024,
  "scratch_bytes": 131072,
  "stack_bytes": 8192,
  "n_tensors": 3,
  "n_scalars": 4
}
```

## 9. 主机 runtime API

头文件 `clover/clover.h`，C99，`extern "C"`。

```c
typedef struct clover_device clover_device;
typedef struct clover_buffer clover_buffer;
typedef struct clover_kernel clover_kernel;
typedef struct clover_graph  clover_graph;

typedef enum {
    CLOVER_OK = 0,
    CLOVER_ERR_INVALID,
    CLOVER_ERR_NOMEM,
    CLOVER_ERR_IO,
    CLOVER_ERR_TIMEOUT,
    CLOVER_ERR_DEVICE,
    CLOVER_ERR_UNSUPPORTED
} clover_status;

clover_status clover_open_sim(const char *config, clover_device **out);
void          clover_close(clover_device *dev);

clover_status clover_alloc(clover_device *dev, size_t bytes, size_t align,
                           uint32_t flags, clover_buffer **out);
void          clover_free(clover_buffer *buf);
void         *clover_buffer_ptr(clover_buffer *buf);
uint32_t      clover_buffer_addr(clover_buffer *buf);
size_t        clover_buffer_size(clover_buffer *buf);

clover_status clover_copy_h2d(clover_device *dev, clover_buffer *dst,
                              const void *src, size_t bytes);
clover_status clover_copy_d2h(clover_device *dev, void *dst,
                              clover_buffer *src, size_t bytes);

clover_status clover_load(clover_device *dev, const void *clov, size_t bytes,
                          clover_kernel **out);

clover_status clover_submit(clover_device *dev, const void *cmds, size_t n);
clover_status clover_wait(clover_device *dev, uint32_t seq, uint32_t timeout_ms);

clover_status clover_graph_create(clover_device *dev, clover_graph **out);
clover_status clover_graph_add(clover_graph *g, clover_kernel *k,
                               const char *const *inputs, uint32_t n_inputs,
                               const char *const *outputs, uint32_t n_outputs,
                               const char *attrs_json);
clover_status clover_graph_execute(clover_graph *g,
                                   const clover_tensor *inputs, uint32_t n_inputs,
                                   clover_tensor *outputs, uint32_t n_outputs);
void          clover_graph_destroy(clover_graph *g);
```

约定：

- 每个 device 单线程使用；v0.1 不支持并发 submit。
- `clover_buffer` 默认 DRAM；`flags` bit0 请求 scratchpad。
- 所有 buffer 64 B 对齐；地址通过 `clover_buffer_addr` 获取。
- `clover_submit` 接受已编码的 64 B 命令数组，内部完成 doorbell 与 completion 等待策略。
- `clover_graph_execute` 的 `inputs` 必须与图输入顺序一致；动态 shape 在此解析。
- 错误字符串：`clover_strerror(clover_status)`。

## 10. 版本与兼容

- ABI 版本 `MAJOR.MINOR`；结构体布局变化升 MAJOR。
- 描述符 `flags` 保留位必须写 0；设备忽略未知保留位。
- 新增 dtype/opcode 必须向后兼容：旧设备遇未知值返回 `CLOVER_ERR_UNSUPPORTED`/tile error。
- CLOV `version=1`；不兼容变更升版本。

## 11. 对齐与端序总结

| 对象 | 对齐 |
|---|---|
| 命令 | 64 B |
| completion entry | 16 B |
| MMA/VEC/DMA 描述符 | 64 B |
| kernel args | 1 KB |
| 张量基址 | 64 B 推荐 |
| 行起始 | 16 B 推荐 |
| 栈 | 16 B（含 64 B 对齐局部变量时 64 B） |

所有多字节字段 little-endian；描述符 flags 位域 LSB-first。
