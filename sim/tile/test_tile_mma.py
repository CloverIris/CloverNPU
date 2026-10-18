import struct
import numpy as np
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer


def f16_bits(value: float) -> int:
    return struct.unpack("<H", np.float16(value).tobytes())[0]


def f32_bits(value: float) -> int:
    return struct.unpack("<I", np.float32(value).tobytes())[0]


def f32(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


async def host_write_line(dut, addr: int, value: int):
    dut.host_addr.value = addr
    dut.host_wdata.value = value
    dut.host_be.value = (1 << 64) - 1
    dut.host_we.value = 1
    await RisingEdge(dut.clk)
    dut.host_we.value = 0
    await Timer(1, unit="ns")


async def host_read_line(dut, addr: int) -> int:
    dut.host_addr.value = addr
    await Timer(1, unit="ns")
    return int(dut.host_rdata.value)


def pack_words(words):
    value = 0
    for i, word in enumerate(words):
        value |= (word & 0xFFFFFFFF) << (32 * i)
    return value


def unpack_words(value, count=16):
    return [(value >> (32 * i)) & 0xFFFFFFFF for i in range(count)]


async def run_case(dut, m, n, k, accumulate=False, c_init=None):
    a_base, b_base, c_base = 0x10000, 0x20000, 0x30000
    desc_addr = 0x0

    a_vals = [[(mi * 7 + ki * 3) % 16 for ki in range(16)] for mi in range(16)]
    b_vals = [[(ki * 5 + ni * 2) % 16 for ni in range(16)] for ki in range(16)]

    for line in range(8):
        words = []
        for pair in range(16):
            idx = line * 32 + pair * 2
            mi, ki = divmod(idx, 16)
            words.append(f16_bits(a_vals[mi][ki]) | (f16_bits(a_vals[mi][ki + 1]) << 16))
        # The loop above is wrong at row boundaries; rebuild explicitly below.
        words = []
        for word_i in range(16):
            e0 = line * 32 + word_i * 2
            e1 = e0 + 1
            m0, k0 = divmod(e0, 16)
            m1, k1 = divmod(e1, 16)
            words.append(f16_bits(a_vals[m0][k0]) | (f16_bits(a_vals[m1][k1]) << 16))
        await host_write_line(dut, a_base + line * 64, pack_words(words))

    for line in range(8):
        words = []
        for word_i in range(16):
            e0 = line * 32 + word_i * 2
            e1 = e0 + 1
            k0, n0 = divmod(e0, 16)
            k1, n1 = divmod(e1, 16)
            words.append(f16_bits(b_vals[k0][n0]) | (f16_bits(b_vals[k1][n1]) << 16))
        await host_write_line(dut, b_base + line * 64, pack_words(words))

    for line in range(16):
        words = [0] * 16
        if accumulate and c_init is not None:
            words = [f32_bits(c_init[line * 16 + i]) for i in range(16)]
        await host_write_line(dut, c_base + line * 64, pack_words(words))

    desc = [
        0x31414D4D,
        0x130 | (1 if accumulate else 0),
        m, n, k, a_base, b_base, c_base,
        32, 32, 64, 0, 0, 0, 0, 0,
    ]
    await host_write_line(dut, desc_addr, pack_words(desc))

    dut.desc_addr.value = desc_addr
    dut.start.value = 1
    await RisingEdge(dut.clk)
    dut.start.value = 0

    for _ in range(200000):
        await RisingEdge(dut.clk)
        if int(dut.done.value):
            break
    else:
        raise AssertionError("MMA timeout")

    assert int(dut.error.value) == 0, f"MMA error {int(dut.error_code.value)}"

    result = []
    for line in range(16):
        value = await host_read_line(dut, c_base + line * 64)
        result.extend(f32(word) for word in unpack_words(value))
    return a_vals, b_vals, result


@cocotb.test()
async def tile_mma_16(dut):
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

    a_vals, b_vals, result = await run_case(dut, 16, 16, 16)
    for mi in range(16):
        for ni in range(16):
            expected = sum(a_vals[mi][ki] * b_vals[ki][ni] for ki in range(16))
            got = result[mi * 16 + ni]
            assert got == expected, f"C[{mi},{ni}] {got} != {expected}"

    a_vals, b_vals, result = await run_case(dut, 8, 8, 8)
    for mi in range(8):
        for ni in range(8):
            expected = sum(a_vals[mi][ki] * b_vals[ki][ni] for ki in range(8))
            got = result[mi * 16 + ni]
            assert got == expected, f"edge C[{mi},{ni}] {got} != {expected}"

    c_init = [10.0] * 256
    a_vals, b_vals, result = await run_case(dut, 16, 16, 16, True, c_init)
    for mi in range(16):
        for ni in range(16):
            expected = 10.0 + sum(a_vals[mi][ki] * b_vals[ki][ni] for ki in range(16))
            got = result[mi * 16 + ni]
            assert got == expected, f"acc C[{mi},{ni}] {got} != {expected}"

    dut._log.info("tile MMA 16x16x16, 8x8x8 and accumulate passed")
