/*
 * guideXOS bounded byte utilities.
 *
 * These declarations use the guideXOS native compiler's supported source
 * subset. They are not a claim of full C library compatibility.
 *
 * Every operation accepts at most 256 bytes. Pointer arguments must refer to
 * live unsigned-byte arrays with enough accessible capacity for the requested
 * operation. The compiler checks each indexed access against its pointer
 * descriptor. Copy requires non-overlapping ranges.
 *
 * Return values:
 *   copy/fill: 0 on success, -1 for a negative length, -2 above 256.
 *   compare: -1/0/1 for byte ordering, -2 for an invalid length.
 *   strlen_bounded: length when a NUL is found, -1 when absent within the
 *                   limit, -2 for a negative limit, -3 above 256.
 * Descriptor violations take the compiler runtime-failure path and are not
 * represented by these ordinary return codes.
 */
int gx_bytes_copy(unsigned char* destination, unsigned char* source, int length);
int gx_bytes_fill(unsigned char* destination, int value, int length);
int gx_bytes_compare(unsigned char* left, unsigned char* right, int length);
int gx_bytes_strlen_bounded(unsigned char* text, int max_length);
