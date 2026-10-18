"""cocotb test for the command processor + DMA + completion ring."""

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


def build_command(*, opcode, flags=0, src=0, dst=0, dim0=0, dim1=0,
                  stride0=0, stride1=0, seq=0):
    words = [
        opcode & 0xFFFFFFFF,
        flags & 0xFFFFFFFF,
        src & 0xFFFFFFFF,
        dst & 0xFFFFFFFF,
        dim0 & 0xFFFFFFFF,
        dim1 & 0xFFFFFFFF,
        0,
        stride0 & 0xFFFFFFFF,
        stride1 & 0xFFFFFFFF,
        0,
        0,
        seq & 0xFFFFFFFF,
        0,
        0,
        0,
        0,
    ]
    return words


async def write_command(dut, index: int, words: list[int]):
    for beat in range(4):
        value = 0
        for i in range(4):
            value |= (words[beat * 4 + i] & 0xFFFFFFFF) << (32 * i)
        await host_write(dut, 0x0000 + index * 64 + beat * 16, value)


@cocotb.test()
async def cp_dma_completion(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())

    dut.enable.value = 0
    dut.cp_abort.value = 0
    dut.queue_base.value = 0x10000000
    dut.queue_size.value = 8
    dut.queue_tail.value = 0
    dut.completion_base.value = 0x10001000
    dut.completion_size.value = 8
    dut.completion_tail.value = 0
    dut.cycle_count.value = 0x00001234
    dut.host_we.value = 0
    dut.host_addr.value = 0
    dut.host_wdata.value = 0
    dut.host_wstrb.value = 0

    dut.rst_n.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)

    # Source: 2 rows x 64 B, packed.
    source = []
    for r in range(2):
        for w in range(4):
            value = ((r + 1) << 96) | ((w + 1) << 64) | 0xDEADBEEFCAFEF00D
            source.append(value)
            await host_write(dut, 0x2000 + r * 64 + w * 16, value)

    commands = [
        build_command(opcode=2, flags=0, src=0x10002000, dst=0,
                      dim0=64, dim1=2, stride0=64, stride1=64),
        build_command(opcode=2, flags=1, src=0, dst=0x10003000,
                      dim0=64, dim1=2, stride0=64, stride1=64),
        build_command(opcode=5, seq=1),
        build_command(opcode=8, seq=2),
    ]
    for index, command in enumerate(commands):
        await write_command(dut, index, command)

    irq_count = 0

    async def monitor_irq():
        nonlocal irq_count
        while True:
            await RisingEdge(dut.clk)
            if int(dut.irq.value):
                irq_count += 1

    cocotb.start_soon(monitor_irq())

    dut.queue_tail.value = len(commands)
    dut.enable.value = 1

    for cycle in range(50000):
        await RisingEdge(dut.clk)
        if (int(dut.queue_head.value) == 4 and
                int(dut.completion_head.value) == 2 and
                not int(dut.busy.value)):
            break
        if cycle % 500 == 0:
            dut._log.info("waiting: qhead=%d chead=%d busy=%d",
                          int(dut.queue_head.value),
                          int(dut.completion_head.value),
                          int(dut.busy.value))
    else:
        raise AssertionError(
            f"stalled: qhead={int(dut.queue_head.value)} "
            f"chead={int(dut.completion_head.value)} "
            f"busy={int(dut.busy.value)}"
        )

    assert int(dut.queue_head.value) == 4, "queue head"
    assert int(dut.completion_head.value) == 2, "completion head"
    assert int(dut.error.value) == 0, "CP error"
    assert irq_count >= 2, "IRQ pulses"

    for entry, expected_seq in enumerate((1, 2)):
        value = await host_read(dut, 0x1000 + entry * 16)
        seq = value & 0xFFFFFFFF
        status = (value >> 32) & 0xFFFFFFFF
        cycles = (value >> 64) & 0xFFFFFFFF
        err = (value >> 96) & 0xFFFFFFFF
        assert seq == expected_seq, f"entry {entry} seq {seq}"
        assert status == 0, f"entry {entry} status {status}"
        assert cycles == 0x1234, f"entry {entry} cycles {cycles}"
        assert err == 0, f"entry {entry} error {err}"

    for i, value in enumerate(source):
        got = await host_read(dut, 0x3000 + i * 16)
        assert got == value, f"dest word {i}: {got:#x} != {value:#x}"

    dut._log.info("command processor tests passed")
