/****************************************************************************
 * D13x home panel - Reusable UI component builders
 *
 * Extracted from home_panel_main.c for modularity.
 * All components follow Xiaomi Home design language.
 ****************************************************************************/

#ifndef __HOME_PANEL_COMPONENTS_H
#define __HOME_PANEL_COMPONENTS_H

#include <lvgl/lvgl.h>
#include "home_panel_theme.h"
#include "home_panel_mijia_model.h"

/* Forward declarations for callback types */

typedef void (*home_panel_click_cb_t)(lv_event_t *event, void *user_data);

/* Card builders */

lv_obj_t *component_create_card(lv_obj_t *parent, int x, int y,
                                int width, int height);

lv_obj_t *component_create_card_button(lv_obj_t *parent, int x, int y,
                                       int width, int height,
                                       lv_event_cb_t callback,
                                       void *user_data);

/* Label helpers */

lv_obj_t *component_create_label(lv_obj_t *parent, const char *text,
                                 int x, int y, lv_color_t color);

lv_obj_t *component_create_label_font(lv_obj_t *parent, const char *text,
                                      int x, int y, lv_color_t color,
                                      const lv_font_t *font);

/* Button builders */

lv_obj_t *component_create_button(lv_obj_t *parent, const char *text,
                                  int x, int y, int width, int height,
                                  lv_event_cb_t callback, void *user_data);

lv_obj_t *component_create_icon_button(lv_obj_t *parent, const char *icon,
                                       int x, int y, int size,
                                       lv_event_cb_t callback,
                                       void *user_data);

/* Toggle/Switch */

lv_obj_t *component_create_toggle(lv_obj_t *parent, bool checked,
                                  int x, int y,
                                  lv_event_cb_t callback, void *user_data);

void component_update_toggle(lv_obj_t *toggle, bool checked);

/* Slider */

lv_obj_t *component_create_slider(lv_obj_t *parent, int min, int max,
                                  int value, int x, int y, int width,
                                  lv_event_cb_t callback, void *user_data);

/* Navigation item */

lv_obj_t *component_create_nav_item(lv_obj_t *parent, const char *icon,
                                    const char *text, int x, int y,
                                    int width, int height,
                                    lv_event_cb_t callback, void *user_data);

void component_set_nav_active(lv_obj_t *item, bool active);

/* Device card */

typedef struct
{
  lv_obj_t *card;
  lv_obj_t *icon_label;
  lv_obj_t *name_label;
  lv_obj_t *value_label;
  lv_obj_t *state_label;
  lv_obj_t *toggle;
  const struct home_panel_device_s *device;
} component_device_card_t;

component_device_card_t *component_create_device_card(
  lv_obj_t *parent, int x, int y, int width, int height,
  const struct home_panel_device_s *device,
  lv_event_cb_t toggle_callback, void *toggle_user_data);

void component_update_device_card(component_device_card_t *card,
                                  const struct home_panel_device_s *device);

/* Scene card */

lv_obj_t *component_create_scene_card(lv_obj_t *parent, int x, int y,
                                      int width, int height,
                                      const struct home_panel_scene_s *scene,
                                      lv_color_t accent,
                                      lv_event_cb_t callback,
                                      void *user_data);

/* Metric card (for device detail, proactive, etc.) */

lv_obj_t *component_create_metric_card(lv_obj_t *parent, int x, int y,
                                       int width, int height,
                                       const char *name, const char *value,
                                       lv_color_t accent);

/* Section title */

lv_obj_t *component_create_section_title(lv_obj_t *parent, const char *text,
                                         int x, int y);

/* Status badge */

lv_obj_t *component_create_status_badge(lv_obj_t *parent, const char *text,
                                        int x, int y, lv_color_t color);

/* Empty state */

lv_obj_t *component_create_empty_state(lv_obj_t *parent, const char *text,
                                       int x, int y, int width);

/* Dialog/Modal */

lv_obj_t *component_create_dialog(lv_obj_t *parent, int width, int height,
                                  const char *title);

void component_close_dialog(lv_obj_t *shade);

#endif
