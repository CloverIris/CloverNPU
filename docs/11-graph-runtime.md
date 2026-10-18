# 11 · 主机图 Runtime（Simulation Backend）

**状态：M6a 已完成（2026-10-18）**。`libclover` 的仿真后端可以在 ISS 上执行 `GEMM → VEC ADD` 两节点图，动态 shape `16x16x16` 与 `17x19x23` 均通过逐元素比较。RTL tile engine 与真实设备命令队列后端仍未接入。

## 1. 运行时接口

`include/clover/clover.h`：

- `clover_open_sim(config, &dev)`：创建 ISS 后端设备。
- `clover_alloc/free`、`clover_buffer_ptr/addr/size`：scratchpad 分配器 + host shadow。
- `clover_copy_h2d/d2h`：host ↔ ISS scratchpad 拷贝。
- `clover_load_image(dev, image, bytes, base, entry, &kernel)`：加载 flat image；CLOV 解析后续实现。
- `clover_graph_create`、`clover_graph_add_op`、`clover_graph_run`、`clover_graph_destroy`。
- `clover_graph_add_op` 是显式张量 API：node 携带 `clover_tensor` 数组，`op` 为 `"gemm"` 或 `"add"`。

## 2. 图执行

`runtime/src/clover_sim.cpp`：

- 设备内嵌 `clover::Iss`，kernel image 加载到 DRAM（GEMM 0x10000000，ADD 0x10020000）。
- scratchpad 分配从 0x10000 起，避开描述符 0x28000 与 args 0x38000。
- 每个 node：
  1. 按 op 写 args 到 0x38000；
  2. `iss.boot(kernel.entry, 0x3FFF0, 0x38000)`；
  3. 运行并检查 `halted && !faulted && exit_code==0`。
- GEMM args：`M,N,K,A,B,C,lda,ldb,ldc`，A/B FP16、C FP32。
- ADD args：元素数、A/B/C 地址；kernel 用 VEC 描述符做 FP32 逐元素加。

## 3. 测试

```bash
make runtime-graph-test
```

`tests/runtime/test_graph_runtime.cpp`：

- 分别编译 `kernels/gemm.c`（entry=`gemm`）和 `kernels/add.c`（entry=`add`）。
- 构造两节点图：`GEMM(A,B) → C`，`ADD(C,bias) → out`。
- 对 `16x16x16` 和 `17x19x23` 两个 shape 执行，比较 `out = C + 1.0`。
- kernel image 只加载一次，shape 变化只改变 tensor 描述/args。

结果：

```text
graph 16x16x16 passed
graph 17x19x23 passed
graph runtime tests passed
```

## 4. 已知限制

- 仿真后端直接调用 ISS，不经过 AXI/命令队列；RTL 端到端仍走 cocotb。
- 图节点使用显式 `clover_tensor`，还没有 JSON/CLOV 图解析、名字解析、自动中间 buffer 分配。
- 只支持 `gemm` 与 `add` 两种 op；没有图优化、融合、多 stream。
- scratchpad 分配器是 bump，不回收；大 tensor 会超出预留窗口。
- 没有 host/device 异步、事件、profiling。
- `clover_load` 仍返回 `UNSUPPORTED`；CLOV 打包在 M5/M6 后续。

## 5. 下一步

1. RTL tile engine，并让同一 GEMM 在 RTL 上 bit-exact。
2. 把 runtime 后端切到 AXI/命令队列（Verilator 或真实 RTL）。
3. CLOV + kernel manifest（entry symbol、tensor ABI）。
4. JSON 图解析、自动中间 buffer、更多 op（ReLU/MatMul/Conv 由 kernel 组合）。
