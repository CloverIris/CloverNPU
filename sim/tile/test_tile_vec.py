import struct
import numpy as np
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer


def f32_bits(value):
    return struct.unpack("<I", np.float32(value).tobytes())[0]


def f32(bits):
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


async def write_line(dut, addr, value):
    dut.host_addr.value = addr
    dut.host_wdata.value = value
    dut.host_be.value = (1 << 64) - 1
    dut.host_we.value = 1
    await RisingEdge(dut.clk)
    dut.host_we.value = 0
    await Timer(1, unit="ns")


async def read_line(dut, addr):
    dut.host_addr.value = addr
    await Timer(1, unit="ns")
    return int(dut.host_rdata.value)


def pack_words(words):
    return sum((w & 0xFFFFFFFF) << (32 * i) for i, w in enumerate(words))


async def run_vec(dut, a_vals, b_vals, op, relu=False):
    count = len(a_vals)
    a_base, b_base, c_base = 0x10000, 0x20000, 0x30000
    lines = (count + 15) // 16
    for line in range(lines):
        wa = []
        wb = []
        for i in range(16):
            idx = line * 16 + i
            wa.append(f32_bits(a_vals[idx]) if idx < count else 0)
            wb.append(f32_bits(b_vals[idx]) if idx < count else 0)
        await write_line(dut, a_base + line * 64, pack_words(wa))
        await write_line(dut, b_base + line * 64, pack_words(wb))
        await write_line(dut, c_base + line * 64, 0)

    desc = [
        0x31434556,
        (op & 0xF) | (0x20 if relu else 0),
        count,
        a_base, b_base, c_base,
        4, 4, 4, 0, 0, 0, 0,
        0x3F800000, 0, 0,
    ]
    await write_line(dut, 0, pack_words(desc))
    dut.desc_addr.value = 0
    dut.start.value = 1
    await RisingEdge(dut.clk)
    dut.start.value = 0

    for _ in range(10000):
        await RisingEdge(dut.clk)
        if int(dut.done.value):
            break
    else:
        raise AssertionError("VEC timeout")
    assert int(dut.error.value) == 0, f"VEC error {int(dut.error_code.value)}"

    result = []
    for line in range(lines):
        value = await read_line(dut, c_base + line * 64)
        result.extend(f32((value >> (32 * i)) & 0xFFFFFFFF) for i in range(16))
    return result[:count]


@cocotb.test()
async def tile_vec_fp32(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    dut.host_we.value = 0
    dut.host_addr.value = 0
    dut.host_wdata.value = 0
    dut.host_be.value = 0
    dut.start.value = 0
    dut.desc_addr.value = 0
    dut.rst_n.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)

    rng = np.random.default_rng(4)
    a = [float(v) for v in rng.integers(-100, 100, 256)]
    b = [float(v) for v in rng.integers(-100, 100, 256)]

    result = await run_vec(dut, a, b, 0)
    for i in range(256):
        assert result[i] == a[i] + b[i], f"add {i}"

    result = await run_vec(dut, a, b, 2)
    for i in range(256):
        assert result[i] == np.float32(a[i]) * np.float32(b[i]), f"mul {i}"

    result = await run_vec(dut, a, b, 5)
    for i in range(256):
        assert result[i] == max(a[i], 0.0), f"relu {i}"

    dut._log.info("VEC FP32 ADD/MUL/RELU 256 lanes passed")
