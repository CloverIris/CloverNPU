"""cocotb tests for the 256 KiB banked scratchpad."""

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer


def line_pattern(line: int) -> int:
    value = 0
    for i in range(16):
        value |= (((line * 16 + i) * 0x01010101) & 0xFFFFFFFF) << (32 * i)
    return value


async def write_line(dut, addr: int, data: int, be: int = (1 << 64) - 1):
    dut.w_req.value = 1
    dut.w_addr.value = addr
    dut.w_data.value = data
    dut.w_be.value = be
    await RisingEdge(dut.clk)
    dut.w_req.value = 0


async def read_a(dut, addr: int) -> int:
    dut.a_req.value = 1
    dut.a_addr.value = addr
    await Timer(1, unit="ns")
    value = int(dut.a_rdata.value)
    dut.a_req.value = 0
    return value


async def read_b(dut, addr: int) -> int:
    dut.b_req.value = 1
    dut.b_addr.value = addr
    await Timer(1, unit="ns")
    value = int(dut.b_rdata.value)
    dut.b_req.value = 0
    return value


@cocotb.test()
async def scratchpad_basic(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())

    dut.a_req.value = 0
    dut.a_addr.value = 0
    dut.b_req.value = 0
    dut.b_addr.value = 0
    dut.w_req.value = 0
    dut.w_addr.value = 0
    dut.w_data.value = 0
    dut.w_be.value = 0

    for line in (0, 1, 4095):
        pattern = line_pattern(line)
        await write_line(dut, line * 64, pattern)
        got_a = await read_a(dut, line * 64)
        got_b = await read_b(dut, line * 64)
        assert got_a == pattern, f"A line {line}: {got_a:#x} != {pattern:#x}"
        assert got_b == pattern, f"B line {line}: {got_b:#x} != {pattern:#x}"

    # Byte enables: only bytes 0 and 2 of lane 0 change.
    before = line_pattern(1)
    lane0 = before & 0xFFFFFFFF
    new_lane0 = 0xDEADBEEF
    await write_line(dut, 64, (before & ~0xFFFFFFFF) | new_lane0, 0b0000_0000_0000_0000_0000_0000_0000_0101)
    after = await read_a(dut, 64)
    expected_lane0 = (lane0 & ~0x00FF00FF) | (new_lane0 & 0x00FF00FF)
    assert (after & 0xFFFFFFFF) == expected_lane0, (
        f"byte enable: {after & 0xFFFFFFFF:#x} != {expected_lane0:#x}"
    )
    assert (after >> 32) == (before >> 32), "other lanes changed"

    # Two read ports can access different lines in the same cycle.
    dut.a_req.value = 1
    dut.a_addr.value = 0
    dut.b_req.value = 1
    dut.b_addr.value = 64
    await Timer(1, unit="ns")
    assert int(dut.a_rdata.value) == line_pattern(0)
    assert int(dut.b_rdata.value) == after
    dut.a_req.value = 0
    dut.b_req.value = 0

    dut._log.info("scratchpad tests passed")
