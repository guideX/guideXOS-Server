int gx_main(void* ctx)
{
    int outer = 0;
    int total = 0;
    while (outer < 3)
    {
        int inner = 0;
        while (inner < 2)
        {
            total = total + 7;
            inner = inner + 1;
        }
        outer = outer + 1;
    }
    return total;
}
