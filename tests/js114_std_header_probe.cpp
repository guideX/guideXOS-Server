#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <new>
#include <utility>

static_assert(sizeof(std::unique_ptr<int>) == sizeof(int*),
              "unique_ptr header must be available to the target");
static_assert(std::numeric_limits<double>::is_iec559,
              "the probe assumes the target's IEEE-754 double representation");

extern "C" int js114_std_header_probe() {
    std::array<int, 3> values = {{3, 1, 2}};
    std::sort(values.begin(), values.end());
    const int moved = std::move(values[1]);
    return moved == 2 && values[0] == 1 &&
           std::numeric_limits<int>::max() > moved ? 0 : 1;
}
