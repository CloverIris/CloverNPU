# 02 · Clover-S ISA

Clover-S 是 CloverNPU 的 32 位标量控制核指令集。它借鉴 RISC-V 的编码习惯，但**不是 RISC-V**：opcode、tile 指令、CSR 与异常模型都由本项目定义。

## 1. 机器模型

| 项 | 定义 |
|---|---|
| 字长 | 32 bit |
| 地址 | 32 bit 物理地址，字节寻址 |
| 字节序 | little-endian |
| GPR | 32 个，`x0` 恒为 0 |
| PC | 32 bit，指向当前指令 |
| 指令长度 | 固定 32 bit，4 B 对齐 |
| 特权级 | 仅 machine mode |
| MMU | 无 |
| 浮点寄存器 | 无；FP 由 tile engine 处理 |
| 复位向量 | `0x1000_0000`（可通过顶层参数覆盖） |

## 2. 寄存器

| 编号 | ABI 名 | 用途 | 保存者 |
|---|---|---|---|
| `x0` | `zero` | 常量 0 | — |
| `x1` | `ra` | 返回地址 | caller |
| `x2` | `sp` | 栈指针 | callee |
| `x3` | `gp` | 全局指针（v0.1 未用） | — |
| `x4` | `tp` | 线程指针（v0.1 未用） | — |
| `x5-x7` | `t0-t2` | 临时 | caller |
| `x8` | `s0/fp` | 保存寄存器/帧指针 | callee |
| `x9` | `s1` | 保存寄存器 | callee |
| `x10-x17` | `a0-a7` | 参数/返回值 | caller |
| `x18-x27` | `s2-s11` | 保存寄存器 | callee |
| `x28-x31` | `t3-t6` | 临时 | caller |

## 3. 指令格式

所有指令 32 bit。字段位置：

```
R-type:  funct7[31:25] rs2[24:20] rs1[19:15] funct3[14:12] rd[11:7] opcode[6:0]
I-type:  imm[31:20]    rs1[19:15] funct3[14:12] rd[11:7] opcode[6:0]
S-type:  imm[11:5]     rs2[24:20] rs1[19:15] funct3[14:12] imm[4:0] opcode[6:0]
B-type:  imm[12|10:5]  rs2[24:20] rs1[19:15] funct3[14:12] imm[4:1|11] opcode[6:0]
U-type:  imm[31:12]                              rd[11:7] opcode[6:0]
J-type:  imm[20|10:1|11|19:12]                   rd[11:7] opcode[6:0]
```

- `imm` 除特别说明外为二进制补码。
- I-type 的 12-bit 立即数符号扩展到 32 bit。
- S-type 的 12-bit 立即数由 `imm[11:5]` 与 `imm[4:0]` 拼接。
- B-type 的 13-bit 偏移为 2 的倍数，范围 `-4096..+4094`。
- J-type 的 21-bit 偏移为 2 的倍数，范围 `-1 MiB..+1 MiB-2`。

## 4. Opcode 分配

| opcode | 名称 | 说明 |
|---|---|---|
| `0x03` | LOAD | 整数 load |
| `0x0F` | MISC-MEM | FENCE / CACHE |
| `0x13` | OP-IMM | 立即数 ALU |
| `0x17` | AUIPC | PC 相对高位 |
| `0x23` | STORE | 整数 store |
| `0x2B` | TILE | tile engine 指令 |
| `0x33` | OP | 寄存器 ALU |
| `0x37` | LUI | 高位立即数 |
| `0x63` | BRANCH | 条件分支 |
| `0x67` | JALR | 间接跳转 |
| `0x6F` | JAL | 直接跳转 |
| `0x73` | SYSTEM | CSR / ECALL / MRET / WFI |

其余 opcode 保留，执行时触发 illegal instruction。

## 5. 整数指令

### 5.1 OP-IMM（`0x13`）

| funct3 | 指令 | 语义 |
|---|---|---|
| `000` | `addi rd, rs1, imm` | `rd = rs1 + imm` |
| `001` | `slli rd, rs1, shamt` | `rd = rs1 << shamt`，`imm[11:5]=0` |
| `010` | `slti` | `rd = (rs1 < imm) ? 1 : 0`，有符号 |
| `011` | `sltiu` | 无符号比较 |
| `100` | `xori` | 按位异或 |
| `101` | `srli` / `srai` | `imm[11:5]=0` / `0x20` |
| `110` | `ori` | 按位或 |
| `111` | `andi` | 按位与 |

`shamt = imm[4:0]`。

### 5.2 OP（`0x33`）

| funct7 | funct3 | 指令 | 语义 |
|---|---|---|---|
| `0000000` | `000` | `add` | `rd = rs1 + rs2` |
| `0100000` | `000` | `sub` | `rd = rs1 - rs2` |
| `0000000` | `001` | `sll` | `rd = rs1 << rs2[4:0]` |
| `0000000` | `010` | `slt` | 有符号小于 |
| `0000000` | `011` | `sltu` | 无符号小于 |
| `0000000` | `100` | `xor` | 按位异或 |
| `0000000` | `101` | `srl` | 逻辑右移 |
| `0100000` | `101` | `sra` | 算术右移 |
| `0000000` | `110` | `or` | 按位或 |
| `0000000` | `111` | `and` | 按位与 |
| `0000001` | `000` | `mul` | 低 32 位乘积 |
| `0000001` | `001` | `mulh` | 有符号 × 有符号高 32 位 |
| `0000001` | `010` | `mulhsu` | 有符号 × 无符号高 32 位 |
| `0000001` | `011` | `mulhu` | 无符号 × 无符号高 32 位 |
| `0000001` | `100` | `div` | 有符号除法，除零返回 `-1` |
| `0000001` | `101` | `divu` | 无符号除法，除零返回 `0xFFFF_FFFF` |
| `0000001` | `110` | `rem` | 有符号余数，除零返回被除数 |
| `0000001` | `111` | `remu` | 无符号余数，除零返回被除数 |

v0.1 允许 `div/rem` 由多周期硬件或软件模拟实现；编译器不假设固定延迟。溢出语义：

- `div(0x8000_0000, -1) = 0x8000_0000`
- `rem(0x8000_0000, -1) = 0`

### 5.3 LUI / AUIPC

| opcode | 指令 | 语义 |
|---|---|---|
| `0x37` | `lui rd, imm20` | `rd = imm20 << 12` |
| `0x17` | `auipc rd, imm20` | `rd = PC + (imm20 << 12)` |

### 5.4 LOAD / STORE

LOAD（`0x03`）：`rd = mem[rs1 + imm]`，地址必须自然对齐。

| funct3 | 指令 | 宽度 | 扩展 |
|---|---|---|---|
| `000` | `lb` | 8 | 符号 |
| `001` | `lh` | 16 | 符号 |
| `010` | `lw` | 32 | — |
| `100` | `lbu` | 8 | 零 |
| `101` | `lhu` | 16 | 零 |

STORE（`0x23`）：`mem[rs1 + imm] = rs2`。

| funct3 | 指令 | 宽度 |
|---|---|---|
| `000` | `sb` | 8 |
| `001` | `sh` | 16 |
| `010` | `sw` | 32 |

未对齐访问触发 misaligned 异常；v0.1 不做硬件拆分。

### 5.5 BRANCH / JAL / JALR

BRANCH（`0x63`）：比较 `rs1` 与 `rs2`，条件成立时 `PC += offset`。

| funct3 | 指令 | 条件 |
|---|---|---|
| `000` | `beq` | `rs1 == rs2` |
| `001` | `bne` | `rs1 != rs2` |
| `100` | `blt` | 有符号 `<` |
| `101` | `bge` | 有符号 `>=` |
| `110` | `bltu` | 无符号 `<` |
| `111` | `bgeu` | 无符号 `>=` |

| opcode | 指令 | 语义 |
|---|---|---|
| `0x6F` | `jal rd, offset` | `rd = PC+4; PC += offset` |
| `0x67` | `jalr rd, rs1, imm` | `t = PC+4; PC = (rs1+imm) & ~1; rd = t` |

`rd=x0` 表示丢弃返回地址。

## 6. MISC-MEM

### 6.1 FENCE（`0x0F`, funct3=`000`）

`fence` 等待之前所有 store 对 DMA、tile engine 与外部观察者可见，并阻止之后的 load/store 越过。v0.1 不区分 pred/succ 位，`imm=0` 即全局 fence。

### 6.2 CACHE（`0x0F`, funct3=`001`）

R-type 形式：`rs1 = op`，`rs2 = address`（0 表示全部），`rd = 0`。

| op | 助记符 | 语义 |
|---|---|---|
| 0 | `cache.flush.d` | 写回 dirty D-cache line（write-through 下等待 store 完成） |
| 1 | `cache.inval.d` | 使 D-cache line 无效 |
| 2 | `cache.flush.inval.d` | 先 flush 再 invalidate |
| 3 | `cache.inval.i` | 使 I-cache line 无效 |
| 4 | `cache.flush.all` | 全部 D-cache flush |

`address=0` 表示整个 cache；非零表示按地址查找。v0.1 实现可忽略地址，做全量操作。

## 7. SYSTEM

`0x73`，funct3：

| funct3 | 指令 | 说明 |
|---|---|---|
| `000` | `ecall` | 系统调用，`a7` 为编号 |
| `000` | `ebreak` | 断点 |
| `000` | `mret` | 从异常返回 |
| `000` | `wfi` | 等待中断 |
| `001` | `csrrw` | `rd = csr; csr = rs1` |
| `010` | `csrrs` | `rd = csr; csr |= rs1` |
| `011` | `csrrc` | `rd = csr; csr &= ~rs1` |
| `101` | `csrrwi` | `rd = csr; csr = zimm` |
| `110` | `csrrsi` | `rd = csr; csr |= zimm` |
| `111` | `csrrci` | `rd = csr; csr &= ~zimm` |

`ecall` 编码：`imm=0x000`；`ebreak`：`imm=0x001`；`mret`：`imm=0x302`；`wfi`：`imm=0x105`。

### 7.1 系统调用（v0.1）

| `a7` | 名称 | 参数 | 返回 |
|---|---|---|---|
| 0 | `exit` | `a0 = status` | 不返回 |
| 1 | `putchar` | `a0 = char` | `a0 = 0` |
| 2 | `get_cycle` | — | `a0 = cycle[31:0]` |
| 3 | `debug_break` | — | 进入调试停机 |

kernel 正常退出用 `exit(0)`。command processor 把 `exit` 视为 kernel 完成。

## 8. CSR

| 地址 | 名称 | 说明 |
|---|---|---|
| `0x300` | `mstatus` | `MIE[3]`, `MPIE[7]`, `MPP[12:11]` |
| `0x304` | `mie` | `MEIE[11]` |
| `0x305` | `mtvec` | 异常入口，direct 模式 |
| `0x340` | `mscratch` | 暂存 |
| `0x341` | `mepc` | 异常 PC |
| `0x342` | `mcause` | 异常原因 |
| `0x343` | `mtval` | 异常附加信息 |
| `0x344` | `mip` | `MEIP[11]` |
| `0xC00` | `cycle` | 周期计数低 32 位 |
| `0xC02` | `instret` | 退休指令计数低 32 位 |
| `0x7C0` | `tile_status` | bit0 busy, bit1 error, bit[7:4] queue depth |
| `0x7C1` | `tile_error` | 最近一次 tile 错误码 |
| `0x7C2` | `dma_status` | bit0 busy, bit1 error, bit[7:4] outstanding |
| `0x7C3` | `cp_status` | bit0 busy, bit[7:4] queue depth |
| `0x7C4` | `hart_id` | 恒 0 |
| `0x7C5` | `perf_cycles` | tile engine busy cycles |
| `0x7C6` | `perf_mma` | MMA 次数 |
| `0x7C7` | `perf_vec` | VEC 元素数 |

写入只读 CSR 触发 illegal instruction。未定义 CSR 读返回 0，写忽略（v0.1），后续可改为异常。

## 9. 异常

| `mcause` | 名称 | 触发 |
|---|---|---|
| 0 | instruction address misaligned | 跳转目标非 4 B 对齐 |
| 1 | instruction access fault | I-cache/AXI 错误 |
| 2 | illegal instruction | 未定义 opcode/funct |
| 3 | breakpoint | `ebreak` |
| 4 | load address misaligned | load 未对齐 |
| 5 | load access fault | AXI 错误 |
| 6 | store address misaligned | store 未对齐 |
| 7 | store access fault | AXI 错误 |
| 11 | environment call from M-mode | `ecall` |

异常处理：

1. `mepc = 故障指令 PC`（`ecall/ebreak` 为当前 PC）。
2. `mcause = 原因`，`mtval = 地址/指令`。
3. `mstatus.MPIE = MIE`，`MIE = 0`，`MPP = 3`。
4. `PC = mtvec`。
5. `mret` 恢复 `MIE` 并跳回 `mepc`。

v0.1 不支持嵌套异常与用户态。

## 10. TILE 指令

### 10.1 编码

opcode `0x2B`，R-type：

```
funct7 = 0
rs2    = flags（保留，v0.1 置 0）
rs1    = 描述符地址 / wait mask
funct3 = 操作
rd     = 目的（仅 WAIT 使用，其余置 0）
```

| funct3 | 助记符 | 语义 |
|---|---|---|
| `000` | `tile.mma rs1` | 提交 MMA 描述符 |
| `001` | `tile.vec rs1` | 提交 VEC 描述符 |
| `010` | `tile.dma rs1` | 提交 DMA 描述符 |
| `011` | `tile.wait rd, rs1, rs2` | 等待 mask；`rs2` 超时（0=无限） |
| `100` | `tile.fence` | 等待 tile 队列与 DMA 全部完成 |
| 其余 | — | illegal instruction |

时序语义：

- `tile.mma/vec/dma` 在描述符被 tile engine 接收后退休；不等待执行完成。
- tile engine 按提交顺序执行描述符。
- `tile.wait` 阻塞，直到 mask 对应引擎完成、超时或错误；结果写 `rd`：
  - 0 = 成功
  - 1 = 超时
  - 2 = tile/dma 错误
- mask：
  - bit0 = MMA
  - bit1 = VEC
  - bit2 = DMA
  - bit3 = 全部
- `tile.fence` 等价于 `tile.wait x0, 0xF, 0`。
- tile 指令对之前的 scratchpad store 有隐式顺序保证；对 DRAM 中的描述符，软件必须先 `fence`。

### 10.2 描述符

描述符必须 64 B 对齐，放在 scratchpad。字段定义见 [ABI §6](03-abi.md)。magic：

- MMA：`0x31414D4D`（内存字节 `"MMA1"`）
- VEC：`0x31434556`（内存字节 `"VEC1"`）
- DMA：`0x31414D44`（内存字节 `"DMA1"`）

### 10.3 汇编示例

```asm
    .section .text
    .globl gemm_block
gemm_block:
    addi    sp, sp, -64
    sw      ra, 60(sp)
    # a0 = clover_mma_desc*
    tile.mma a0
    li      t0, 1
    tile.wait t1, t0, 0
    bnez    t1, .Lerr
    lw      ra, 60(sp)
    addi    sp, sp, 64
    ret
.Lerr:
    mv      a0, t1
    lw      ra, 60(sp)
    addi    sp, sp, 64
    ret
```

## 11. 汇编器语法

### 11.1 寄存器名

`x0..x31`，以及 ABI 名 `zero, ra, sp, gp, tp, t0-t6, s0-s11, a0-a7, fp`。

### 11.2 指令

- 整数：`add, addi, sub, and, andi, or, ori, xor, xori, sll, slli, srl, srli, sra, srai, slt, slti, sltu, sltiu, lui, auipc`
- 乘除：`mul, mulh, mulhsu, mulhu, div, divu, rem, remu`
- 访存：`lb, lh, lw, lbu, lhu, sb, sh, sw`
- 控制：`beq, bne, blt, bge, bltu, bgeu, jal, jalr, ret, call, tail`
- 系统：`ecall, ebreak, mret, wfi, csrrw, csrrs, csrrc, csrrwi, csrrsi, csrrci, fence`
- cache：`cache.flush.d, cache.inval.d, cache.flush.inval.d, cache.inval.i, cache.flush.all`
- tile：`tile.mma, tile.vec, tile.dma, tile.wait, tile.fence`

### 11.3 伪指令

| 伪指令 | 展开 |
|---|---|
| `nop` | `addi x0, x0, 0` |
| `mv rd, rs` | `addi rd, rs, 0` |
| `li rd, imm` | `lui` + `addi`（按需） |
| `la rd, symbol` | `auipc` + `addi`（PC 相对） |
| `not rd, rs` | `xori rd, rs, -1` |
| `neg rd, rs` | `sub rd, x0, rs` |
| `ret` | `jalr x0, ra, 0` |
| `call sym` | `auipc ra, ...; jalr ra, ra, ...` |
| `tail sym` | `auipc t1, ...; jalr x0, t1, ...` |
| `bnez rs, label` | `bne rs, x0, label` |
| `beqz rs, label` | `beq rs, x0, label` |

### 11.4 汇编指示符

`.section`, `.text`, `.data`, `.rodata`, `.bss`, `.globl`, `.local`, `.align n`（2^n 对齐）, `.balign n`, `.byte`, `.half`, `.word`, `.space`, `.zero`, `.asciz`, `.string`, `.equ`, `.set`。

## 12. 编码示例

`addi a0, a0, 1`：

```
imm=1, rs1=x10, funct3=000, rd=x10, opcode=0x13
= 0x00150513
```

`tile.mma a0`（`rs1=x10`, `funct3=000`, `rd=0`）：

```
funct7=0, rs2=0, rs1=10, funct3=0, rd=0, opcode=0x2B
= 0x0005002B
```

`tile.wait t1, t0, x0`（`rd=x6`, `rs1=x5`, `rs2=x0`, `funct3=011`）：

```
= 0x0002B32B
```

> 编码以 `clover-as` 的实现与测试向量为准；本文档中的十六进制为手工推导，进入 M2 后用汇编器回归锁定。
