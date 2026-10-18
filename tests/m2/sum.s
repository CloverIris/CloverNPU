.section .text
.globl _start
_start:
    li   t0, 10
    li   t1, 0
loop:
    add  t1, t1, t0
    addi t0, t0, -1
    bnez t0, loop
    mv   a0, t1
    li   a7, 0
    ecall
