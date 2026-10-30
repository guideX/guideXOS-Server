int gx_bytes_fill(unsigned char* destination, int value, int length);

unsigned char gx_comp4o_fill_values[7] = {9, 9, 9, 9, 9, 9, 9};
unsigned char gx_comp4o_fill_guarded[9] = {90, 9, 9, 9, 9, 9, 9, 9, 91};
unsigned char gx_comp4o_zero_fill[4] = {55, 56, 57, 58};
unsigned char gx_comp4o_reject_fill[4] = {20, 21, 22, 23};
unsigned char gx_comp4o_full_fill[256];
int gx_comp4o_fill_results[12];
int gx_comp4o_fill_inputs[7] = {0, 1, 127, 128, 255, 256, -1};
int gx_comp4o_fill_lengths[3] = {0, -1, 257};

int gx_comp4o_test_fill()
{
    int index;
    index = 0;
    while (index < 3) {
        gx_comp4o_fill_results[index] = gx_bytes_fill(
            gx_comp4o_zero_fill, 1, gx_comp4o_fill_lengths[index]);
        index = index + 1;
    }
    gx_comp4o_fill_results[3] = gx_bytes_fill(gx_comp4o_full_fill, 171, 256);
    gx_comp4o_fill_results[4] = gx_bytes_fill(gx_comp4o_fill_guarded + 1, 128, 7);
    index = 0;
    while (index < 7) {
        gx_comp4o_fill_results[index + 5] = gx_bytes_fill(
            gx_comp4o_fill_values + index, gx_comp4o_fill_inputs[index], 1);
        index = index + 1;
    }
    return 0;
}
