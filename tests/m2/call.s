.text
.globl _start
_start:
    li   a0, 20
    li   a1, 22
    call add2
    li   a7, 0
    ecall

add2:
    add  a0, a0, a1
    ret
