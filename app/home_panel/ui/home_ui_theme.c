/****************************************************************************
 * D13x home panel - Xiaomi Home design language theme implementation
 ****************************************************************************/

#include "home_ui_internal.h"

static home_panel_theme_t g_theme;

static lv_color_t theme_page_color(unsigned int page)
{
  switch (page)
    {
      case HOME_PANEL_THEME_ROOMS:
        return lv_color_hex(0x14242a);
      case HOME_PANEL_THEME_SCENES:
        return lv_color_hex(0x17233a);
      case HOME_PANEL_THEME_PROACTIVE:
        return lv_color_hex(0x14251f);
      case HOME_PANEL_THEME_SETTINGS:
        return lv_color_hex(0x17222d);
      case HOME_PANEL_THEME_HOME:
      default:
        return lv_color_hex(0x14262a);
    }
}

static lv_color_t theme_mask_color(unsigned int page)
{
  switch (page)
    {
      case HOME_PANEL_THEME_ROOMS:
        return lv_color_hex(0x17665c);
      case HOME_PANEL_THEME_SCENES:
        return lv_color_hex(0x28558a);
      case HOME_PANEL_THEME_PROACTIVE:
        return lv_color_hex(0x167258);
      case HOME_PANEL_THEME_SETTINGS:
        return lv_color_hex(0x31546f);
      case HOME_PANEL_THEME_HOME:
      default:
        return lv_color_hex(0x176c5c);
    }
}

const home_panel_theme_t *home_panel_theme_get(void)
{
  return &g_theme;
}

void home_panel_theme_init(void)
{
  if (g_theme.initialized)
    {
      return;
    }

  /* Card base */

  lv_style_init(&g_theme.card);
  lv_style_set_radius(&g_theme.card, THEME_RADIUS_CARD);
  lv_style_set_bg_color(&g_theme.card, lv_color_hex(COLOR_SURFACE));
  lv_style_set_bg_grad_color(&g_theme.card,
                             lv_color_hex(COLOR_SURFACE_2));
  lv_style_set_bg_grad_dir(&g_theme.card, LV_GRAD_DIR_VER);
  lv_style_set_bg_main_stop(&g_theme.card, 0);
  lv_style_set_bg_grad_stop(&g_theme.card, 255);
  lv_style_set_bg_opa(&g_theme.card, LV_OPA_COVER);
  lv_style_set_border_width(&g_theme.card, 1);
  lv_style_set_border_color(&g_theme.card, lv_color_hex(COLOR_BORDER));
  lv_style_set_shadow_width(&g_theme.card, 0);
  lv_style_set_pad_all(&g_theme.card, 18);

  /* Card active */

  lv_style_init(&g_theme.card_active);
  lv_style_set_bg_color(&g_theme.card_active,
                        lv_color_hex(THEME_COLOR_SURFACE_ACTIVE));
  lv_style_set_text_color(&g_theme.card_active,
                          lv_color_hex(THEME_COLOR_BG));

  /* Card offline */

  lv_style_init(&g_theme.card_offline);
  lv_style_set_bg_color(&g_theme.card_offline,
                        lv_color_hex(COLOR_SURFACE));
  lv_style_set_opa(&g_theme.card_offline, LV_OPA_60);

  /* Card pressed */

  lv_style_init(&g_theme.card_pressed);
  lv_style_set_bg_color(&g_theme.card_pressed,
                        lv_color_hex(COLOR_SURFACE_2));

  /* Navigation button */

  lv_style_init(&g_theme.nav_button);
  lv_style_set_radius(&g_theme.nav_button, THEME_RADIUS_CTRL);
  lv_style_set_bg_color(&g_theme.nav_button, lv_color_hex(COLOR_NAV));
  lv_style_set_bg_opa(&g_theme.nav_button, LV_OPA_COVER);
  lv_style_set_border_width(&g_theme.nav_button, 0);
  lv_style_set_shadow_width(&g_theme.nav_button, 0);
  lv_style_set_pad_left(&g_theme.nav_button, 16);
  lv_style_set_pad_top(&g_theme.nav_button, 12);
  lv_style_set_pad_bottom(&g_theme.nav_button, 12);

  /* Navigation button active */

  lv_style_init(&g_theme.nav_button_active);
  lv_style_set_bg_color(&g_theme.nav_button_active,
                        lv_color_hex(THEME_COLOR_NAV_ACTIVE_BG));
  lv_style_set_bg_grad_color(&g_theme.nav_button_active,
                             lv_color_hex(0x17483f));
  lv_style_set_bg_grad_dir(&g_theme.nav_button_active, LV_GRAD_DIR_HOR);
  lv_style_set_bg_opa(&g_theme.nav_button_active, LV_OPA_COVER);

  /* Top bar */

  lv_style_init(&g_theme.topbar);
  lv_style_set_bg_color(&g_theme.topbar, lv_color_hex(COLOR_TOPBAR));
  lv_style_set_bg_grad_color(&g_theme.topbar, lv_color_hex(0x111a20));
  lv_style_set_bg_grad_dir(&g_theme.topbar, LV_GRAD_DIR_HOR);
  lv_style_set_bg_opa(&g_theme.topbar, LV_OPA_COVER);
  lv_style_set_border_width(&g_theme.topbar, 1);
  lv_style_set_border_side(&g_theme.topbar, LV_BORDER_SIDE_BOTTOM);
  lv_style_set_border_color(&g_theme.topbar, lv_color_hex(COLOR_BORDER));
  lv_style_set_shadow_width(&g_theme.topbar, 0);

  /* Page background */

  lv_style_init(&g_theme.page_bg);
  lv_style_set_bg_color(&g_theme.page_bg, lv_color_hex(COLOR_BG));
  lv_style_set_bg_opa(&g_theme.page_bg, LV_OPA_COVER);
  lv_style_set_border_width(&g_theme.page_bg, 0);
  lv_style_set_shadow_width(&g_theme.page_bg, 0);

  /* Section title */

  lv_style_init(&g_theme.section_title);
  lv_style_set_text_color(&g_theme.section_title,
                          lv_color_hex(COLOR_TEXT));
  lv_style_set_text_font(&g_theme.section_title, home_panel_font_get());

  /* Body text */

  lv_style_init(&g_theme.body_text);
  lv_style_set_text_color(&g_theme.body_text,
                          lv_color_hex(COLOR_TEXT));
  lv_style_set_text_font(&g_theme.body_text, home_panel_font_get());

  /* Secondary text */

  lv_style_init(&g_theme.secondary_text);
  lv_style_set_text_color(&g_theme.secondary_text,
                          lv_color_hex(COLOR_SECONDARY));
  lv_style_set_text_font(&g_theme.secondary_text, home_panel_font_get());

  /* Caption text */

  lv_style_init(&g_theme.caption_text);
  lv_style_set_text_color(&g_theme.caption_text,
                          lv_color_hex(COLOR_MUTED));
  lv_style_set_text_font(&g_theme.caption_text, home_panel_font_get());

  /* Key value (large number) */

  lv_style_init(&g_theme.key_value);
  lv_style_set_text_color(&g_theme.key_value,
                          lv_color_hex(COLOR_TEXT));

  /* Primary action button */

  lv_style_init(&g_theme.btn_primary);
  lv_style_set_radius(&g_theme.btn_primary, THEME_RADIUS_CTRL);
  lv_style_set_bg_color(&g_theme.btn_primary, lv_color_hex(COLOR_BLUE));
  lv_style_set_bg_opa(&g_theme.btn_primary, LV_OPA_COVER);
  lv_style_set_border_width(&g_theme.btn_primary, 1);
  lv_style_set_border_color(&g_theme.btn_primary,
                            lv_color_hex(0x7bb4ff));
  lv_style_set_shadow_width(&g_theme.btn_primary, 0);
  lv_style_set_text_color(&g_theme.btn_primary,
                          lv_color_hex(COLOR_TEXT));

  /* Secondary action button */

  lv_style_init(&g_theme.btn_secondary);
  lv_style_set_radius(&g_theme.btn_secondary, THEME_RADIUS_CTRL);
  lv_style_set_bg_color(&g_theme.btn_secondary,
                        lv_color_hex(COLOR_SURFACE_2));
  lv_style_set_bg_opa(&g_theme.btn_secondary, LV_OPA_COVER);
  lv_style_set_border_width(&g_theme.btn_secondary, 1);
  lv_style_set_border_color(&g_theme.btn_secondary,
                            lv_color_hex(COLOR_BORDER));
  lv_style_set_shadow_width(&g_theme.btn_secondary, 0);
  lv_style_set_text_color(&g_theme.btn_secondary,
                          lv_color_hex(COLOR_TEXT));

  /* Execute/confirm button (green) */

  lv_style_init(&g_theme.btn_exec);
  lv_style_set_radius(&g_theme.btn_exec, THEME_RADIUS_CTRL);
  lv_style_set_bg_color(&g_theme.btn_exec, lv_color_hex(COLOR_GREEN));
  lv_style_set_bg_opa(&g_theme.btn_exec, LV_OPA_COVER);
  lv_style_set_border_width(&g_theme.btn_exec, 0);
  lv_style_set_shadow_width(&g_theme.btn_exec, 0);
  lv_style_set_text_color(&g_theme.btn_exec,
                          lv_color_hex(THEME_COLOR_BG));

  /* Slider main track */

  lv_style_init(&g_theme.slider_main);
  lv_style_set_bg_color(&g_theme.slider_main,
                        lv_color_hex(COLOR_SURFACE_2));
  lv_style_set_radius(&g_theme.slider_main, THEME_RADIUS_CTRL);

  /* Slider indicator */

  lv_style_init(&g_theme.slider_indic);
  lv_style_set_bg_color(&g_theme.slider_indic, lv_color_hex(COLOR_BLUE));
  lv_style_set_radius(&g_theme.slider_indic, THEME_RADIUS_CTRL);

  /* Slider knob */

  lv_style_init(&g_theme.slider_knob);
  lv_style_set_bg_color(&g_theme.slider_knob, lv_color_hex(COLOR_TEXT));
  lv_style_set_radius(&g_theme.slider_knob, THEME_RADIUS_CTRL);

  /* Toggle on */

  lv_style_init(&g_theme.toggle_on);
  lv_style_set_radius(&g_theme.toggle_on, THEME_RADIUS_CTRL);
  lv_style_set_bg_color(&g_theme.toggle_on, lv_color_hex(COLOR_GREEN));
  lv_style_set_border_width(&g_theme.toggle_on, 0);
  lv_style_set_shadow_width(&g_theme.toggle_on, 0);

  /* Toggle off */

  lv_style_init(&g_theme.toggle_off);
  lv_style_set_radius(&g_theme.toggle_off, THEME_RADIUS_CTRL);
  lv_style_set_bg_color(&g_theme.toggle_off,
                        lv_color_hex(COLOR_SURFACE_2));
  lv_style_set_border_width(&g_theme.toggle_off, 1);
  lv_style_set_border_color(&g_theme.toggle_off,
                            lv_color_hex(COLOR_BORDER));
  lv_style_set_shadow_width(&g_theme.toggle_off, 0);

  /* Typography styles */

  lv_style_init(&g_theme.typography.page_title);
  lv_style_set_text_font(&g_theme.typography.page_title,
                         home_panel_font_get());
  lv_style_set_text_color(&g_theme.typography.page_title,
                          lv_color_hex(COLOR_TEXT));

  lv_style_init(&g_theme.typography.card_title);
  lv_style_set_text_font(&g_theme.typography.card_title,
                         home_panel_font_get());
  lv_style_set_text_color(&g_theme.typography.card_title,
                          lv_color_hex(COLOR_TEXT));

  lv_style_init(&g_theme.typography.body);
  lv_style_set_text_font(&g_theme.typography.body, home_panel_font_get());
  lv_style_set_text_color(&g_theme.typography.body,
                          lv_color_hex(COLOR_TEXT));

  lv_style_init(&g_theme.typography.body_secondary);
  lv_style_set_text_font(&g_theme.typography.body_secondary,
                         home_panel_font_get());
  lv_style_set_text_color(&g_theme.typography.body_secondary,
                          lv_color_hex(COLOR_SECONDARY));

  lv_style_init(&g_theme.typography.caption);
  lv_style_set_text_font(&g_theme.typography.caption, home_panel_font_get());
  lv_style_set_text_color(&g_theme.typography.caption,
                          lv_color_hex(COLOR_MUTED));

  lv_style_init(&g_theme.typography.caption_small);
  lv_style_set_text_font(&g_theme.typography.caption_small,
                         home_panel_font_get());
  lv_style_set_text_color(&g_theme.typography.caption_small,
                          lv_color_hex(COLOR_MUTED));

  lv_style_init(&g_theme.typography.key_value);
  lv_style_set_text_font(&g_theme.typography.key_value,
                         &home_panel_digits_28);
  lv_style_set_text_color(&g_theme.typography.key_value,
                          lv_color_hex(COLOR_TEXT));

  lv_style_init(&g_theme.typography.key_value_small);
  lv_style_set_text_font(&g_theme.typography.key_value_small,
                         home_panel_font_get());
  lv_style_set_text_color(&g_theme.typography.key_value_small,
                          lv_color_hex(COLOR_TEXT));

  lv_style_init(&g_theme.typography.time_display);
  lv_style_set_text_font(&g_theme.typography.time_display,
                         &home_panel_digits_28);
  lv_style_set_text_color(&g_theme.typography.time_display,
                          lv_color_hex(COLOR_TEXT));

  g_theme.initialized = true;
}

void theme_apply_card(lv_obj_t *obj)
{
  lv_obj_add_style(obj, &g_theme.card, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void theme_apply_nav(lv_obj_t *obj, bool selected)
{
  lv_obj_add_style(obj, selected ?
                   &g_theme.nav_button_active :
                   &g_theme.nav_button, 0);
}

void theme_apply_topbar(lv_obj_t *obj)
{
  lv_obj_add_style(obj, &g_theme.topbar, 0);
}

void theme_apply_page_bg(lv_obj_t *obj)
{
  lv_obj_add_style(obj, &g_theme.page_bg, 0);
}

void theme_apply_page_atmosphere(lv_obj_t *obj, unsigned int page)
{
  lv_obj_set_style_bg_color(obj, theme_page_color(page), 0);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_stop(obj, 0, 0);
  lv_obj_set_style_bg_grad_stop(obj, 230, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

lv_obj_t *theme_create_ambient_mask(lv_obj_t *parent, unsigned int page)
{
  lv_obj_t *mask = lv_obj_create(parent);
  lv_color_t color = theme_mask_color(page);

  lv_obj_remove_style_all(mask);
  lv_obj_set_pos(mask, 0, 0);
  lv_obj_set_size(mask, lv_pct(100), 176);
  lv_obj_set_style_bg_color(mask, color, 0);
  lv_obj_set_style_bg_grad_color(mask, color, 0);
  lv_obj_set_style_bg_grad_dir(mask, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_stop(mask, 0, 0);
  lv_obj_set_style_bg_grad_stop(mask, 255, 0);
  lv_obj_set_style_bg_main_opa(mask, LV_OPA_30, 0);
  lv_obj_set_style_bg_grad_opa(mask, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(mask, 0, 0);
  lv_obj_add_flag(mask, LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(mask, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return mask;
}

void theme_apply_surface_gradient(lv_obj_t *obj)
{
  lv_obj_set_style_bg_color(obj, lv_color_hex(COLOR_SURFACE_2), 0);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_stop(obj, 0, 0);
  lv_obj_set_style_bg_grad_stop(obj, 235, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

void theme_apply_card_state(lv_obj_t *obj,
                            enum home_panel_card_state_e state)
{
  lv_color_t start = lv_color_hex(COLOR_SURFACE_2);
  lv_color_t end = lv_color_hex(COLOR_SURFACE);
  lv_color_t border = lv_color_hex(COLOR_BORDER);
  lv_opa_t opacity = LV_OPA_COVER;

  switch (state)
    {
      case HOME_PANEL_CARD_ACTIVE:
        start = lv_color_hex(0x25423a);
        end = lv_color_hex(0x1b2b28);
        border = lv_color_hex(0x2f6759);
        break;
      case HOME_PANEL_CARD_OFFLINE:
        opacity = LV_OPA_60;
        break;
      case HOME_PANEL_CARD_LOADING:
        border = lv_color_hex(COLOR_SENSOR);
        opacity = LV_OPA_80;
        break;
      case HOME_PANEL_CARD_ERROR:
        border = lv_color_hex(COLOR_RED);
        break;
      case HOME_PANEL_CARD_IDLE:
      default:
        break;
    }

  lv_obj_set_style_bg_color(obj, start, 0);
  lv_obj_set_style_bg_grad_color(obj, end, 0);
  lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_color(obj, border, 0);
  lv_obj_set_style_opa(obj, opacity, 0);
}

lv_obj_t *theme_create_device_ambient_mask(lv_obj_t *parent,
                                           const char *device_type)
{
  lv_obj_t *mask;
  lv_color_t color = lv_color_hex(0x31546f);

  if (device_type != NULL && strcmp(device_type, "light") == 0)
    {
      color = lv_color_hex(0x8b6720);
    }
  else if (device_type != NULL &&
           (strcmp(device_type, "environment-sensor") == 0 ||
            strcmp(device_type, "contact-sensor") == 0))
    {
      color = lv_color_hex(0x245d87);
    }
  else if (device_type != NULL && strcmp(device_type, "speaker") == 0)
    {
      color = lv_color_hex(0x48505d);
    }

  mask = lv_obj_create(parent);
  lv_obj_remove_style_all(mask);
  lv_obj_set_pos(mask, 0, 0);
  lv_obj_set_size(mask, lv_pct(100), 132);
  lv_obj_set_style_bg_color(mask, color, 0);
  lv_obj_set_style_bg_grad_color(mask, color, 0);
  lv_obj_set_style_bg_grad_dir(mask, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_opa(mask, LV_OPA_30, 0);
  lv_obj_set_style_bg_grad_opa(mask, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(mask, 0, 0);
  lv_obj_add_flag(mask, LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(mask, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return mask;
}

void theme_apply_topbar_gradient(lv_obj_t *obj)
{
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x1a2028), 0);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x111920), 0);
  lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_main_stop(obj, 0, 0);
  lv_obj_set_style_bg_grad_stop(obj, 255, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

void theme_apply_nav_gradient(lv_obj_t *obj)
{
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x171d24), 0);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x0d1116), 0);
  lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_stop(obj, 0, 0);
  lv_obj_set_style_bg_grad_stop(obj, 255, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

void theme_apply_scrim(lv_obj_t *obj)
{
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x05070a), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_70, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
}

lv_obj_t *theme_create_label(lv_obj_t *parent, const char *text,
                             const lv_font_t *font, lv_color_t color)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

/* Typography helpers */

void theme_apply_page_title(lv_obj_t *label)
{
  lv_obj_add_style(label, &g_theme.typography.page_title, 0);
}

void theme_apply_card_title(lv_obj_t *label)
{
  lv_obj_add_style(label, &g_theme.typography.card_title, 0);
}

void theme_apply_body(lv_obj_t *label)
{
  lv_obj_add_style(label, &g_theme.typography.body, 0);
}

void theme_apply_body_secondary(lv_obj_t *label)
{
  lv_obj_add_style(label, &g_theme.typography.body_secondary, 0);
}

void theme_apply_caption(lv_obj_t *label)
{
  lv_obj_add_style(label, &g_theme.typography.caption, 0);
}

void theme_apply_caption_small(lv_obj_t *label)
{
  lv_obj_add_style(label, &g_theme.typography.caption_small, 0);
}

void theme_apply_key_value(lv_obj_t *label, lv_color_t accent)
{
  lv_obj_add_style(label, &g_theme.typography.key_value, 0);
  lv_obj_set_style_text_color(label, accent, 0);
}

void theme_apply_key_value_small(lv_obj_t *label, lv_color_t accent)
{
  lv_obj_add_style(label, &g_theme.typography.key_value_small, 0);
  lv_obj_set_style_text_color(label, accent, 0);
}

void theme_apply_time_display(lv_obj_t *label)
{
  lv_obj_add_style(label, &g_theme.typography.time_display, 0);
}

/* Create labels with semantic typography */

lv_obj_t *theme_create_page_title(lv_obj_t *parent, const char *text,
                                  int x, int y)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  theme_apply_page_title(label);
  return label;
}

lv_obj_t *theme_create_card_title(lv_obj_t *parent, const char *text,
                                  int x, int y)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  theme_apply_card_title(label);
  return label;
}

lv_obj_t *theme_create_body(lv_obj_t *parent, const char *text,
                            int x, int y)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  theme_apply_body(label);
  return label;
}

lv_obj_t *theme_create_body_secondary(lv_obj_t *parent, const char *text,
                                      int x, int y)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  theme_apply_body_secondary(label);
  return label;
}

lv_obj_t *theme_create_caption(lv_obj_t *parent, const char *text,
                               int x, int y)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  theme_apply_caption(label);
  return label;
}

lv_obj_t *theme_create_caption_small(lv_obj_t *parent, const char *text,
                                     int x, int y)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  theme_apply_caption_small(label);
  return label;
}

lv_obj_t *theme_create_key_value(lv_obj_t *parent, const char *text,
                                 int x, int y, lv_color_t accent)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  theme_apply_key_value(label, accent);
  return label;
}

lv_obj_t *theme_create_key_value_small(lv_obj_t *parent, const char *text,
                                       int x, int y, lv_color_t accent)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  theme_apply_key_value_small(label, accent);
  return label;
}
