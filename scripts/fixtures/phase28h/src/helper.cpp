int helper_tail(int tail_input, int tail_bonus)
{
    int tail_value = tail_input + tail_bonus;
    int* tail_pointer = &tail_value;
    int value = tail_value;
    if (tail_pointer == 0) return tail_value;
    return tail_value + (value - tail_value);
}

int helper(int input, int delta)
{
    int adjusted = input + delta;
    int value = adjusted;
    int doubled = adjusted * 2;
    int tail_result = helper_tail(doubled, 5);
    adjusted = adjusted + 1;
    int result = tail_result + delta + (adjusted - adjusted);
    return result;
}
