int helper_tail(int tail_input, int tail_bonus)
{
    int tail_value = tail_input + tail_bonus;
    return tail_value;
}

int helper(int input, int delta)
{
    int adjusted = input + delta;
    int doubled = adjusted * 2;
    int tail_result = helper_tail(doubled, 5);
    int result = tail_result + delta;
    return result;
}
