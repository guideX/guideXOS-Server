int gx_bytes_copy(unsigned char* destination, unsigned char* source, int length);

int gx_comp4o_local_observed[10];

int gx_comp4o_test_copy_local()
{
    int index;
    unsigned char local_source[8];
    unsigned char local_destination[10];
    index = 0;
    while (index < 8) {
        local_source[index] = index * 17;
        local_destination[index] = 90;
        index = index + 1;
    }
    local_destination[8] = 165;
    local_destination[9] = 90;
    gx_bytes_copy(local_destination, local_source, 8);
    index = 0;
    while (index < 10) {
        gx_comp4o_local_observed[index] = local_destination[index];
        index = index + 1;
    }
    return 0;
}
