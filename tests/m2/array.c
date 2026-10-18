int a[5];

int main(void)
{
    for (int i = 0; i < 5; i++)
        a[i] = i * i;
    int s = 0;
    for (int i = 0; i < 5; i++)
        s += a[i];
    return s;
}
