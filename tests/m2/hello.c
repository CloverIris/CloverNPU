int main(void)
{
    const char *s = "Hi\n";
    while (*s)
        __clover_putchar(*s++);
    return 0;
}
