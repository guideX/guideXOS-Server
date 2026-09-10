int helper(int input, int delta)
{
    int adjusted = input + delta;
    int doubled = adjusted * 2;
    adjusted = adjusted + 1;
    int result = doubled + 4;
    return result;
}
