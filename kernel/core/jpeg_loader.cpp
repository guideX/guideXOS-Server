// The hosted and bare-metal adapters intentionally use the same tracked JPEG
// implementation.  This wrapper lets the kernel wildcard build compile that
// implementation with its freestanding allocator and STBI_ONLY_JPEG setup.
#include "../../jpeg_loader.cpp"
