#include <cmath>

extern "C" double js114_cmath_probe(double value) {
    return std::floor(std::pow(value, 2.0));
}
