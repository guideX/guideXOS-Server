extern int answer;
int add_two();

int gx_main(gx_app_context* ctx)
{
    add_two();
    log(ctx, "Linked global state updated.");
    return answer;
}
