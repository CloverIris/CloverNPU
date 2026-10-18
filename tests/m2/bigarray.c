int main(void)
{
    int a[600];
    for (int i = 0; i < 600; i++)
        a[i] = i;
    int s = 0;
    for (int i = 0; i < 600; i++)
        s += a[i];
    return (s / 600) % 256;
}
