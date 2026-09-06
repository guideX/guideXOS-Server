extern int answer;
int add_two(int* p);

int gx_main(gx_app_context* ctx)
{
    int* p = &answer;
    add_two(p);
    log(ctx, "Typed pointer execution completed.");
    return answer;
}
