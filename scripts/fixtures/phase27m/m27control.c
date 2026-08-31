int recursive_answer(int n)
{
    if (n <= 0) { return 0; }
    if (n == 6) { return n + recursive_answer(n - 1); }
    return n + recursive_answer(n - 1);
}

int gx_main(gx_app_context* ctx)
{
    return recursive_answer(6) * 2;
}
