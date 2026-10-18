"""RTL graph: GEMM -> VEC ADD through the command queue."""

import struct
from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer

GEMM_CODE_DRAM = 0x4000
ADD_CODE_DRAM = 0x5000
RESULT_DRAM = 0x6000
GEMM_ARGS_DRAM = 0x7000
A_DRAM = 0x9000
B_DRAM = 0xA000
BIAS_DRAM = 0xB000
ADD_ARGS_DRAM = 0xC000

GEMM_CODE_SP = 0x0
ADD_CODE_SP = 0x1000
A_SP = 0x10000
B_SP = 0x20000
C_SP = 0x30000
BIAS_SP = 0x34000
OUT_SP = 0x35000
ARGS_SP = 0x38000


async def axi_write(dut, addr, data, strb=0xF):
    dut.s_awaddr.value = addr
    dut.s_awvalid.value = 1
    dut.s_wdata.value = data
    dut.s_wstrb.value = strb
    dut.s_wvalid.value = 1
    while not (int(dut.s_awready.value) and int(dut.s_wready.value)):
        await RisingEdge(dut.clk)
    await RisingEdge(dut.clk)
    dut.s_awvalid.value = 0
    dut.s_wvalid.value = 0
    while not int(dut.s_bvalid.value):
        await RisingEdge(dut.clk)
    dut.s_bready.value = 1
    await RisingEdge(dut.clk)
    dut.s_bready.value = 0


async def host_write(dut, offset, value):
    dut.host_addr.value = offset
    dut.host_wdata.value = value
    dut.host_wstrb.value = 0xFFFF
    dut.host_we.value = 1
    await RisingEdge(dut.clk)
    dut.host_we.value = 0


async def host_read(dut, offset):
    dut.host_addr.value = offset
    await Timer(1, unit="ns")
    return int(dut.host_rdata.value)


def f16(value):
    return struct.unpack("<H", struct.pack("<e", value))[0]


def f32(bits):
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def build_command(*, opcode, flags=0, src=0, dst=0, dim0=0, dim1=0,
                  stride0=0, stride1=0, seq=0):
    return [
        opcode & 0xFFFFFFFF, flags & 0xFFFFFFFF,
        src & 0xFFFFFFFF, dst & 0xFFFFFFFF,
        dim0 & 0xFFFFFFFF, dim1 & 0xFFFFFFFF, 0,
        stride0 & 0xFFFFFFFF, stride1 & 0xFFFFFFFF,
        0, 0, seq & 0xFFFFFFFF, 0, 0, 0, 0,
    ]


async def write_command(dut, index, words):
    for beat in range(4):
        value = 0
        for i in range(4):
            value |= (words[beat * 4 + i] & 0xFFFFFFFF) << (32 * i)
        await host_write(dut, index * 64 + beat * 16, value)


async def write_blob(dut, offset, data):
    data += b"\x00" * ((16 - len(data) % 16) % 16)
    for i in range(0, len(data), 16):
        await host_write(dut, offset + i, int.from_bytes(data[i:i + 16], "little"))
    return len(data)


@cocotb.test()
async def npu_rtl_graph(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())

    dut.s_awaddr.value = 0
    dut.s_awvalid.value = 0
    dut.s_wdata.value = 0
    dut.s_wstrb.value = 0
    dut.s_wvalid.value = 0
    dut.s_bready.value = 0
    dut.s_araddr.value = 0
    dut.s_arvalid.value = 0
    dut.s_rready.value = 0
    dut.cycle_count.value = 0x2222
    dut.host_we.value = 0
    dut.host_addr.value = 0
    dut.host_wdata.value = 0
    dut.host_wstrb.value = 0

    dut.rst_n.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)

    m, n, k = 17, 19, 23
    a_vals = [[(mi * 7 + ki * 3) % 16 for ki in range(k)] for mi in range(m)]
    b_vals = [[(ki * 5 + ni * 2) % 16 for ni in range(n)] for ki in range(k)]

    a_bytes = bytearray()
    for mi in range(m):
        for ki in range(k):
            a_bytes += struct.pack("<H", f16(float(a_vals[mi][ki])))
    b_bytes = bytearray()
    for ki in range(k):
        for ni in range(n):
            b_bytes += struct.pack("<H", f16(float(b_vals[ki][ni])))

    gemm_kernel = Path(cocotb.plusargs["KERNEL_BIN"]).read_bytes()
    add_kernel = Path(cocotb.plusargs["ADD_BIN"]).read_bytes()
    gemm_entry = int(cocotb.plusargs["KERNEL_ENTRY"], 0)
    add_entry = int(cocotb.plusargs["ADD_ENTRY"], 0)

    gemm_code_pad = await write_blob(dut, GEMM_CODE_DRAM, gemm_kernel)
    add_code_pad = await write_blob(dut, ADD_CODE_DRAM, add_kernel)
    a_pad = await write_blob(dut, A_DRAM, bytes(a_bytes))
    b_pad = await write_blob(dut, B_DRAM, bytes(b_bytes))

    c_count = m * n
    c_pad = ((c_count * 4 + 15) // 16) * 16
    bias_bytes = struct.pack("<I", 0x3F800000) * c_count
    await write_blob(dut, BIAS_DRAM, bias_bytes)

    gemm_args = [m, n, k, A_SP, B_SP, C_SP, k * 2, n * 2, n * 4]
    await write_blob(dut, GEMM_ARGS_DRAM,
                     b"".join(struct.pack("<I", v) for v in gemm_args))
    add_args = [c_count, C_SP, BIAS_SP, OUT_SP]
    await write_blob(dut, ADD_ARGS_DRAM,
                     b"".join(struct.pack("<I", v) for v in add_args))

    commands = [
        build_command(opcode=2, src=0x10000000 + GEMM_CODE_DRAM, dst=GEMM_CODE_SP,
                      dim0=gemm_code_pad, dim1=1),
        build_command(opcode=2, src=0x10000000 + ADD_CODE_DRAM, dst=ADD_CODE_SP,
                      dim0=add_code_pad, dim1=1),
        build_command(opcode=2, src=0x10000000 + GEMM_ARGS_DRAM, dst=ARGS_SP,
                      dim0=64, dim1=1),
        build_command(opcode=2, src=0x10000000 + A_DRAM, dst=A_SP,
                      dim0=a_pad, dim1=1),
        build_command(opcode=2, src=0x10000000 + B_DRAM, dst=B_SP,
                      dim0=b_pad, dim1=1),
        build_command(opcode=3, src=gemm_entry, dst=ARGS_SP,
                      dim0=0x0003FFF0, dim1=1),
        build_command(opcode=2, src=0x10000000 + BIAS_DRAM, dst=BIAS_SP,
                      dim0=c_pad, dim1=1),
        build_command(opcode=2, src=0x10000000 + ADD_ARGS_DRAM, dst=ARGS_SP,
                      dim0=64, dim1=1),
        build_command(opcode=3, src=add_entry, dst=ARGS_SP,
                      dim0=0x0003FFF0, dim1=2),
        build_command(opcode=2, flags=1, src=OUT_SP, dst=0x10000000 + RESULT_DRAM,
                      dim0=c_pad, dim1=1),
        build_command(opcode=5, seq=1),
        build_command(opcode=8, seq=2),
    ]
    for index, command in enumerate(commands):
        await write_command(dut, index, command)

    await axi_write(dut, 0x00C, 1)
    await axi_write(dut, 0x014, 1)
    await axi_write(dut, 0x018, 0x10000000)
    await axi_write(dut, 0x01C, 16)
    await axi_write(dut, 0x028, 0x10001000)
    await axi_write(dut, 0x02C, 16)
    await axi_write(dut, 0x034, 0)
    await axi_write(dut, 0x024, len(commands))

    for _ in range(500000):
        await RisingEdge(dut.clk)
        if (int(dut.queue_head.value) == len(commands) and
                int(dut.completion_head.value) == 2 and
                not int(dut.busy.value)):
            break
    else:
        raise AssertionError(
            f"graph stalled: qhead={int(dut.queue_head.value)} "
            f"chead={int(dut.completion_head.value)} busy={int(dut.busy.value)} "
            f"tile_busy={int(dut.tile_busy.value)} "
            f"tile_error={int(dut.tile_error.value)} "
            f"core_active={int(dut.core_active.value)} "
            f"exit={int(dut.last_exit_code.value)}"
        )

    assert int(dut.tile_error.value) == 0, f"tile error {int(dut.tile_error_code.value)}"
    assert int(dut.last_exit_code.value) == 0, "add kernel exit"

    result = []
    for offset in range(0, c_count * 4, 16):
        value = await host_read(dut, RESULT_DRAM + offset)
        for i in range(4):
            if len(result) < c_count:
                result.append(f32((value >> (32 * i)) & 0xFFFFFFFF))

    for mi in range(m):
        for ni in range(n):
            expected = 1.0 + sum(a_vals[mi][ki] * b_vals[ki][ni] for ki in range(k))
            got = result[mi * n + ni]
            assert got == expected, f"C[{mi},{ni}] {got} != {expected}"

    dut._log.info("RTL graph GEMM -> VEC ADD 17x19x23 passed")
