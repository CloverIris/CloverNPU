# 12 · RTL Tile Engine：FP32 单元与串行 MMA

**状态：M4c-MMA + M4c-Integration 已完成（2026-10-18）**。可综合的 FP32 add/mul、FP16/BF16 解包和串行 16×16×16 MMA 已实现；`16x16x16`、`8x8x8`、accumulate 三组 block 测试通过，Yosys 通过；MMA 已接入 core/NPU，完整 RTL GEMM `17x19x23` 通过。还没有 RTL VEC。

## 1. FP32 数据通路

`rtl/fp/clover_fp_pkg.sv`：

- `clover_fp32_add`：24-bit 有效数 + 3 个 guard/round/sticky 位，RNE 舍入，处理 NaN/Inf/zero/符号。
- `clover_fp32_mul`：24×24 有效数乘法、归一化、RNE。
- `clover_fp16_to_fp32`、`clover_bf16_to_fp32`。
- `clover_fp32_pack`：溢出饱和到 Inf，下溢/次正规结果 flush 到 0。

验证：`sim/fp/test_fp.py` 用 2000 组随机 normal FP32 向量与 numpy float32 逐位比较 add/mul。

```bash
make rtl-tile-test   # 同时跑 fp32 与 tile_mma
```

## 2. MMA 引擎

`rtl/tile/clover_tile_mma.sv`：

- 64 B `MMA1` 描述符 fetch（16 个 32-bit 字）。
- 支持 M/N/K ≤ 16、accumulate、transpose A/B、ReLU。
- A/B 支持 FP16（type=1）与 BF16（type=2）；C 固定 FP32（type=0）。
- 串行 1 MAC/cycle：A 从 scratchpad 读端口 A，B 从端口 B；C 通过写端口逐元素写回。
- `busy/done/error/error_code`；错误码：1 magic、2 范围、3 dtype。
- 描述符 fetch 使用端口 A，C accumulate 使用端口 B。

## 3. 测试

`sim/tile/test_tile_mma.py`：

- 16×16×16 FP16 输入，C FP32，256 个元素逐元素比较。
- 8×8×8 边界（描述符 m/n/k=8）。
- accumulate：C 预置 10.0，验证 `C += A*B`。
- 数据布局：A/B 行主序 FP16，stride 32 B；C FP32，stride 64 B。

结果：

```text
PASS fp32
PASS tile_mma
```

## 4. 已知限制

- 只实现 MMA；VEC/CVT/REDUCE/DMA 仍在 ISS 功能模型。
- FP8-E4M3/E5M2/FP4 的 RTL 解包尚未实现。
- 次正规输入/输出与结果 flush 到 0；尚未做完整 IEEE 次正规舍入。
- 单 MAC/cycle，16×16×16 约 4100 cycles；没有 16×16 阵列。
- 没有 per-tensor/per-channel scale、post-scale/bias 的 RTL（ISS 已支持）。
- MMA 已接入 `clover_npu_mem` / core；完整 RTL GEMM 见 [RTL GEMM](13-rtl-gemm.md)。
- 还没有 RTL VEC；`GEMM → ADD` 图仍只在 ISS 图 runtime 中。

## 5. 下一步

1. 补 RTL VEC 引擎，让 RTL 也能跑 `GEMM → ADD` 图。
2. FP8/FP4 RTL 解包与 scale。
3. 多 shape RTL GEMM 回归（16×16×16 等）。
4. 图 runtime 的 RTL/Verilator 后端。
