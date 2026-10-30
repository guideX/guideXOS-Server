int gx_bytes_copy(unsigned char* destination, unsigned char* source, int length);

unsigned char gx_comp4o_seed[8] = {65, 66, 0, 68, 255, 128, 1, 2};
unsigned char gx_comp4o_global_copy[8] = {90, 90, 90, 90, 90, 90, 90, 90};
int gx_comp4o_copy_guard = 123456;
int gx_comp4o_copy_results[4];
unsigned char gx_comp4o_reject_copy[4] = {10, 11, 12, 13};
int gx_comp4o_copy_lengths[3] = {0, -1, 257};

int gx_comp4o_test_copy()
{
    int index;
    gx_comp4o_copy_results[0] = gx_bytes_copy(gx_comp4o_global_copy, gx_comp4o_seed, 8);
    index = 0;
    while (index < 3) {
        gx_comp4o_copy_results[index + 1] = gx_bytes_copy(
            gx_comp4o_reject_copy, gx_comp4o_seed, gx_comp4o_copy_lengths[index]);
        index = index + 1;
    }
    return 0;
}
