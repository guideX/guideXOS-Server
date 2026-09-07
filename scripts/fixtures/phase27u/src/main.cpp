struct Point
{
    int x;
    int y;
    int tag;
};

int point_value(struct Point* p);

int gx_main(gx_app_context* ctx)
{
    struct Point points[3];
    struct Point* p;
    int d;

    points[0].x = 10;
    p = &points[0];
    p = p + 1;
    p->x = 40;
    d = point_value(p);

    log(ctx, "Phase 27U struct array traversal completed.");
    return d;
}
