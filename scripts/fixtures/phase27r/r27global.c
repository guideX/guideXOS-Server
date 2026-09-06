int answer = 40;
int gx_main(gx_app_context* ctx)
{
    int* p = &answer;
    *p = *p + 2;
    return answer;
}
