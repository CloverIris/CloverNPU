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


VALS = [0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0]
FP4_CODE = {0.0: 0, 0.5: 1, 1.0: 2, 1.5: 3, 2.0: 4, 3.0: 5, 4.0: 6, 6.0: 7}
E4M3_CODE = {0.0: 0x00, 0.5: 0x30, 1.0: 0x38, 1.5: 0x3C,
             2.0: 0x40, 3.0: 0x44, 4.0: 0x48, 6.0: 0x4C}
E5M2_CODE = {0.0: 0x00, 0.5: 0x38, 1.0: 0x3C, 1.5: 0x3E,
             2.0: 0x40, 3.0: 0x42, 4.0: 0x44, 6.0: 0x46}
FP16_CODE = {v: struct.unpack("<H", struct.pack("<e", v))[0] for v in VALS}
BF16_CODE = {v: struct.unpack("<I", np.float32(v).tobytes())[0] >> 16 for v in VALS}


async def run_format_vec(dut, src_type, code_map, elem_size):
    count = 256
    a_base, b_base, c_base = 0x10000, 0x20000, 0x30000
    a_vals = [VALS[(i * 3 + 1) % 8] for i in range(count)]
    b_vals = [VALS[(i * 5 + 2) % 8] for i in range(count)]

    def encode(values):
        out = bytearray()
        for value in values:
            code = code_map[value]
            out += struct.pack("<H", code) if elem_size == 2 else bytes([code])
        return out

    for data, base in ((encode(a_vals), a_base), (encode(b_vals), b_base)):
        data += b"\x00" * ((64 - len(data) % 64) % 64)
        for line in range(0, len(data), 64):
            await write_line(dut, base + line, int.from_bytes(data[line:line + 64], "little"))
    for line in range(16):
        await write_line(dut, c_base + line * 64, 0)

    desc = [
        0x31434556, 0, count,
        a_base, b_base, c_base,
        elem_size, elem_size, 4,
        src_type, 0, 0, 0, 0x3F800000, 0, 0,
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
        raise AssertionError("VEC format timeout")
    assert int(dut.error.value) == 0, f"VEC format error {int(dut.error_code.value)}"

    result = []
    for line in range(16):
        value = await read_line(dut, c_base + line * 64)
        result.extend(f32((value >> (32 * i)) & 0xFFFFFFFF) for i in range(16))
    for i in range(count):
        assert result[i] == a_vals[i] + b_vals[i], \
            f"VEC type {src_type} [{i}] {result[i]} != {a_vals[i] + b_vals[i]}"


@cocotb.test()
async def tile_vec_formats(dut):
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

    await run_format_vec(dut, 1, FP16_CODE, 2)
    await run_format_vec(dut, 2, BF16_CODE, 2)
    await run_format_vec(dut, 5, FP4_CODE, 1)
    await run_format_vec(dut, 3, E4M3_CODE, 1)
    await run_format_vec(dut, 4, E5M2_CODE, 1)
    dut._log.info("VEC FP16/BF16/FP4/E4M3/E5M2 ADD passed")
