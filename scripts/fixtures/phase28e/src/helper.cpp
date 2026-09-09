int helper_tail(int value)
{
    return value + 2;
}

int helper(int value)
{
    int adjusted = value + 1;
    adjusted = helper_tail(adjusted);
    return adjusted;
}
