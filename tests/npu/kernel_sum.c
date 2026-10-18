int kernel(void)
{
    volatile int *p = (volatile int *)0x00030000;
    int s = 0;
    for (int i = 1; i <= 10; i++)
        s += i;
    *p = s;
    return 0;
}
