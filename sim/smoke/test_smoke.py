"""cocotb smoke test for the CloverNPU toolchain."""

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer


@cocotb.test()
async def add_smoke(dut):
    """Reset the DUT, drive 3 + 4, and check the registered sum."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())

    dut.rst_n.value = 0
    dut.a.value = 0
    dut.b.value = 0

    for _ in range(3):
        await RisingEdge(dut.clk)

    dut.rst_n.value = 1
    dut.a.value = 3
    dut.b.value = 4

    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")

    assert int(dut.y.value) == 7, f"expected 7, got {int(dut.y.value)}"
    dut._log.info("cocotb smoke passed: 3 + 4 = %d", int(dut.y.value))
