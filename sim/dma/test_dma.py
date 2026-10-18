"""cocotb test for the 2D AXI DMA."""

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer


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


async def start_dma(dut, *, direction, src, dst, row_bytes, rows,
                    src_stride, dst_stride):
    dut.dir.value = direction
    dut.src_addr.value = src
    dut.dst_addr.value = dst
    dut.bytes_per_row.value = row_bytes
    dut.num_rows.value = rows
    dut.src_stride.value = src_stride
    dut.dst_stride.value = dst_stride
    dut.start.value = 1

    # Hold start until busy rises.  This also walks DONE -> IDLE -> latch
    # when starting back-to-back transactions.
    for _ in range(8):
        await RisingEdge(dut.clk)
        if int(dut.busy.value):
            break
    dut.start.value = 0

    for _ in range(100000):
        await RisingEdge(dut.clk)
        if int(dut.done.value):
            return
    raise AssertionError("DMA did not finish")


@cocotb.test()
async def dma_2d_copy(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())

    dut.start.value = 0
    dut.dir.value = 0
    dut.src_addr.value = 0
    dut.dst_addr.value = 0
    dut.bytes_per_row.value = 0
    dut.num_rows.value = 0
    dut.src_stride.value = 0
    dut.dst_stride.value = 0
    dut.host_we.value = 0
    dut.host_addr.value = 0
    dut.host_wdata.value = 0
    dut.host_wstrb.value = 0
    dut.loader_we.value = 0
    dut.loader_addr.value = 0
    dut.loader_data.value = 0
    dut.loader_be.value = 0

    dut.rst_n.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)

    rows = 4
    row_bytes = 64
    src_stride = 128
    dst_stride = 64
    src_base = 0x0000
    dst_base = 0x2000
    expected = []

    for r in range(rows):
        for w in range(4):
            value = ((r + 1) << 96) | ((w + 1) << 64) | 0x0123456789ABCDEF
            expected.append(value)
            await host_write(dut, src_base + r * src_stride + w * 16, value)

    await start_dma(
        dut,
        direction=0,
        src=0x10000000 + src_base,
        dst=0,
        row_bytes=row_bytes,
        rows=rows,
        src_stride=src_stride,
        dst_stride=dst_stride,
    )
    assert int(dut.error.value) == 0, "DRAM->SP error"

    await start_dma(
        dut,
        direction=1,
        src=0,
        dst=0x10000000 + dst_base,
        row_bytes=row_bytes,
        rows=rows,
        src_stride=dst_stride,
        dst_stride=dst_stride,
    )
    assert int(dut.error.value) == 0, "SP->DRAM error"

    for i, value in enumerate(expected):
        got = await host_read(dut, dst_base + i * 16)
        assert got == value, f"word {i}: {got:#x} != {value:#x}"

    # Misaligned row length must be rejected.
    await start_dma(
        dut,
        direction=0,
        src=0x10000000,
        dst=0,
        row_bytes=20,
        rows=1,
        src_stride=0,
        dst_stride=0,
    )
    assert int(dut.error.value) == 1, "misaligned DMA not rejected"

    dut._log.info("DMA 2D tests passed")
