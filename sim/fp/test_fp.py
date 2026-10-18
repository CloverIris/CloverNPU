import struct
import numpy as np
import cocotb
from cocotb.triggers import Timer


def f32(bits):
    return np.frombuffer(struct.pack("<I", bits & 0xFFFFFFFF), dtype=np.float32)[0]


def bits(x):
    return struct.unpack("<I", np.float32(x).tobytes())[0]


@cocotb.test()
async def fp32_add_mul_random(dut):
    rng = np.random.default_rng(7)
    cases = []
    for _ in range(2000):
        ea = int(rng.integers(100, 151))
        eb = int(rng.integers(100, 151))
        sa = int(rng.integers(0, 2))
        sb = int(rng.integers(0, 2))
        fa = int(rng.integers(0, 1 << 23))
        fb = int(rng.integers(0, 1 << 23))
        cases.append(((sa << 31) | (ea << 23) | fa,
                      (sb << 31) | (eb << 23) | fb))
    for index, (a, b) in enumerate(cases):
        dut.a.value = a
        dut.b.value = b
        dut.is_mul.value = 0
        await Timer(1, unit="ns")
        got = int(dut.y.value)
        expected = bits(np.float32(f32(a)) + np.float32(f32(b)))
        if got != expected:
            raise AssertionError(
                f"add {index}: {a:#010x}+{b:#010x} got {got:#010x} want {expected:#010x}"
            )
        dut.is_mul.value = 1
        await Timer(1, unit="ns")
        got = int(dut.y.value)
        expected = bits(np.float32(f32(a)) * np.float32(f32(b)))
        if got != expected:
            raise AssertionError(
                f"mul {index}: {a:#010x}*{b:#010x} got {got:#010x} want {expected:#010x}"
            )
    dut._log.info("FP32 add/mul random-normal tests passed")
