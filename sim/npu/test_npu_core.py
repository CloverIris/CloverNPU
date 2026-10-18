"""Host -> DMA -> RUN kernel -> DMA result -> completion test."""

from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer


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


def build_command(*, opcode, flags=0, src=0, dst=0, dim0=0, dim1=0,
                  stride0=0, stride1=0, seq=0):
    return [
        opcode & 0xFFFFFFFF, flags & 0xFFFFFFFF,
        src & 0xFFFFFFFF, dst & 0xFFFFFFFF,
        dim0 & 0xFFFFFFFF, dim1 & 0xFFFFFFFF, 0,
        stride0 & 0xFFFFFFFF, stride1 & 0xFFFFFFFF,
        0, 0, seq & 0xFFFFFFFF, 0, 0, 0, 0,
    ]


async def write_command(dut, index: int, words: list[int]):
    for beat in range(4):
        value = 0
        for i in range(4):
            value |= (words[beat * 4 + i] & 0xFFFFFFFF) << (32 * i)
        await host_write(dut, index * 64 + beat * 16, value)


@cocotb.test()
async def npu_run_kernel(dut):
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
    dut.cycle_count.value = 0x00000042
    dut.host_we.value = 0
    dut.host_addr.value = 0
    dut.host_wdata.value = 0
    dut.host_wstrb.value = 0

    dut.rst_n.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)

    kernel = Path(cocotb.plusargs["KERNEL_BIN"]).read_bytes()
    kernel += b"\x00" * ((16 - len(kernel) % 16) % 16)
    for offset in range(0, len(kernel), 16):
        chunk = kernel[offset : offset + 16]
        await host_write(dut, 0x4000 + offset, int.from_bytes(chunk, "little"))
    kernel_bytes = len(kernel)

    commands = [
        build_command(opcode=2, flags=0, src=0x10004000, dst=0,
                      dim0=kernel_bytes, dim1=1, stride0=0, stride1=0),
        build_command(opcode=3, src=0, dst=0, dim0=0x0003FFF0, dim1=1),
        build_command(opcode=2, flags=1, src=0x00030000, dst=0x10005000,
                      dim0=16, dim1=1, stride0=0, stride1=0),
        build_command(opcode=5, seq=1),
        build_command(opcode=8, seq=2),
    ]
    for index, command in enumerate(commands):
        await write_command(dut, index, command)

    await axi_write(dut, 0x00C, 1)           # CONTROL.ENABLE
    await axi_write(dut, 0x014, 1)           # IRQ_ENABLE
    await axi_write(dut, 0x018, 0x10000000)  # QUEUE_BASE
    await axi_write(dut, 0x01C, 8)           # QUEUE_SIZE
    await axi_write(dut, 0x028, 0x10001000)  # COMPLETE_BASE
    await axi_write(dut, 0x02C, 8)           # COMPLETE_SIZE
    await axi_write(dut, 0x034, 0)           # COMPLETE_TAIL
    await axi_write(dut, 0x024, len(commands))

    core_seen = 0

    async def monitor_core():
        nonlocal core_seen
        while True:
            await RisingEdge(dut.clk)
            if int(dut.core_active.value):
                core_seen += 1

    cocotb.start_soon(monitor_core())

    for _ in range(200000):
        await RisingEdge(dut.clk)
        if (int(dut.queue_head.value) == 5 and
                int(dut.completion_head.value) == 2 and
                not int(dut.busy.value)):
            break
    else:
        raise AssertionError(
            f"NPU stalled: qhead={int(dut.queue_head.value)} "
            f"chead={int(dut.completion_head.value)} "
            f"busy={int(dut.busy.value)} core={int(dut.core_active.value)}"
        )

    assert core_seen > 0, "core never became active"
    assert int(dut.last_exit_code.value) == 0, "kernel exit status"
    result = await host_read(dut, 0x5000)
    assert result & 0xFFFFFFFF == 55, f"kernel result {result & 0xFFFFFFFF}"

    for entry, expected_seq in enumerate((1, 2)):
        value = await host_read(dut, 0x1000 + entry * 16)
        assert value & 0xFFFFFFFF == expected_seq
        assert (value >> 32) & 0xFFFFFFFF == 0

    dut._log.info("NPU RUN kernel test passed")
