.data
msg:
    .asciz "Hi\n"
.text
.globl _start
_start:
    la   t0, msg
loop:
    lbu  a0, 0(t0)
    beqz a0, done
    li   a7, 1
    ecall
    addi t0, t0, 1
    j    loop
done:
    li   a0, 0
    li   a7, 0
    ecall
