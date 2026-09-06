int recursive_sum(int n)
{
    int pair[2];
    pair[0] = n;
    pair[1] = 1;
    if (n == 0) return 0;
    return pair[0] + recursive_sum(n - pair[1]);
}

int gx_main(gx_app_context* ctx) { return recursive_sum(8) + 6; }
