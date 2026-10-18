"""Cocotb test for the Clover-S scalar core."""

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge


@cocotb.test()
async def run_program(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())

    dut.rst_n.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1

    expected_exit = int(cocotb.plusargs.get("EXPECT_EXIT", "0"))
    expected_stdout = bytes.fromhex(
        cocotb.plusargs.get("EXPECT_STDOUT_HEX", "")
    ).decode()

    uart = []
    for _ in range(2_000_000):
        await RisingEdge(dut.clk)
        if int(dut.uart_valid.value):
            uart.append(chr(int(dut.uart_data.value)))
        if int(dut.halted.value):
            break
    else:
        assert False, "core did not halt before the cycle limit"

    actual_stdout = "".join(uart)
    dut._log.info(
        "RTL halt: exit=%d reason=%d pc=0x%x stdout=%r cycles=%d",
        int(dut.exit_code.value),
        int(dut.halt_reason.value),
        int(dut.pc_out.value),
        actual_stdout,
        int(dut.cycle_count.value),
    )
    assert int(dut.exit_code.value) == expected_exit, (
        f"exit code {int(dut.exit_code.value)} != {expected_exit}"
    )
    assert actual_stdout == expected_stdout, (
        f"uart {actual_stdout!r} != {expected_stdout!r}"
    )
    dut._log.info(
        "RTL core: exit=%d reason=%d pc=0x%x stdout=%r cycles=%d",
        int(dut.exit_code.value),
        int(dut.halt_reason.value),
        int(dut.pc_out.value),
        actual_stdout,
        int(dut.cycle_count.value),
    )
