/*
 * FP32 elementwise add: C = A + B.
 * args[0..3] = N, A_addr, B_addr, C_addr (all FP32, contiguous).
 */
int add(unsigned int *args)
{
    unsigned int *d = (unsigned int *)0x00028000;
    d[0] = 0x31434556;                 /* "VEC1" */
    d[1] = 0;                          /* ADD */
    d[2] = args[0];
    d[3] = args[1];
    d[4] = args[2];
    d[5] = args[3];
    d[6] = 4;
    d[7] = 4;
    d[8] = 4;
    d[9] = 0;                          /* src FP32 */
    d[10] = 0;                         /* dst FP32 */
    d[11] = 0;
    d[12] = 0;
    d[13] = 0x3F800000;                /* post_scale = 1.0f */
    d[14] = 0;
    d[15] = 0;
    __clover_vec(d);
    __clover_wait(2);
    return 0;
}
