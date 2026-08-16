/****************************************************************************
 * D13x home panel - Xiaomi Home design language theme tokens
 *
 * Based on xiaomi-home-ui-design SKILL, D13x LVGL 1024x600 Profile,
 * and Xiaomi Home 11.6.704 verified design language.
 ****************************************************************************/

#ifndef __HOME_PANEL_THEME_H
#define __HOME_PANEL_THEME_H

#include <lvgl/lvgl.h>

/* Layout constants */

#define THEME_TOPBAR_HEIGHT     64
#define THEME_NAV_WIDTH         152
#define THEME_CONTENT_PAD       24
#define THEME_CARD_GAP          16
#define THEME_SECTION_GAP       20
#define THEME_MIN_TOUCH         52

/* Shape */

#define THEME_RADIUS_SM         12
#define THEME_RADIUS_CTRL       16
#define THEME_RADIUS_CARD       20
#define THEME_RADIUS_DIALOG     24
#define THEME_RADIUS_PILL       999

/* Typography sizes */

#define THEME_FONT_TIME         32
#define THEME_FONT_PAGE_TITLE   28
#define THEME_FONT_KEY_VALUE    30
#define THEME_FONT_CARD_TITLE   20
#define THEME_FONT_BODY         18
#define THEME_FONT_SECONDARY    16
#define THEME_FONT_CAPTION      14

/* Typography semantic styles */

typedef struct
{
  lv_style_t page_title;      /* 28px, medium weight, primary color */
  lv_style_t card_title;      /* 20px, medium weight, primary color */
  lv_style_t body;            /* 18px, regular, primary color */
  lv_style_t body_secondary;  /* 18px, regular, secondary color */
  lv_style_t caption;         /* 16px, regular, muted color */
  lv_style_t caption_small;   /* 14px, regular, muted color */
  lv_style_t key_value;       /* 30px, medium weight, accent color */
  lv_style_t key_value_small; /* 20px, medium weight, accent color */
  lv_style_t time_display;    /* 32px, digits font, primary color */
} home_panel_typography_t;

/* Colors - Xiaomi Home dark theme
 * Evidence: D13X_DESIGN_TOKENS.json + concept-reference.png
 * verified against RGB565 panel constraints.
 */

#define THEME_COLOR_BG              0x101318
#define THEME_COLOR_TOPBAR          0x171B22
#define THEME_COLOR_NAV             0x13171D
#define THEME_COLOR_SURFACE         0x1D222A
#define THEME_COLOR_SURFACE_RAISED  0x252B35
#define THEME_COLOR_SURFACE_ACTIVE  0xE9EEEA
#define THEME_COLOR_TEXT_PRIMARY    0xF4F7F8
#define THEME_COLOR_TEXT_SECONDARY  0xAAB2BC
#define THEME_COLOR_TEXT_MUTED      0x737D89
#define THEME_COLOR_ACTIVE          0x20C997
#define THEME_COLOR_LIGHTING        0xF5B82E
#define THEME_COLOR_SERVICE         0xF58B42
#define THEME_COLOR_SENSOR          0x4AA8FF
#define THEME_COLOR_WARNING         0xFFB44A
#define THEME_COLOR_ERROR           0xFF6262
#define THEME_COLOR_DIVIDER         0x303743
#define THEME_COLOR_NAV_ACTIVE_BG   0x1A2A30

/* Semantic aliases for readability */

#define COLOR_BG            THEME_COLOR_BG
#define COLOR_TOPBAR        THEME_COLOR_TOPBAR
#define COLOR_NAV           THEME_COLOR_NAV
#define COLOR_SURFACE       THEME_COLOR_SURFACE
#define COLOR_SURFACE_2     THEME_COLOR_SURFACE_RAISED
#define COLOR_TEXT          THEME_COLOR_TEXT_PRIMARY
#define COLOR_MUTED         THEME_COLOR_TEXT_MUTED
#define COLOR_SECONDARY     THEME_COLOR_TEXT_SECONDARY
#define COLOR_GREEN         THEME_COLOR_ACTIVE
#define COLOR_BLUE          THEME_COLOR_SENSOR
#define COLOR_ORANGE        THEME_COLOR_SERVICE
#define COLOR_YELLOW        THEME_COLOR_LIGHTING
#define COLOR_RED           THEME_COLOR_ERROR
#define COLOR_WARNING       THEME_COLOR_WARNING
#define COLOR_ERROR         THEME_COLOR_ERROR
#define COLOR_BORDER        THEME_COLOR_DIVIDER
#define COLOR_SENSOR        THEME_COLOR_SENSOR

/* Page atmosphere variants.  Keep these aligned with the five primary
 * navigation destinations without coupling the theme to main.c enums.
 */

enum home_panel_theme_page_e
{
  HOME_PANEL_THEME_HOME = 0,
  HOME_PANEL_THEME_ROOMS,
  HOME_PANEL_THEME_SCENES,
  HOME_PANEL_THEME_PROACTIVE,
  HOME_PANEL_THEME_SETTINGS
};

enum home_panel_card_state_e
{
  HOME_PANEL_CARD_IDLE = 0,
  HOME_PANEL_CARD_ACTIVE,
  HOME_PANEL_CARD_OFFLINE,
  HOME_PANEL_CARD_LOADING,
  HOME_PANEL_CARD_ERROR
};

/* Panel dimensions */

#define PANEL_WIDTH         1024
#define PANEL_HEIGHT        600

/* Shared LVGL style descriptors (initialized once) */

typedef struct
{
  bool initialized;
  lv_style_t card;           /* Base card: bg, radius, border */
  lv_style_t card_active;    /* Active/on state */
  lv_style_t card_offline;   /* Offline state */
  lv_style_t card_pressed;   /* Touch feedback */
  lv_style_t nav_button;     /* Navigation item default */
  lv_style_t nav_button_active; /* Navigation item selected */
  lv_style_t topbar;         /* Top bar background */
  lv_style_t page_bg;        /* Page content background */
  lv_style_t section_title;  /* Section heading text */
  lv_style_t body_text;      /* Normal body text */
  lv_style_t secondary_text; /* Secondary/muted text */
  lv_style_t caption_text;   /* Small caption text */
  lv_style_t key_value;      /* Large key value display */
  lv_style_t btn_primary;    /* Primary action button */
  lv_style_t btn_secondary;  /* Secondary action button */
  lv_style_t btn_exec;       /* Execute/confirm button (green) */
  lv_style_t slider_main;    /* Slider track */
  lv_style_t slider_indic;   /* Slider indicator */
  lv_style_t slider_knob;    /* Slider knob */
  lv_style_t toggle_on;      /* Switch/toggle on */
  lv_style_t toggle_off;     /* Switch/toggle off */
  home_panel_typography_t typography; /* Typography styles */
} home_panel_theme_t;

/* Initialize all shared styles (call once at startup) */

void home_panel_theme_init(void);

/* Get the global theme instance */

const home_panel_theme_t *home_panel_theme_get(void);

/* Helper: apply card style to an object */

void theme_apply_card(lv_obj_t *obj);

/* Helper: apply nav button style */

void theme_apply_nav(lv_obj_t *obj, bool selected);

/* Helper: apply topbar style */

void theme_apply_topbar(lv_obj_t *obj);

/* Helper: apply page background style */

void theme_apply_page_bg(lv_obj_t *obj);

/* Apply the low-frequency page gradient and create the local ambient mask.
 * The mask is created before page content so it remains a background layer.
 */

void theme_apply_page_atmosphere(lv_obj_t *obj, unsigned int page);
lv_obj_t *theme_create_ambient_mask(lv_obj_t *parent, unsigned int page);

/* Lightweight visual layers used throughout the panel. */

void theme_apply_surface_gradient(lv_obj_t *obj);
void theme_apply_card_state(lv_obj_t *obj,
                            enum home_panel_card_state_e state);
lv_obj_t *theme_create_device_ambient_mask(lv_obj_t *parent,
                                           const char *device_type);
void theme_apply_topbar_gradient(lv_obj_t *obj);
void theme_apply_nav_gradient(lv_obj_t *obj);
void theme_apply_scrim(lv_obj_t *obj);

/* Helper: create a styled label */

lv_obj_t *theme_create_label(lv_obj_t *parent, const char *text,
                             const lv_font_t *font, lv_color_t color);

/* Typography helpers - apply semantic text styles */

void theme_apply_page_title(lv_obj_t *label);
void theme_apply_card_title(lv_obj_t *label);
void theme_apply_body(lv_obj_t *label);
void theme_apply_body_secondary(lv_obj_t *label);
void theme_apply_caption(lv_obj_t *label);
void theme_apply_caption_small(lv_obj_t *label);
void theme_apply_key_value(lv_obj_t *label, lv_color_t accent);
void theme_apply_key_value_small(lv_obj_t *label, lv_color_t accent);
void theme_apply_time_display(lv_obj_t *label);

/* Create labels with semantic typography */

lv_obj_t *theme_create_page_title(lv_obj_t *parent, const char *text,
                                  int x, int y);
lv_obj_t *theme_create_card_title(lv_obj_t *parent, const char *text,
                                  int x, int y);
lv_obj_t *theme_create_body(lv_obj_t *parent, const char *text,
                            int x, int y);
lv_obj_t *theme_create_body_secondary(lv_obj_t *parent, const char *text,
                                      int x, int y);
lv_obj_t *theme_create_caption(lv_obj_t *parent, const char *text,
                               int x, int y);
lv_obj_t *theme_create_caption_small(lv_obj_t *parent, const char *text,
                                     int x, int y);
lv_obj_t *theme_create_key_value(lv_obj_t *parent, const char *text,
                                 int x, int y, lv_color_t accent);
lv_obj_t *theme_create_key_value_small(lv_obj_t *parent, const char *text,
                                       int x, int y, lv_color_t accent);

#endif
