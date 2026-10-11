#include "../compositor_resize_notification.h"

#include <iostream>

int main()
{
    const std::string expected = "73|640|480";
    const std::string actual = gxos::gui::resizenotice::payload(73u, 640, 480);
    if (actual != expected) {
        std::cerr << "FAIL: resize notice payload was " << actual << '\n';
        return 1;
    }

    std::cout << "Compositor resize notification test PASS\n";
    return 0;
}
