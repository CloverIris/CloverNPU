# 10 · M4b：功能级 Tile Engine 与动态 GEMM

**状态：M4b-ISS 已完成（2026-10-18）**。编译器已支持 tile 内建，ISS 已实现功能级 tile engine，`kernels/gemm.c` 在 `16x16x16` 与 `17x19x23` 两种动态 shape 上通过 bit-exact 参考测试。RTL tile engine 尚未实现。

## 1. 编译器内建

| 内建 | 生成 |
|---|---|
| `__clover_mma(desc)` | `tile.mma a0` |
| `__clover_vec(desc)` | `tile.vec a0` |
| `__clover_dma(desc)` | `tile.dma a0` |
| `__clover_wait(mask)` | `tile.wait a0, a0, zero` |
| `__clover_wait_timeout(mask, t)` | `tile.wait a0, a0, a1` |
| `__clover_fence()` | `tile.fence` |
| `__clover_cycle()` | `li a7,2; ecall` |

`__clover_mma` 等没有头文件依赖；当前 kernel 用 `unsigned int d[16]` 或固定地址描述符即可。`struct`/`#include` 仍在 M5。

## 2. ISS 功能级 Tile Engine

- MMA：读 64 B `MMA1` 描述符，支持 M/N/K <= 16、转置、accumulate、per-tensor/per-channel scale、post-scale/bias、ReLU。
- 输入格式：FP16/BF16/FP8-E4M3/FP8-E5M2/FP4-E2M1 解包到 FP32；FP32 乘加、FP32 累加；输出支持 FP32/FP16/BF16/FP8/FP4（低精度输出用最近码字，尚未做严格 RNE/饱和一致性）。
- VEC：ADD/SUB/MUL/MAX/MIN/RELU/ABS/NEG/FMA/CVT，支持 stride/broadcast（stride=0）。
- DMA：按 2D 行/stride 做字节 copy。
- `tile.wait` 在功能模型中是 no-op；MMA/VEC/DMA 同步执行。
- `Iss::boot(entry, sp, arg)` 与 kernel-return sentinel 已加入，便于 C++ 测试直接启动 kernel。

## 3. GEMM kernel

`kernels/gemm.c`：

- 参数：`M,N,K,A,B,C,lda,ldb,ldc`，A/B 为 FP16，C 为 FP32。
- 16×16×16 block；M/N/K 任意，边界用 `m,n,k` 字段裁剪。
- K 方向第一块 `accumulate=0`，之后为 1。
- 描述符放在固定 64 B 对齐 scratchpad 地址 `0x28000`，避免当前编译器缺少 `aligned(64)`。
- 每个 `(m0,n0)` 结束后 `__clover_wait(1)`。

## 4. 验证

```bash
make tile-gemm-test
```

`tests/tile/test_gemm.cpp`：

1. 用 `cloverc` 编译 `kernels/gemm.c`（`--no-start`，entry=`gemm`）。
2. 组装并加载到 ISS；A/B 以 FP16 写入 scratchpad，C 清零。
3. boot 到 `gemm`，`a0=args`，`sp=0x3FFF0`。
4. 跑 `16x16x16` 和 `17x19x23`。
5. 与整数参考逐元素比较，要求 bit-exact。

结果：

```text
GEMM ISS tests passed (16x16x16, 17x19x23)
```

## 5. 已知限制

- **RTL tile engine 尚未实现**；当前只有 ISS 功能模型。
- 低精度输出（FP8/FP4）的 pack 是最近码字，不是严格 RNE/饱和；输入解包是精确的。
- 描述符必须由软件放在 64 B 对齐地址；编译器还没有 `aligned(64)`/`struct`。
- 没有 per-channel scale 的 GEMM 测试；功能模型已支持。
- `tile.wait` 在 ISS 中同步，没有真实异步队列/延迟。
- GEMM kernel 目前把描述符固定在 `0x28000`；多 kernel 并发需要 runtime 分配描述符。

## 6. 下一步

1. RTL tile engine：描述符 fetch/decode + 16×16 MAC + VEC + CVT/REDUCE。
2. 编译器 `struct`/`#include`/`aligned(64)`，让 kernel 直接包含 `clover_tile.h`。
3. RTL/ISS 对同一 GEMM bit-exact。
4. `libclover` graph runtime 与动态 shape GEMM 端到端。
