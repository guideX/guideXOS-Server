#ifndef KERNEL_WIDGET_ACTIVATION_H
#define KERNEL_WIDGET_ACTIVATION_H

#include "kernel_app.h"

namespace kernel { namespace app {

inline int focused_button_widget_id_for_index(int focusIndex,
                                              int buttonFocusIndex,
                                              int buttonWidgetId)
{
    return focusIndex == buttonFocusIndex ? buttonWidgetId : -1;
}

template <typename ClickHandler>
inline bool dispatch_widget_activation(KernelWindow* window, int widgetId,
                                       ClickHandler click)
{
    if (!window || widgetId < 0) return false;
    for (int i = 0; i < window->widgetCount; ++i) {
        Widget& widget = window->widgets[i];
        if (widget.id != widgetId) continue;
        if (!widget.visible || !widget.enabled) return false;
        click(widget.id);
        return true;
    }
    return false;
}

template <typename ClickHandler>
inline bool dispatch_button_activation(KernelWindow* window, int widgetId,
                                       ClickHandler click)
{
    if (!window || widgetId < 0) return false;
    for (int i = 0; i < window->widgetCount; ++i) {
        const Widget& widget = window->widgets[i];
        if (widget.id != widgetId) continue;
        if (widget.type != WidgetType::Button) return false;
        return dispatch_widget_activation(window, widgetId, click);
    }
    return false;
}

template <typename ClickHandler>
inline bool dispatch_focused_button_activation(KernelWindow* window,
                                               int widgetId,
                                               int focusedWidgetId,
                                               uint32_t key,
                                               ClickHandler click)
{
    if (focusedWidgetId != widgetId ||
        (key != '\n' && key != '\r' && key != 32)) return false;
    return dispatch_button_activation(window, widgetId, click);
}

}} // namespace kernel::app

#endif
