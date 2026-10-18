# 05 · 验证与工具链

## 1. 环境

已验证环境：

| 项 | 版本 |
|---|---|
| macOS | 15.7.7 (arm64, Apple M3) |
| Apple clang | 17.0.0 |
| CMake | 4.1.1 |
| Ninja | 1.x（Homebrew） |
| Python | 3.13.5 |
| Verilator | 5.052 |
| Icarus Verilog | 13.0 |
| Yosys | 0.69 |
| cocotb | 2.1.0 |
| numpy | 2.5.x |

RTL 仿真主路径是 **Verilator + cocotb**；Icarus 用于小模块/快速语法检查；Yosys 用于可综合性检查。

## 2. 目录约定

```
sim/
  smoke/         工具链最小闭环
  models/        Python golden model（dtype、MMA、VEC、DMA）
  verilator/     Verilator C++ testbench / 顶层 harness
  cocotb/        按模块划分的 cocotb 测试
  iss/           C++ 指令级模拟器（M2 起）
rtl/
  ...            可综合 RTL
tests/
  ...            跨组件回归、端到端用例、测试向量
```

## 3. 工具链闭环（L0）

`make smoke` 必须完成四件事：

1. **Verilator lint**：对 `sim/smoke/clover_smoke.sv` 做 `--lint-only -Wall`。
2. **Icarus 编译/运行**：用 `iverilog` + `vvp` 跑一个自检 testbench。
3. **cocotb**：用 Verilator 或 Icarus 后端跑一个 Python 用例，验证 DUT 握手。
4. **Yosys**：`read_verilog -sv` + `hierarchy -check` + `proc` + `check`，确认可综合性入口可用。

`scripts/check_tools.sh` 负责版本检查；`scripts/setup.sh` 负责创建 `.venv` 与安装 Python 依赖。

## 4. 验证层级

| 层级 | 对象 | 方法 | 通过标准 |
|---|---|---|---|
| L0 | 工具链 | smoke | 四个工具都跑通 |
| L1 | 单模块 | cocotb 单元测试 | 定向 + 随机，覆盖边界 |
| L2 | 子系统 | core / tile / dma / cp 集成 | 协议断言 + 数据比对 |
| L3 | 顶层 | kernel 端到端 | 与 golden 一致 |
| L4 | 随机/差分 | ISS vs RTL | 指令/描述符随机序列一致 |
| L5 | 可综合性 | Yosys | `check -assert` 无错误 |

### 4.1 L1 单元测试

- **ALU/decoder**：每条指令的边界值、符号、移位量、非法编码。
- **regfile**：x0 恒 0、双读单写、写后读。
- **CSR/trap**：ecall/ebreak/mret、misaligned、illegal、mtvec/mepc。
- **I-cache/D-cache**：hit/miss/evict、line crossing、backpressure、invalidate。
- **scratchpad**：bank 交错、2R1W 冲突、core/tile/DMA 仲裁。
- **MMA PE/array**：M/N/K = 1/15/16、转置、accumulate、scale、各 dtype、NaN/Inf/次正规。
- **vector lane**：所有 op、stride/broadcast、reduce 树、CVT。
- **DMA**：2D 行/stride、非对齐错误、跨 4KB、backpressure。
- **CP**：ring 回绕、completion、error/abort、doorbell 竞态。

### 4.2 L2 子系统

- core + I/D cache + scratchpad：跑整数程序。
- tile + scratchpad：直接注入描述符，比对 tile 输出。
- DMA + scratchpad + AXI slave model：2D copy。
- CP + DMA + core：命令序列。

### 4.3 L3 端到端

- `gemm`：M/N/K 动态，覆盖 1、15、16、17、31、64、127。
- `conv2d`：im2col + GEMM 组合，验证非固化算子。
- `elementwise`：add/mul/relu + 动态 N。
- `softmax`：max/sum/exp 近似 + 除法，验证多原语组合。
- 图：2-3 节点图，动态 batch。

### 4.4 L4 随机/差分

- 随机整数指令序列：ISS 与 RTL 的寄存器/内存终态一致。
- 随机描述符：golden 与 RTL 输出 bit-exact（FP32 路径）。
- 随机 AXI backpressure：协议断言不失败。
- 编译器差分：同一整数 C 程序分别用宿主 clang 与 cloverc 编译，stdout/返回值一致。

## 5. Golden model

`sim/models/` 用 Python + numpy 实现，不依赖 RTL：

- `dtypes.py`：FP16/BF16/FP8-E4M3/FP8-E5M2/FP4-E2M1 ↔ FP32 的位精确转换、RNE、饱和、NaN/Inf 规则。
- `mma.py`：block MMA，按 `m -> n -> k` 顺序累加，使用 `numpy.float32`。
- `vec.py`：逐元素 op、stride/broadcast、reduce 树。
- `dma.py`：2D copy 地址生成。
- `graph.py`：图级参考执行（可选，用 numpy）。

### 5.1 位精确要求

- 每个 FP32 运算后立即 round 到 `numpy.float32`，不得用 Python `float`（binary64）累积。
- MMA 的 k 顺序必须与 RTL 一致：`k = 0..K-1` 串行累加。
- VEC 的 reduce 顺序必须与 RTL 一致：每 16 lane 用平衡二叉树（`((0+1)+(2+3))+...`），chunk 之间串行。
- 低精度输入的乘积在 FP32 中精确；golden 仍按 `fp32_mul` 再 `fp32_add` 实现，以锁定语义。
- 若未来 RTL 改为 FMA，golden 与 ABI 必须同步升级。

### 5.2 容差

- 单元/端到端 FP32 路径：**bit-exact**。
- 与 numpy 高层实现（如 `np.dot`）比较：允许 ULP 误差，仅用于 sanity check，不作为回归标准。
- 低精度输出：按目标格式 bit-exact。

## 6. RTL 仿真

### 6.1 Verilator

- 所有 RTL 通过 `verilator --lint-only -Wall -Wno-fatal`。
- 顶层仿真使用 `--timing`（需要时）、`--trace-fst`（可选）。
- cocotb 后端优先 Verilator；小模块可用 Icarus 快速迭代。
- 禁止在可综合 RTL 中使用 `#delay`、`initial` 初始化、`real`、动态数组。

### 6.2 cocotb

- 每个模块一个 test module，放在 `sim/cocotb/<module>/`。
- 时钟/复位 fixture 统一。
- 断言使用 `cocotb.triggers` 与 `assert`；失败信息包含事务上下文。
- 随机种子记录在日志中，失败可复现。
- 覆盖率：v0.1 用定向 + 随机；后续接 `cocotb-coverage`。

### 6.3 AXI 验证

- 自建 AXI4/AXI4-Lite slave/monitor model（Python 或 SV）。
- 协议检查：握手稳定性、burst 长度、4KB 边界、写响应顺序、ID 映射。
- 随机 backpressure：`awready/wready/arready/rvalid` 随机延迟。

### 6.4 波形

- Verilator 输出 FST/VCD；失败用例默认保留波形。
- 波形文件放 `build/waves/`，不提交仓库。
- `make wave TEST=...` 打开/生成对应波形。

## 7. Yosys 可综合性检查

最低检查：

```bash
yosys -p "
  read_verilog -sv -Irtl rtl/common/clover_pkg.sv rtl/top/clover_npu.sv
  hierarchy -check -top clover_npu
  proc
  opt
  check -assert
  stat
"
```

要求：

- 无 latch 推断（除非有意）。
- 无组合环。
- 无未驱动/多位驱动。
- 无仿真专用构造。
- 全 16x16 配置的综合可能很慢；CI 默认跑 `check` 与 `stat`，完整 `synth` 用缩减参数（`NUM_PE_ROWS=4`）作为门禁。

## 8. 回归入口

```bash
make check-tools     # 版本
make abi-check       # ABI 结构体大小/偏移 static_assert
make smoke           # L0：Verilator + Icarus + cocotb + Yosys
make lint            # Verilator lint
make test            # cocotb smoke
make synth           # Yosys check
make tools           # 构建 cloverc/clover-as/clover-iss/clover-objdump
make m2-test         # 23 个汇编/C 的 ISS 端到端用例
make rtl-lint        # Verilator lint of clover_core
make rtl-synth       # Yosys read/proc/check of clover_core
make rtl-core-test   # 19 个 C 程序的 ISS/RTL 差分
make rtl-mem-lint    # scratchpad / AXI-CSR / core+mem lint
make rtl-mem-synth   # scratchpad / AXI-CSR / core+mem Yosys check
make rtl-mem-test    # scratchpad + AXI-CSR + DMA + CP + NPU end-to-end
make rtl-npu-test    # host -> RUN C kernel -> DMA result -> completion
make tile-gemm-test  # dynamic-shape GEMM on the ISS tile engine
make runtime-graph-test # GEMM -> VEC ADD graph via libclover sim
make rtl-tile-test   # FP32 units + MMA/VEC formats + 16x16x16 MMA
make rtl-gemm-test   # RTL GEMM 16x16x16 + 17x19x23
make rtl-graph-test  # RTL GEMM -> VEC ADD two-node graph
make check           # 以上全部
```

M2、M3、M4a、M4b-ISS、M4c、M4d、M4e、M4f-partial 与 M6a 已完成。当前 RTL 存储/命令测试以定向用例为主；AXI 随机 backpressure、ring 回绕 1000 条、I/D-cache 一致性尚未覆盖；tile engine 尚无 RTL。

## 9. 失败处理约定

- 仿真失败必须保留：随机种子、命令行、波形、日志。
- RTL 断言失败优先于数据比对失败报告。
- golden 与 RTL 不一致时，先判定是规范歧义还是实现错误；规范歧义必须回到文档修复，不能只改测试。
- 任何 ABI/描述符布局变更必须同时更新：文档、头文件、golden、RTL、测试向量。

## 10. 性能与可观测性

- 全局周期计数、每引擎 busy 周期、MMA/VEC/DMA 计数。
- ISS 支持指令 trace、描述符 trace。
- RTL 支持可选的 AXI transaction trace（`ifdef CLOVER_TRACE_AXI`）。
- 端到端 benchmark 记录：周期、DMA 字节、MMA 次数、有效 MAC 利用率。
- 性能数字只用于相对比较，不作为流片指标。
