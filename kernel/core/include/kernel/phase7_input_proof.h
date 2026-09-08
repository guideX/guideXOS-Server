#pragma once

#include "kernel/kernel_app.h"

namespace kernel {
namespace phase7_input_proof {

bool initialize();
app::KernelWindow* window();
int initial_x();
int initial_y();
bool keyboard_passed();
bool focus_passed();
bool text_passed();

} // namespace phase7_input_proof
} // namespace kernel
