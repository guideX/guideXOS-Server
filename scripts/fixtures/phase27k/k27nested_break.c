int gx_main(void* ctx)
{
    int outer = 0;
    int total = 0;
    while (outer < 3)
    {
        int inner = 0;
        while (inner < 10)
        {
            inner = inner + 1;
            if (inner > 2)
            {
                break;
            }
            total = total + 7;
        }
        outer = outer + 1;
    }
    return total;
}
