/****************************************************************************
 * D13x home panel - Settings page
 ****************************************************************************/

#include "home_ui_internal.h"

void settings_page_create(lv_event_cb_t network_refresh_cb)
{
  lv_obj_t *panel;
  lv_obj_t *section;
  char online_text[32];
  const int left_margin = THEME_CONTENT_PAD;
  const int content_width = PANEL_WIDTH - NAV_WIDTH - THEME_CONTENT_PAD * 2;

  /* Page title */

  theme_create_page_title(g_ui.content, "设置", left_margin, 16);
  home_ui_make_label(g_ui.content, "网络、账号与系统状态", left_margin, 50,
                     lv_color_hex(COLOR_SECONDARY), home_panel_font_get());

  /* Network section */

  section = lv_obj_create(g_ui.content);
  lv_obj_set_pos(section, left_margin, 88);
  lv_obj_set_size(section, content_width, 180);
  lv_obj_set_style_radius(section, THEME_RADIUS_CARD, 0);
  lv_obj_set_style_bg_color(section, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_bg_opa(section, LV_OPA_COVER, 0);
  theme_apply_surface_gradient(section);
  lv_obj_set_style_border_width(section, 1, 0);
  lv_obj_set_style_border_color(section, lv_color_hex(COLOR_BORDER), 0);
  lv_obj_set_style_shadow_width(section, 0, 0);
  lv_obj_set_style_pad_all(section, 16, 0);
  lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);

  home_ui_make_label(section, "网络", 0, 0, lv_color_hex(COLOR_TEXT),
                     home_panel_font_get());

  g_ui.settings_network_label = home_ui_make_info_row(
    section, "有线网络", "有线网络未连接", 36,
    lv_color_hex(COLOR_WARNING));
  g_ui.settings_probe_label = home_ui_make_info_row(
    section, "互联网检测", "等待网线连接", 76,
    lv_color_hex(COLOR_WARNING));
  home_ui_make_info_row(section, "地址获取", "DHCP 自动", 116,
                        lv_color_hex(COLOR_TEXT));

  /* Account section */

  panel = lv_obj_create(g_ui.content);
  lv_obj_set_pos(panel, left_margin, 284);
  lv_obj_set_size(panel, content_width, 156);
  lv_obj_set_style_radius(panel, THEME_RADIUS_CARD, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  theme_apply_surface_gradient(panel);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_BORDER), 0);
  lv_obj_set_style_shadow_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 16, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  home_ui_make_label(panel, "米家账户", 0, 0, lv_color_hex(COLOR_TEXT),
                     home_panel_font_get());

  g_ui.settings_account_label = home_ui_make_info_row(
    panel, "当前家庭", "未登录", 36, lv_color_hex(COLOR_MUTED));

  if (g_ui.family_model_valid)
    {
      snprintf(online_text, sizeof(online_text), "%u / %u 台在线",
               g_ui.family_model->online_count,
               g_ui.family_model->device_count);
    }
  else
    {
      snprintf(online_text, sizeof(online_text), "等待同步");
    }

  g_ui.settings_online_label = home_ui_make_info_row(
    panel, "在线设备", online_text, 76,
    lv_color_hex(g_ui.family_model_valid ? COLOR_TEXT : COLOR_MUTED));

  /* Status channel */

  g_ui.settings_channel_label = home_ui_make_info_row(
    panel, "状态通道",
    g_ui.family_model->mqtt_connected ? "MQTT 实时推送" :
    (g_ui.family_model->event_source[0] != '\0' ?
     "云端定向回读" : "等待米家同步"),
    116, g_ui.family_model->mqtt_connected ?
         lv_color_hex(COLOR_TEXT) : lv_color_hex(COLOR_SENSOR));

  /* Bottom row: status + refresh button */

  g_ui.status_label = home_ui_make_label(
    g_ui.content, "系统每分钟自动检测网络",
    left_margin, 466, lv_color_hex(COLOR_MUTED), home_panel_font_get());

  if (network_refresh_cb != NULL)
    {
      panel = home_ui_make_action_button(
        g_ui.content, "重新检测网络",
        left_margin + content_width - 192, 458, 192);
      lv_obj_remove_event_cb(panel, home_ui_action_clicked);
      lv_obj_add_event_cb(panel, network_refresh_cb, LV_EVENT_CLICKED, NULL);
    }

  settings_page_update_network(g_ui.network_state);
}

void settings_page_update_network(enum home_ui_network_state_e state)
{
  const char *settings_text;
  const char *probe_text;
  lv_color_t color;

  if (state == HOME_UI_NET_ONLINE)
    {
      settings_text = "已连接互联网";
      probe_text = "mi.com / xiaomi.cn 可达";
      color = lv_color_hex(COLOR_SECONDARY);
    }
  else if (state == HOME_UI_NET_NO_INTERNET)
    {
      settings_text = "无互联网连接";
      probe_text = "mi.com / xiaomi.cn 不可达";
      color = lv_color_hex(COLOR_WARNING);
    }
  else if (state == HOME_UI_NET_CHECKING)
    {
      settings_text = "网线已连接";
      probe_text = "正在检测互联网";
      color = lv_color_hex(COLOR_BLUE);
    }
  else if (state == HOME_UI_NET_INITIALIZING)
    {
      settings_text = "正在初始化有线网络";
      probe_text = "等待网络接口启动";
      color = lv_color_hex(COLOR_BLUE);
    }
  else
    {
      settings_text = "有线网络未连接";
      probe_text = "等待网线连接";
      color = lv_color_hex(COLOR_WARNING);
    }

  if (g_ui.settings_network_label != NULL)
    {
      lv_label_set_text(g_ui.settings_network_label, settings_text);
      lv_obj_set_style_text_color(g_ui.settings_network_label, color, 0);
    }

  if (g_ui.settings_probe_label != NULL)
    {
      lv_label_set_text(g_ui.settings_probe_label, probe_text);
      lv_obj_set_style_text_color(g_ui.settings_probe_label, color, 0);
    }
}
