#ifndef KERNEL_DISPLAY_OPTIONS_STATE_H
#define KERNEL_DISPLAY_OPTIONS_STATE_H

namespace kernel {
namespace apps {
namespace display_options {

inline bool isCurrentAppearanceOption(const char* currentId,
                                      const char* optionId)
{
    if (!currentId || !optionId) return false;
    while (*currentId && *optionId) {
        if (*currentId != *optionId) return false;
        ++currentId;
        ++optionId;
    }
    return *currentId == '\0' && *optionId == '\0';
}

} // namespace display_options
} // namespace apps
} // namespace kernel

#endif
