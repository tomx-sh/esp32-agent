#pragma once

#include <cstddef>

#include <lvgl.h>

using UiPageBuilder = void (*)(lv_obj_t *parent);

struct UiPageDefinition {
  const char *name;
  UiPageBuilder build;
};

lv_obj_t *ui_create_page_view(
    lv_obj_t *parent,
    const UiPageDefinition *pages,
    size_t pageCount);
