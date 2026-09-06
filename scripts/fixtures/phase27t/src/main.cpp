struct Point
{
    int x;
    int y;
};

int sum_point(struct Point* p);

int gx_main(gx_app_context* ctx)
{
    struct Point p;
    p.x = 20;
    p.y = 22;
    int result = sum_point(&p);
    log(ctx, "Struct field execution completed.");
    return result;
}
