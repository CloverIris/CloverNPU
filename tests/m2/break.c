int main(void)
{
    int s = 0;
    for (int i = 0; i < 10; i++) {
        if (i == 5) break;
        if (i == 2) continue;
        s += i;
    }
    return s;
}
