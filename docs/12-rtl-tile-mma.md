# 12 · RTL Tile Engine：FP32 单元与串行 MMA

**状态：M4c + M4e-formats 已完成（2026-10-18）**。可综合 FP32 add/mul、FP16/BF16/FP8-E4M3/FP8-E5M2/FP4-E2M1 解包和串行 16×16×16 MMA；`16x16x16`、`8x8x8`、accumulate、FP4/FP8 格式测试通过；MMA 已接入 core/NPU，RTL GEMM `16x16x16` 与 `17x19x23` 通过。RTL VEC 见 [RTL 图](14-rtl-graph.md)。

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
- A/B 支持 FP32（0）、FP16（1）、BF16（2）、FP8-E4M3（3）、FP8-E5M2（4）、FP4-E2M1（5）；元素宽度 4/2/1 字节；C 固定 FP32。FP4 当前按 1 byte/element 存储。
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

- MMA 与 VEC 输入都已支持 FP4–FP16；两者的 C 输出仍固定 FP32。
- FP4/FP8 已支持 A/B 解包；C 仍是 FP32，低精度 C 输出未实现。
- 次正规输入/输出与结果 flush 到 0；尚未做完整 IEEE 次正规舍入。
- 单 MAC/cycle，16×16×16 约 4100 cycles；没有 16×16 阵列。
- 没有 per-tensor/per-channel scale、post-scale/bias 的 RTL（ISS 已支持）。
- MMA 已接入 `clover_npu_mem` / core；完整 RTL GEMM 见 [RTL GEMM](13-rtl-gemm.md)。
- 还没有 RTL VEC；`GEMM → ADD` 图仍只在 ISS 图 runtime 中。

## 5. 下一步

1. per-channel scale 与低精度 C 输出。
2. 图 runtime 的 RTL/Verilator 后端。
3. CLOV/manifest 与编译器 `struct/#include`。
4. 多 shape/scale 的 RTL 回归扩展。
