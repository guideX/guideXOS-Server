int clamp_answer(int x)
{
    if (x > 42)
    {
        return 42;
    }

    return x;
}

int gx_main(gx_app_context* ctx)
{
    return clamp_answer(100);
}
