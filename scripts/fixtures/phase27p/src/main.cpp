extern int answer;
int add_two();

int gx_main(gx_app_context* ctx)
{
    add_two();
    log(ctx, "Incremental linked build executed.");
    return answer;
}
