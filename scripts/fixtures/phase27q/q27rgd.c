int descend(int n)
{
    int pair[2];
    pair[0] = n;
    pair[1] = 1;
    return descend(n + pair[1]);
}

int gx_main(gx_app_context* ctx) { return descend(0); }
