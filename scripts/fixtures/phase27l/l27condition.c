int is_answer(int x)
{
    return x == 42;
}

int gx_main(gx_app_context* ctx)
{
    if (is_answer(42))
    {
        return 42;
    }

    return 0;
}
