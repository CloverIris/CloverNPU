"""Run a flat image from scratchpad on the 5-stage core."""

from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge


async def load_image(dut, data: bytes) -> None:
    data = data + b"\x00" * ((64 - len(data) % 64) % 64)
    for line in range(0, len(data), 64):
        chunk = data[line : line + 64]
        value = int.from_bytes(chunk, "little")
        dut.loader_addr.value = line
        dut.loader_data.value = value
        dut.loader_be.value = (1 << 64) - 1
        dut.loader_we.value = 1
        await RisingEdge(dut.clk)
        dut.loader_we.value = 0
        await RisingEdge(dut.clk)


@cocotb.test()
async def run_from_scratchpad(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())

    dut.loader_we.value = 0
    dut.loader_addr.value = 0
    dut.loader_data.value = 0
    dut.loader_be.value = 0
    dut.rst_n.value = 0

    for _ in range(2):
        await RisingEdge(dut.clk)

    image = Path(cocotb.plusargs["PROG_BIN"]).read_bytes()
    await load_image(dut, image)

    expected_exit = int(cocotb.plusargs.get("EXPECT_EXIT", "0"))
    expected_stdout = bytes.fromhex(
        cocotb.plusargs.get("EXPECT_STDOUT_HEX", "")
    ).decode()

    dut.rst_n.value = 1
    uart = []
    for _ in range(2_000_000):
        await RisingEdge(dut.clk)
        if int(dut.uart_valid.value):
            uart.append(chr(int(dut.uart_data.value)))
        if int(dut.halted.value):
            break
    else:
        assert False, "core did not halt"

    actual_stdout = "".join(uart)
    dut._log.info(
        "scratchpad core: exit=%d reason=%d pc=0x%x stdout=%r cycles=%d",
        int(dut.exit_code.value),
        int(dut.halt_reason.value),
        int(dut.pc_out.value),
        actual_stdout,
        int(dut.cycle_count.value),
    )
    assert int(dut.exit_code.value) == expected_exit
    assert actual_stdout == expected_stdout
