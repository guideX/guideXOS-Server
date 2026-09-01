// Small fixed-storage text field used by kernel dialogs.
//
// The framebuffer desktop does not use a hosted GUI toolkit, so controls must
// make focus, caret, and edit handling explicit. This helper keeps those
// semantics in one place for numeric IPv4 fields and is also usable by other
// fixed-size kernel controls.

#ifndef KERNEL_TEXT_FIELD_H
#define KERNEL_TEXT_FIELD_H

#include "kernel/types.h"

namespace kernel {
namespace text_field {

static const uint8_t MAX_TEXT_LENGTH = 15;

struct TextField {
    char text[MAX_TEXT_LENGTH + 1];
    uint8_t length;
    uint8_t caret;
    bool focused;
    bool enabled;
};

inline void clear(TextField& field)
{
    field.text[0] = '\0';
    field.length = 0;
    field.caret = 0;
}

inline void set_text(TextField& field, const char* value)
{
    clear(field);
    if (!value) return;
    while (value[field.length] != '\0' && field.length < MAX_TEXT_LENGTH) {
        field.text[field.length] = value[field.length];
        ++field.length;
    }
    field.text[field.length] = '\0';
    field.caret = field.length;
}

inline void set_focus(TextField& field, bool focused)
{
    field.focused = focused;
    if (!focused) field.caret = field.length;
}

inline void set_caret(TextField& field, uint8_t position)
{
    field.caret = position > field.length ? field.length : position;
}

inline bool erase_backward(TextField& field)
{
    if (!field.enabled || !field.focused || field.caret == 0) return false;
    for (uint8_t i = field.caret; i < field.length; ++i) {
        field.text[i - 1] = field.text[i];
    }
    --field.length;
    --field.caret;
    field.text[field.length] = '\0';
    return true;
}

inline bool erase_forward(TextField& field)
{
    if (!field.enabled || !field.focused || field.caret >= field.length) return false;
    for (uint8_t i = field.caret + 1; i < field.length; ++i) {
        field.text[i - 1] = field.text[i];
    }
    --field.length;
    field.text[field.length] = '\0';
    return true;
}

inline bool insert_numeric(TextField& field, char value)
{
    if (!field.enabled || !field.focused || field.length >= MAX_TEXT_LENGTH) return false;
    if (!((value >= '0' && value <= '9') || value == '.')) return false;

    for (uint8_t i = field.length; i > field.caret; --i) {
        field.text[i] = field.text[i - 1];
    }
    field.text[field.caret] = value;
    ++field.length;
    ++field.caret;
    field.text[field.length] = '\0';
    return true;
}

// Key values intentionally match shell.h, but the helper does not include
// shell.h so it can be used by the desktop and hosted control tests without a
// dependency cycle.
inline bool handle_key(TextField& field, uint32_t key)
{
    if (!field.enabled || !field.focused) return false;

    if (key == '\b') return erase_backward(field);
    if (key == 0x106u) return erase_forward(field); // shell::KEY_DELETE
    if (key == 0x102u) { // shell::KEY_LEFT
        if (field.caret == 0) return false;
        --field.caret;
        return true;
    }
    if (key == 0x103u) { // shell::KEY_RIGHT
        if (field.caret >= field.length) return false;
        ++field.caret;
        return true;
    }
    if (key == 0x104u) { // shell::KEY_HOME
        field.caret = 0;
        return true;
    }
    if (key == 0x105u) { // shell::KEY_END
        field.caret = field.length;
        return true;
    }
    return key >= 32u && key < 127u
        ? insert_numeric(field, static_cast<char>(key))
        : false;
}

} // namespace text_field
} // namespace kernel

#endif // KERNEL_TEXT_FIELD_H
