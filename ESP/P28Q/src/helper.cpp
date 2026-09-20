int helper(int input, int delta)
{
    int adjusted = input + delta;
    int doubled = adjusted * 2;
    return doubled - delta;
}
