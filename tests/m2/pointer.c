int main(void)
{
    int a = 3;
    int *p = &a;
    *p = *p + 4;
    return a;
}
