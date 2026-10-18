# 14 · RTL 两节点图：GEMM → VEC ADD

**状态：M4d 已完成（2026-10-18）**。RTL VEC 引擎已实现并接入 NPU top；同一命令队列里连续两个 `RUN`：

```text
RUN gemm  -> core tile.mma -> C (FP32)
RUN add   -> core tile.vec -> out = C + bias
```

测试 shape `M=17,N=19,K=23`，结果逐元素与参考一致。

## 1. RTL VEC

`rtl/tile/clover_tile_vec.sv`：

- `VEC1` 描述符 fetch/锁存/校验；
- 1 element/cycle；
- 输入 FP32/FP16/BF16/FP8-E4M3/FP8-E5M2/FP4-E2M1，输出 FP32；
- ADD/SUB/MUL/MAX/MIN/RELU/ABS/NEG/FMA/CVT（CVT 目前是直通）；
- `post_scale` 已应用；FMA 使用 `post_bias`；A/B scale 仍未实现。
- count 32-bit，stride 字节；写回 FP32；
- busy/done/error/error_code。

独立测试 `sim/tile/test_tile_vec.py`：256 lanes 的 ADD/MUL/RELU 通过。

## 2. NPU 集成

`rtl/top/clover_npu_mem.sv`：

- `tile_op=0` 启动 MMA，`tile_op=1` 启动 VEC；
- `tile_busy = mma_busy | vec_busy`，core 的 `tile.wait` 等待合并 busy；
- scratchpad 端口优先级仍为 tile > core > DMA；
- tile error/code 按当前引擎选择。

## 3. 图测试

`tests/rtl/run_npu_graph_tests.py`：

1. gemm 编译到 SP 0，entry=`gemm`；
2. add 编译到 SP 0x1000，entry=`add`；
3. 12 条命令：搬 gemm/add code、gemm args、A/B、RUN gemm、bias、add args、RUN add、结果 DMA、SIGNAL、END；
4. 等待 queue head 12、completion 2、CP idle；
5. 检查 tile error 0、最后一个 kernel exit 0；
6. 验证 `out = A@B + 1.0`。

结果：

```text
PASS npu_rtl_graph
```

## 4. 已知限制

- VEC 只支持 FP32；MMA 已支持 FP4–FP16。
- VEC 已应用 per-tensor A/B scale 与 `post_scale`；MMA 支持 per-channel scale；VEC per-channel 未实现。
- 图是“两个 RUN 命令”的固定序列，不是通用 JSON/CLOV 图解析。
- 两个 kernel 的 code 必须手工放在不同 scratchpad 地址（gemm 0，add 0x1000）。
- 没有自动中间 buffer、形状推断或图优化；这些仍在 `libclover` ISS 后端。
- 没有 RTL/ISS co-sim；测试用独立参考。
- 没有 cache/AXI protocol checker。

## 5. 下一步

1. VEC/MMA 低精度 C 输出（FP16/BF16/FP8/FP4）。
2. per-channel scale 与完整 post ops。
3. 图 runtime 的 RTL/Verilator 后端（复用同一 CP 队列）。
4. CLOV/manifest：把 entry symbol、tensor 描述、code/data 段打包。
5. 编译器 `struct/#include/aligned(64)`，直接包含 `clover_tile.h`。
