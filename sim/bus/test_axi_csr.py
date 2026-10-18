"""cocotb AXI4-Lite + global CSR tests."""

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge


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


async def axi_read(dut, addr: int) -> int:
    dut.s_araddr.value = addr
    dut.s_arvalid.value = 1
    while not int(dut.s_arready.value):
        await RisingEdge(dut.clk)
    await RisingEdge(dut.clk)
    dut.s_arvalid.value = 0
    while not int(dut.s_rvalid.value):
        await RisingEdge(dut.clk)
    value = int(dut.s_rdata.value)
    dut.s_rready.value = 1
    await RisingEdge(dut.clk)
    dut.s_rready.value = 0
    return value


@cocotb.test()
async def axi_csr_basic(dut):
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
    dut.cycle_count.value = 0x0000_0001_2345_6789
    dut.status_busy.value = 0

    doorbell_count = 0
    abort_count = 0
    reset_count = 0

    async def monitor():
        nonlocal doorbell_count, abort_count, reset_count
        while True:
            await RisingEdge(dut.clk)
            if int(dut.doorbell.value):
                doorbell_count += 1
            if int(dut.abort_pulse.value):
                abort_count += 1
            if int(dut.soft_reset_pulse.value):
                reset_count += 1

    cocotb.start_soon(monitor())

    dut.rst_n.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)

    assert await axi_read(dut, 0x000) == 0x564F4C43, "ID"
    assert await axi_read(dut, 0x004) == 0x00000001, "VERSION"
    status = await axi_read(dut, 0x008)
    assert status & 0x1, "STATUS.IDLE"
    assert await axi_read(dut, 0x04C) == 0, "SCRATCH_BASE"
    assert await axi_read(dut, 0x050) == 0x00040000, "SCRATCH_SIZE"
    assert await axi_read(dut, 0x038) == 0x23456789, "CYCLE_LO"
    assert await axi_read(dut, 0x03C) == 0x00000001, "CYCLE_HI"

    await axi_write(dut, 0x00C, 0x00000003)
    assert await axi_read(dut, 0x00C) == 0x00000003, "CONTROL"

    await axi_write(dut, 0x018, 0x10000000)
    await axi_write(dut, 0x01C, 0x00000100)
    await axi_write(dut, 0x024, 0x00000005)
    assert int(dut.queue_base.value) == 0x10000000
    assert int(dut.queue_size.value) == 0x100
    assert int(dut.queue_tail.value) == 5
    assert doorbell_count >= 1, "doorbell did not pulse"
    assert await axi_read(dut, 0x024) == 5, "QUEUE_TAIL"

    await axi_write(dut, 0x014, 0x0000000F)
    assert await axi_read(dut, 0x014) == 0x0000000F, "IRQ_ENABLE"
    await axi_write(dut, 0x010, 0x00000005)
    assert await axi_read(dut, 0x010) == 0, "IRQ_STATUS RW1C"

    # Read-only register must ignore writes.
    await axi_write(dut, 0x040, 0x00000BAD)
    assert await axi_read(dut, 0x040) == 0, "ERROR_CODE RO"

    # Byte strobe: only byte 1 changes (0x03 -> 0xAA03).
    await axi_write(dut, 0x00C, 0x0000AA00, 0b0010)
    assert await axi_read(dut, 0x00C) == 0x0000AA03, "CONTROL byte strobe"

    await axi_write(dut, 0x048, 1)
    await axi_write(dut, 0x05C, 1)
    assert abort_count >= 1, "abort pulse"
    assert reset_count >= 1, "soft reset pulse"

    dut._log.info("AXI4-Lite CSR tests passed")
