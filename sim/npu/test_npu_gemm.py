"""Full RTL GEMM: CP -> DMA -> RUN -> core tile.mma -> DMA -> completion."""

import struct
from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer

CODE_DRAM = 0x4000
RESULT_DRAM = 0x5000
ARGS_DRAM = 0x6000
A_DRAM = 0x8000
B_DRAM = 0x9000
CODE_SP = 0x0
ARGS_SP = 0x38000
A_SP = 0x10000
B_SP = 0x20000
C_SP = 0x30000


async def axi_write(dut, addr: int, data: int, strb: int = 0xF) -> None:
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


async def host_write(dut, offset: int, value: int):
    dut.host_addr.value = offset
    dut.host_wdata.value = value
    dut.host_wstrb.value = 0xFFFF
    dut.host_we.value = 1
    await RisingEdge(dut.clk)
    dut.host_we.value = 0


async def host_read(dut, offset: int) -> int:
    dut.host_addr.value = offset
    await Timer(1, unit="ns")
    return int(dut.host_rdata.value)


def f16(value: float) -> int:
    return struct.unpack("<H", struct.pack("<e", value))[0]


def f32(bits: int) -> float:
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


async def write_command(dut, index: int, words: list[int]):
    for beat in range(4):
        value = 0
        for i in range(4):
            value |= (words[beat * 4 + i] & 0xFFFFFFFF) << (32 * i)
        await host_write(dut, index * 64 + beat * 16, value)


async def write_blob(dut, offset: int, data: bytes):
    data += b"\x00" * ((16 - len(data) % 16) % 16)
    for i in range(0, len(data), 16):
        await host_write(dut, offset + i, int.from_bytes(data[i:i + 16], "little"))
    return len(data)


@cocotb.test()
async def npu_rtl_gemm(dut):
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
    dut.cycle_count.value = 0x1234
    dut.host_we.value = 0
    dut.host_addr.value = 0
    dut.host_wdata.value = 0
    dut.host_wstrb.value = 0

    dut.rst_n.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)

    m = int(cocotb.plusargs.get("SHAPE_M", "17"))
    n = int(cocotb.plusargs.get("SHAPE_N", "19"))
    k = int(cocotb.plusargs.get("SHAPE_K", "23"))
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

    kernel = Path(cocotb.plusargs["KERNEL_BIN"]).read_bytes()
    entry = int(cocotb.plusargs["KERNEL_ENTRY"], 0)
    code_pad = await write_blob(dut, CODE_DRAM, kernel)
    a_pad = await write_blob(dut, A_DRAM, bytes(a_bytes))
    b_pad = await write_blob(dut, B_DRAM, bytes(b_bytes))

    args = [m, n, k, A_SP, B_SP, C_SP, k * 2, n * 2, n * 4]
    args_bytes = b"".join(struct.pack("<I", v) for v in args)
    await write_blob(dut, ARGS_DRAM, args_bytes)

    c_pad = ((m * n * 4 + 15) // 16) * 16
    commands = [
        build_command(opcode=2, src=0x10000000 + CODE_DRAM, dst=CODE_SP,
                      dim0=code_pad, dim1=1),
        build_command(opcode=2, src=0x10000000 + ARGS_DRAM, dst=ARGS_SP,
                      dim0=64, dim1=1),
        build_command(opcode=2, src=0x10000000 + A_DRAM, dst=A_SP,
                      dim0=a_pad, dim1=1),
        build_command(opcode=2, src=0x10000000 + B_DRAM, dst=B_SP,
                      dim0=b_pad, dim1=1),
        build_command(opcode=3, src=entry, dst=ARGS_SP, dim0=0x0003FFF0, dim1=1),
        build_command(opcode=2, flags=1, src=C_SP, dst=0x10000000 + RESULT_DRAM,
                      dim0=c_pad, dim1=1),
        build_command(opcode=5, seq=1),
        build_command(opcode=8, seq=2),
    ]
    for index, command in enumerate(commands):
        await write_command(dut, index, command)

    await axi_write(dut, 0x00C, 1)           # CONTROL.ENABLE
    await axi_write(dut, 0x014, 1)           # IRQ_ENABLE
    await axi_write(dut, 0x018, 0x10000000)  # QUEUE_BASE
    await axi_write(dut, 0x01C, 16)          # QUEUE_SIZE
    await axi_write(dut, 0x028, 0x10001000)  # COMPLETE_BASE
    await axi_write(dut, 0x02C, 16)          # COMPLETE_SIZE
    await axi_write(dut, 0x034, 0)           # COMPLETE_TAIL
    await axi_write(dut, 0x024, len(commands))

    for _ in range(300000):
        await RisingEdge(dut.clk)
        if (int(dut.queue_head.value) == len(commands) and
                int(dut.completion_head.value) == 2 and
                not int(dut.busy.value)):
            break
    else:
        raise AssertionError(
            f"GEMM stalled: qhead={int(dut.queue_head.value)} "
            f"chead={int(dut.completion_head.value)} busy={int(dut.busy.value)} "
            f"tile_busy={int(dut.tile_busy.value)} "
            f"tile_error={int(dut.tile_error.value)} "
            f"core_active={int(dut.core_active.value)} "
            f"last_exit={int(dut.last_exit_code.value)}"
        )

    assert int(dut.tile_error.value) == 0, f"tile error {int(dut.tile_error_code.value)}"
    assert int(dut.last_exit_code.value) == 0, "kernel exit status"

    result = []
    for offset in range(0, m * n * 4, 16):
        value = await host_read(dut, RESULT_DRAM + offset)
        for i in range(4):
            if len(result) < m * n:
                result.append(f32((value >> (32 * i)) & 0xFFFFFFFF))

    for mi in range(m):
        for ni in range(n):
            expected = sum(a_vals[mi][ki] * b_vals[ki][ni] for ki in range(k))
            got = result[mi * n + ni]
            assert got == expected, f"C[{mi},{ni}] {got} != {expected}"

    dut._log.info(f"RTL GEMM {m}x{n}x{k} passed")
