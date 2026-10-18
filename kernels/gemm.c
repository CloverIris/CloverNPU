/*
 * Dynamic-shape GEMM kernel, block 16x16x16.
 *
 * args[0..8] = M, N, K, A_addr, B_addr, C_addr, lda, ldb, ldc
 * A/B are FP16, C is FP32.  Addresses and strides are bytes.
 *
 * The MMA descriptor lives at a fixed, 64-byte-aligned scratchpad address so
 * the kernel does not depend on stack alignment attributes yet.
 */
unsigned int min_u(unsigned int a, unsigned int b)
{
    return (a < b) ? a : b;
}

void gemm(unsigned int *args)
{
    unsigned int M = args[0];
    unsigned int N = args[1];
    unsigned int K = args[2];
    unsigned int A = args[3];
    unsigned int B = args[4];
    unsigned int C = args[5];
    unsigned int lda = args[6];
    unsigned int ldb = args[7];
    unsigned int ldc = args[8];

    unsigned int *d = (unsigned int *)0x00028000;

    for (unsigned int m0 = 0; m0 < M; m0 += 16) {
        for (unsigned int n0 = 0; n0 < N; n0 += 16) {
            for (unsigned int k0 = 0; k0 < K; k0 += 16) {
                unsigned int mm = min_u(16, M - m0);
                unsigned int nn = min_u(16, N - n0);
                unsigned int kk = min_u(16, K - k0);

                d[0] = 0x31414D4D;              /* "MMA1" */
                d[1] = (k0 != 0) | (1 << 5) |   /* accumulate, A=FP16 */
                       (1 << 8) | (1 << 4);     /* B=FP16, saturate */
                d[2] = mm;
                d[3] = nn;
                d[4] = kk;
                d[5] = A + m0 * lda + k0 * 2;
                d[6] = B + k0 * ldb + n0 * 2;
                d[7] = C + m0 * ldc + n0 * 4;
                d[8] = lda;
                d[9] = ldb;
                d[10] = ldc;
                d[11] = 0;
                d[12] = 0;
                d[13] = 0x3F800000;          /* post_scale = 1.0f */
                d[14] = 0;
                d[15] = 0;

                __clover_mma(d);
            }
            __clover_wait(1);
        }
    }
}
