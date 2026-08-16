/****************************************************************************
 * D13x home panel - Proactive intelligence page (UI only)
 *
 * Pure UI layer: widget creation, layout, display state updates.
 * Business logic lives in home_panel_proactive_controller.c.
 * Communication via view-model + callbacks.
 ****************************************************************************/

#include "home_ui_internal.h"
#include "../home_panel_proactive_controller.h"

/* Current view-model (filled by controller, read by UI) */

static struct proactive_viewmodel_s g_vm;

/* UI event callbacks - forward declarations */

static void feedback_clicked(lv_event_t *event);
static void replay_clicked(lv_event_t *event);
static void reset_clicked(lv_event_t *event);
static void return_clicked(lv_event_t *event);
static void automation_delete_clicked(lv_event_t *event);
static void confirm_close(lv_event_t *event);
static void confirm_accept(lv_event_t *event);

/* Internal helpers */

static void show_automation_confirm(void);

/****************************************************************************
 * Automation list rendering
 ****************************************************************************/

static void update_automation_list(void)
{
  unsigned int index;

  if (g_ui.proactive_automation_list == NULL)
    {
      return;
    }

  if (g_vm.automation_signature == g_ui.proactive_automation_signature)
    {
      return;
    }

  g_ui.proactive_automation_signature = g_vm.automation_signature;
  lv_obj_clean(g_ui.proactive_automation_list);

  if (g_vm.automation_count == 0)
    {
      home_ui_make_label(g_ui.proactive_automation_list,
                         "暂无离线自动化；云端候选需经你确认后才会写入",
                         0, 8, lv_color_hex(COLOR_MUTED),
                         home_panel_font_get());
      return;
    }

  for (index = 0; index < g_vm.automation_count; index++)
    {
      const struct proactive_ctrl_automation_entry_s *entry =
        &g_vm.automations[index];
      const struct home_panel_device_s *device = NULL;
      lv_obj_t *row;
      lv_obj_t *button;
      lv_obj_t *label;
      char text[192];

      /* Find device name from model */

      if (g_ui.family_model != NULL)
        {
          unsigned int di;

          for (di = 0; di < g_ui.family_model->device_count; di++)
            {
              uint32_t hash = 2166136261u;
              const unsigned char *p =
                (const unsigned char *)g_ui.family_model->devices[di].did;

              while (*p != '\0')
                {
                  hash = (hash ^ *p++) * 16777619u;
                }
              if ((hash == 0 ? 1 : hash) == entry->action_device_hash)
                {
                  device = &g_ui.family_model->devices[di];
                  break;
                }
            }
        }

      row = lv_obj_create(g_ui.proactive_automation_list);
      lv_obj_set_pos(row, 0, (int)index * 56);
      lv_obj_set_size(row, lv_pct(100), THEME_MIN_TOUCH);
      lv_obj_set_style_radius(row, THEME_RADIUS_SM, 0);
      lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_SURFACE_2), 0);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(row, 0, 0);
      lv_obj_set_style_pad_all(row, 0, 0);
      lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

      if (entry->cloud)
        {
          if (entry->event_triggered)
            {
              snprintf(text, sizeof(text),
                       "云端设备联动 延迟%u秒 · %u 个动作 · %s",
                       (unsigned int)entry->mean_delay_seconds,
                       entry->action_count,
                       device == NULL ? "设备已移除" : device->name);
            }
          else
            {
              snprintf(text, sizeof(text),
                       "云端学习 %02u:%02u · %u 个动作 · %s",
                       entry->mean_minute_of_day / 60u,
                       entry->mean_minute_of_day % 60u,
                       entry->action_count,
                       device == NULL ? "设备已移除" : device->name);
            }
        }
      else
        {
          if (entry->event_triggered)
            {
              snprintf(text, sizeof(text),
                       "设备联动%s%u 分钟 · %s → %s",
                       entry->mean_delay_seconds > 0 ? " 延迟 " : "",
                       entry->mean_delay_seconds / 60u,
                       device == NULL ? "已移除设备" : device->name,
                       entry->action_kind == HOME_PROACTIVE_EVENT_BOOLEAN ?
                         (entry->action_value != 0 ? "开启" : "关闭") :
                         "调整状态");
            }
          else
            {
              snprintf(text, sizeof(text), "%02u:%02u · %s → %s",
                       entry->mean_minute_of_day / 60u,
                       entry->mean_minute_of_day % 60u,
                       device == NULL ? "已移除设备" : device->name,
                       entry->action_kind == HOME_PROACTIVE_EVENT_BOOLEAN ?
                         (entry->action_value != 0 ? "开启" : "关闭") :
                         "调整状态");
            }
        }

      label = home_ui_make_label(row, text, 12, 16,
                                 lv_color_hex(COLOR_SECONDARY),
                                 home_panel_font_get());
      lv_obj_set_width(label, 620);
      lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

      g_ui.proactive_automation_ui[index].routine_id = entry->routine_id;
      g_ui.proactive_automation_ui[index].cloud = entry->cloud;

      button = home_ui_make_action_button(row, "删除", 650, 0, 74);
      home_ui_style_secondary_action(button);
      lv_obj_remove_event_cb(button, home_ui_action_clicked);
      lv_obj_add_event_cb(button, automation_delete_clicked,
                          LV_EVENT_CLICKED,
                          &g_ui.proactive_automation_ui[index]);
    }
}

/****************************************************************************
 * Main widget update (reads from view-model)
 ****************************************************************************/

static void apply_viewmodel_to_widgets(void)
{
  char text[384];
  bool visible;

  if (g_ui.proactive_mode_label == NULL)
    {
      return;
    }

  /* Cloud proposal active */

  if (g_vm.cloud.valid &&
      g_vm.cloud.expires_at > (uint64_t)time(NULL) &&
      (g_vm.cloud.status == HOME_PANEL_CLOUD_PROPOSAL_AWAITING_CONFIRMATION ||
       g_vm.cloud.status == HOME_PANEL_CLOUD_PROPOSAL_GENERATING_PLAN ||
       g_vm.cloud.status == HOME_PANEL_CLOUD_PROPOSAL_PLAN_READY))
    {
      lv_obj_t *button_label;

      home_ui_set_label_text_if_changed(g_ui.proactive_mode_label,
                                        "真实运行｜云端状态深度分析");
      lv_obj_set_style_text_color(g_ui.proactive_mode_label,
                                  lv_color_hex(COLOR_GREEN), 0);
      snprintf(text, sizeof(text), "%u%%", g_vm.cloud.confidence);
      home_ui_set_label_text_if_changed(g_ui.proactive_confidence_label,
                                        text);
      home_ui_set_label_text_if_changed(g_ui.proactive_suggestion_title,
                                        g_vm.cloud.purpose);
      home_ui_set_label_text_if_changed(g_ui.proactive_reason_label,
                                        g_vm.cloud.explanation);
      snprintf(text, sizeof(text),
               g_vm.cloud.kind == HOME_PANEL_CLOUD_AUTOMATION ?
                 "涉及 %u 项设备动作；确认后生成受限计划并保存到板端" :
                 "涉及 %u 项设备动作；确认后逐项执行并等待真实状态回读",
               g_vm.cloud.action_count == 0 ? g_vm.cloud.intent_count :
                                              g_vm.cloud.action_count);
      home_ui_set_label_text_if_changed(g_ui.proactive_action_label, text);

      if (g_vm.cloud.status == HOME_PANEL_CLOUD_PROPOSAL_GENERATING_PLAN)
        {
          lv_obj_add_flag(g_ui.proactive_accept_button, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(g_ui.proactive_ignore_button, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_remove_flag(g_ui.proactive_accept_button,
                             LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(g_ui.proactive_ignore_button,
                             LV_OBJ_FLAG_HIDDEN);
        }
      lv_obj_add_flag(g_ui.proactive_automation_button, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_ui.proactive_less_button, LV_OBJ_FLAG_HIDDEN);

      button_label = lv_obj_get_child(g_ui.proactive_accept_button, 0);
      if (button_label != NULL)
        {
          home_ui_set_label_text_if_changed(
            button_label,
            g_vm.cloud.kind == HOME_PANEL_CLOUD_AUTOMATION ?
              "确认并生成计划" : "仅本次执行");
        }
      update_automation_list();
      return;
    }

  /* Reset accept button label */

  if (g_ui.proactive_accept_button != NULL)
    {
      lv_obj_t *button_label =
        lv_obj_get_child(g_ui.proactive_accept_button, 0);

      if (button_label != NULL)
        {
          home_ui_set_label_text_if_changed(button_label, "执行建议");
        }
    }

  /* Mode label */

  home_ui_set_label_text_if_changed(
    g_ui.proactive_mode_label,
    g_vm.proactive.mode == HOME_PROACTIVE_REAL ?
      (g_vm.agent_matches &&
       g_vm.agent.state == HOME_PANEL_AGENT_CLOUD_READY ?
        "真实运行｜云端深度分析" :
        (g_vm.proactive.profile_learned ||
         g_vm.proactive.routine_count > 0) ?
          "真实运行｜本地持续学习" : "真实运行｜规则冷启动") :
    g_vm.proactive.mode == HOME_PROACTIVE_REPLAY ?
      "演示模式｜模拟历史数据" : "演示模式｜第 8 天");
  lv_obj_set_style_text_color(
    g_ui.proactive_mode_label,
    lv_color_hex(g_vm.proactive.mode == HOME_PROACTIVE_REAL ?
                   COLOR_GREEN : COLOR_ORANGE), 0);

  /* Stats */

  snprintf(text, sizeof(text), "%u 天", g_vm.proactive.history_days);
  home_ui_set_label_text_if_changed(g_ui.proactive_history_label, text);
  snprintf(text, sizeof(text), "%02u:%02u",
           g_vm.proactive.preferred_hour, g_vm.proactive.preferred_minute);
  home_ui_set_label_text_if_changed(g_ui.proactive_time_label, text);
  snprintf(text, sizeof(text), "%u%%",
           g_vm.agent_matches &&
             g_vm.agent.state != HOME_PANEL_AGENT_PENDING ?
               g_vm.agent.adjusted_confidence :
               g_vm.proactive.confidence);
  home_ui_set_label_text_if_changed(g_ui.proactive_confidence_label, text);

  /* Progress */

  if (g_vm.proactive.mode == HOME_PROACTIVE_REPLAY)
    {
      snprintf(text, sizeof(text), "正在回放：第 %u / 7 天",
               g_vm.proactive.replay_day);
    }
  else if (g_vm.proactive.mode == HOME_PROACTIVE_DEMO_READY)
    {
      snprintf(text, sizeof(text),
               "已学习 %u 次反馈：接受 %u，忽略 %u",
               g_vm.proactive.feedback_count,
               g_vm.proactive.accepted_count,
               g_vm.proactive.ignored_count);
    }
  else
    {
      snprintf(text, sizeof(text),
               "真实画像：%u 条习惯，%u 次观察，%u 条自动化",
               g_vm.proactive.routine_count,
               g_vm.proactive.routine_observations,
               g_vm.proactive.automation_count);
    }
  home_ui_set_label_text_if_changed(g_ui.proactive_progress_label, text);
  update_automation_list();

  /* Suggestion title */

  visible = g_vm.proactive.suggestion_available;
  home_ui_set_label_text_if_changed(
    g_ui.proactive_suggestion_title,
    !visible ? "暂无主动建议" :
    g_vm.agent_matches &&
      g_vm.agent.state == HOME_PANEL_AGENT_PENDING ?
      "主动建议｜云端分析中" :
    g_vm.agent_matches &&
      g_vm.agent.state == HOME_PANEL_AGENT_CLOUD_READY &&
      g_vm.agent.decision == HOME_PANEL_AGENT_SUPPRESS ?
      "云端建议暂缓" :
    g_vm.agent_matches &&
      g_vm.agent.state == HOME_PANEL_AGENT_CLOUD_READY &&
      g_vm.agent.decision == HOME_PANEL_AGENT_DEFER ?
      "云端建议继续观察" :
    g_vm.proactive.candidate_kind ==
      HOME_PROACTIVE_CANDIDATE_EVENT_ROUTINE ?
      "设备联动建议" :
    g_vm.proactive.candidate_kind ==
      HOME_PROACTIVE_CANDIDATE_TIME_ROUTINE ?
      "时间习惯建议" : "睡眠准备");

  /* Reason label */

  if (visible)
    {
      if (g_vm.agent_matches && g_vm.agent.summary[0] != '\0')
        {
          snprintf(text, sizeof(text), "%s%s%s",
                   g_vm.agent.summary,
                   g_vm.agent.reasons[0] != '\0' ? "；" : "",
                   g_vm.agent.reasons);
        }
      else if (g_vm.generic)
        {
          snprintf(text, sizeof(text),
                   "本地已观察到相似行为 %u 次；预测触发时间 %02u:%02u；"
                   "当前置信度 %u%%。%s",
                   g_vm.proactive.candidate_observations,
                   g_vm.proactive.preferred_hour,
                   g_vm.proactive.preferred_minute,
                   g_vm.proactive.confidence,
                   g_vm.proactive.candidate_kind ==
                     HOME_PROACTIVE_CANDIDATE_EVENT_ROUTINE ?
                     "本次由真实设备状态变化触发。" :
                     "本次由长期时间规律触发。");
        }
      else
        {
          snprintf(text, sizeof(text),
                   "触发依据：当前处于常用睡眠时段；%u 盏灯仍开启；"
                   "相似情境接受率 %u%%。",
                   g_vm.proactive.lights_on,
                   g_vm.proactive.feedback_count == 0 ? 0 :
                   g_vm.proactive.accepted_count * 100 /
                   g_vm.proactive.feedback_count);
        }
      home_ui_set_label_text_if_changed(g_ui.proactive_reason_label, text);

      /* Action label */

      if (g_vm.cloud_blocks)
        {
          snprintf(text, sizeof(text),
                   g_vm.agent.state == HOME_PANEL_AGENT_PENDING ?
                     "等待云端决策；超时后自动切换本地算法" :
                     "本次不下发设备操作，板端继续观察状态");
        }
      else if (g_vm.generic && g_vm.target_available)
        {
          if (g_vm.action_already_satisfied)
            {
              snprintf(text, sizeof(text), "%s 已处于建议状态",
                       g_vm.action_device_name);
            }
          else if (g_vm.proactive.action_is_boolean)
            {
              snprintf(text, sizeof(text), "建议将 %s 设置为%s",
                       g_vm.action_device_name,
                       g_vm.proactive.action_value != 0 ? "开启" : "关闭");
            }
          else
            {
              snprintf(text, sizeof(text), "建议将 %s 的 %s 调整为 %d",
                       g_vm.action_device_name,
                       g_vm.action_control_name,
                       g_vm.proactive.action_value);
            }
        }
      else if (g_vm.generic)
        {
          snprintf(text, sizeof(text),
                   "目标设备当前离线、已删除或属性已不可写，本次不会执行");
        }
      else
        {
          snprintf(text, sizeof(text), "建议关闭 %s%s",
                   g_vm.proactive.target_available ?
                     g_vm.proactive.target_name : "当前灯光",
                   g_vm.proactive.air_conditioner_on ?
                     "，保留空调运行" : "");
        }
      home_ui_set_label_text_if_changed(g_ui.proactive_action_label, text);
    }
  else
    {
      home_ui_set_label_text_if_changed(
        g_ui.proactive_reason_label,
        g_vm.proactive.mode == HOME_PROACTIVE_REPLAY ?
          "正在压缩回放七天事件，算法与真实运行使用同一条事件链。" :
        !g_vm.proactive.clock_valid ?
          "等待网络校时完成；时间无效时不会产生真实主动建议。" :
          "系统会结合时间、设备状态和你的反馈生成建议。");
      home_ui_set_label_text_if_changed(g_ui.proactive_action_label,
                                        "不会未经确认控制家庭设备");
    }

  /* Button visibility */

  if (g_ui.proactive_accept_button != NULL)
    {
      if (g_vm.actionable)
        {
          lv_obj_remove_flag(g_ui.proactive_accept_button,
                             LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_ui.proactive_accept_button, LV_OBJ_FLAG_HIDDEN);
        }
      if (visible)
        {
          lv_obj_remove_flag(g_ui.proactive_ignore_button,
                             LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(g_ui.proactive_less_button,
                             LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_ui.proactive_ignore_button, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(g_ui.proactive_less_button, LV_OBJ_FLAG_HIDDEN);
        }
    }

  if (g_ui.proactive_automation_button != NULL)
    {
      if (g_vm.actionable && g_vm.generic &&
          !g_vm.proactive.automation_enabled)
        {
          lv_obj_remove_flag(g_ui.proactive_automation_button,
                             LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_ui.proactive_automation_button,
                          LV_OBJ_FLAG_HIDDEN);
        }
    }

  if (g_ui.proactive_return_button != NULL)
    {
      if (g_vm.proactive.mode == HOME_PROACTIVE_REAL)
        {
          lv_obj_add_flag(g_ui.proactive_return_button, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_remove_flag(g_ui.proactive_return_button,
                             LV_OBJ_FLAG_HIDDEN);
        }
    }

  if (g_ui.proactive_reset_button != NULL)
    {
      if (g_vm.proactive.mode == HOME_PROACTIVE_REAL)
        {
          lv_obj_remove_flag(g_ui.proactive_reset_button,
                             LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_ui.proactive_reset_button, LV_OBJ_FLAG_HIDDEN);
        }
    }

  if (g_ui.proactive_replay_button != NULL)
    {
      if (g_vm.proactive.mode == HOME_PROACTIVE_REAL)
        {
          lv_obj_remove_flag(g_ui.proactive_replay_button,
                             LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_ui.proactive_replay_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/****************************************************************************
 * UI event callbacks
 ****************************************************************************/

static void feedback_clicked(lv_event_t *event)
{
  enum home_proactive_feedback_e feedback =
    (enum home_proactive_feedback_e)(uintptr_t)lv_event_get_user_data(event);

  proactive_ctrl_handle_feedback(feedback, false, &g_vm);
  if (g_vm.feedback_message[0] != '\0')
    {
      home_ui_set_label_text_if_changed(g_ui.proactive_feedback_label,
                                        g_vm.feedback_message);
    }
  apply_viewmodel_to_widgets();
  home_page_update_suggestion();
}

static void replay_clicked(lv_event_t *event)
{
  (void)event;
  proactive_ctrl_start_replay(lv_tick_get());
  home_ui_set_label_text_if_changed(g_ui.proactive_feedback_label,
                                    "演示数据不会写入真实用户画像");
  proactive_ctrl_get_viewmodel(&g_vm);
  apply_viewmodel_to_widgets();
}

static void reset_clicked(lv_event_t *event)
{
  (void)event;
  if (g_vm.proactive.mode != HOME_PROACTIVE_REAL)
    {
      home_ui_set_label_text_if_changed(g_ui.proactive_feedback_label,
                                        "请先返回真实模式再重置画像");
      return;
    }
  proactive_ctrl_reset();
  proactive_ctrl_update_context();
  proactive_ctrl_schedule_persist();
  home_ui_set_label_text_if_changed(g_ui.proactive_feedback_label,
                                    "真实用户画像已重置");
  proactive_ctrl_get_viewmodel(&g_vm);
  apply_viewmodel_to_widgets();
}

static void return_clicked(lv_event_t *event)
{
  (void)event;
  proactive_ctrl_stop_replay();
  proactive_ctrl_update_context();
  home_ui_set_label_text_if_changed(g_ui.proactive_feedback_label,
                                    "已恢复回放前的真实画像");
  proactive_ctrl_get_viewmodel(&g_vm);
  apply_viewmodel_to_widgets();
}

static void automation_delete_clicked(lv_event_t *event)
{
  struct home_ui_proactive_automation_ui_s *binding =
    lv_event_get_user_data(event);
  int ret;

  if (binding == NULL || binding->routine_id == 0)
    {
      return;
    }

  ret = proactive_ctrl_delete_automation(binding->routine_id, binding->cloud);
  if (ret == 0)
    {
      home_ui_set_label_text_if_changed(g_ui.proactive_feedback_label,
                                        "离线自动化已删除并停止执行");
      g_ui.proactive_automation_signature = 0;
      proactive_ctrl_get_viewmodel(&g_vm);
      apply_viewmodel_to_widgets();
    }
  else
    {
      home_ui_set_label_text_if_changed(g_ui.proactive_feedback_label,
                                        "自动化已不存在，无需重复删除");
    }
}

static void confirm_close(lv_event_t *event)
{
  lv_obj_t *shade = lv_event_get_user_data(event);

  if (shade != NULL && shade == g_ui.proactive_confirm_shade)
    {
      g_ui.proactive_confirm_shade = NULL;
      g_ui.proactive_confirm_key = 0;
      lv_obj_delete(shade);
    }
}

static void confirm_accept(lv_event_t *event)
{
  lv_obj_t *shade = lv_event_get_user_data(event);
  uint32_t expected_key = g_ui.proactive_confirm_key;

  if (shade == NULL || shade != g_ui.proactive_confirm_shade)
    {
      return;
    }

  g_ui.proactive_confirm_shade = NULL;
  g_ui.proactive_confirm_key = 0;
  lv_obj_delete(shade);

  /* Refresh view-model and check if suggestion is still valid */

  proactive_ctrl_get_viewmodel(&g_vm);
  if (!g_vm.proactive.suggestion_available ||
      g_vm.proactive.decision_key != expected_key)
    {
      home_ui_set_label_text_if_changed(g_ui.proactive_feedback_label,
                                        "建议已发生变化，请重新确认");
      apply_viewmodel_to_widgets();
      return;
    }

  proactive_ctrl_handle_feedback(HOME_PROACTIVE_ENABLE_AUTOMATION, true,
                                 &g_vm);
  if (g_vm.feedback_message[0] != '\0')
    {
      home_ui_set_label_text_if_changed(g_ui.proactive_feedback_label,
                                        g_vm.feedback_message);
    }
  apply_viewmodel_to_widgets();
  home_page_update_suggestion();
}

static void show_automation_confirm(void)
{
  lv_obj_t *dialog;
  lv_obj_t *label;
  lv_obj_t *button;
  char detail[192];

  if (g_ui.proactive_confirm_shade != NULL)
    {
      return;
    }

  g_ui.proactive_confirm_shade = component_create_dialog(
    NULL, 560, 286, "建立离线自动化？");
  if (g_ui.proactive_confirm_shade == NULL)
    {
      return;
    }

  g_ui.proactive_confirm_key = g_vm.proactive.decision_key;
  dialog = lv_obj_get_child(g_ui.proactive_confirm_shade, 0);
  if (dialog == NULL)
    {
      component_close_dialog(g_ui.proactive_confirm_shade);
      g_ui.proactive_confirm_shade = NULL;
      g_ui.proactive_confirm_key = 0;
      return;
    }

  snprintf(detail, sizeof(detail),
           "确认后，本建议会写入开发板并可在断网时自动执行。\n"
           "目标：%s\n可随时在[主动智能]页面删除。",
           g_vm.proactive.target_name[0] != '\0' ?
             g_vm.proactive.target_name : "当前建议设备");
  label = home_ui_make_label(dialog, detail, 20, 70,
                             lv_color_hex(COLOR_SECONDARY),
                             home_panel_font_get());
  lv_obj_set_width(label, 472);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(label, 8, 0);

  button = home_ui_make_action_button(dialog, "取消", 196, 186, 136);
  home_ui_style_secondary_action(button);
  lv_obj_remove_event_cb(button, home_ui_action_clicked);
  lv_obj_add_event_cb(button, confirm_close, LV_EVENT_CLICKED,
                      g_ui.proactive_confirm_shade);

  button = home_ui_make_action_button(dialog, "确认并启用", 348, 186, 164);
  lv_obj_remove_event_cb(button, home_ui_action_clicked);
  lv_obj_add_event_cb(button, confirm_accept, LV_EVENT_CLICKED,
                      g_ui.proactive_confirm_shade);
}

/****************************************************************************
 * Public API
 ****************************************************************************/

void proactive_page_create(void)
{
  lv_obj_t *panel;
  const int left_margin = THEME_CONTENT_PAD;
  const int content_width = PANEL_WIDTH - NAV_WIDTH - THEME_CONTENT_PAD * 2;

  /* Page title */

  theme_create_page_title(g_ui.content, "主动智能", left_margin, 16);
  home_ui_make_label(g_ui.content, "本地持续学习", left_margin, 50,
                     lv_color_hex(COLOR_SECONDARY), home_panel_font_get());

  /* Mode indicator */

  g_ui.proactive_mode_label = home_ui_make_label(
    g_ui.content, "真实运行", left_margin + 208, 20,
    lv_color_hex(COLOR_GREEN), home_panel_font_get());

  /* Main suggestion card */

  panel = lv_obj_create(g_ui.content);
  lv_obj_set_pos(panel, left_margin, 76);
  lv_obj_set_size(panel, content_width, 226);
  lv_obj_set_style_radius(panel, THEME_RADIUS_CARD, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  theme_apply_surface_gradient(panel);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_BORDER), 0);
  lv_obj_set_style_shadow_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 24, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  g_ui.proactive_suggestion_title = home_ui_make_label(
    panel, "暂无主动建议", 0, 0, lv_color_hex(COLOR_TEXT),
    home_panel_font_get());

  g_ui.proactive_reason_label = home_ui_make_label(
    panel, "系统会结合时间、设备状态和你的反馈生成建议。",
    0, 48, lv_color_hex(COLOR_SECONDARY), home_panel_font_get());
  lv_obj_set_width(g_ui.proactive_reason_label, content_width - 48);
  lv_label_set_long_mode(g_ui.proactive_reason_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(g_ui.proactive_reason_label, 8, 0);

  g_ui.proactive_action_label = home_ui_make_label(
    panel, "不会未经确认控制家庭设备", 0, 126,
    lv_color_hex(COLOR_SENSOR), home_panel_font_get());

  g_ui.proactive_confidence_label = home_ui_make_label(
    panel, "", content_width - 120, 0,
    lv_color_hex(COLOR_SECONDARY), home_panel_font_get());

  /* Action buttons */

  g_ui.proactive_accept_button = home_ui_make_action_button(
    g_ui.content, "执行建议", left_margin, 316, 160);
  lv_obj_remove_event_cb(g_ui.proactive_accept_button,
                         home_ui_action_clicked);
  lv_obj_add_event_cb(g_ui.proactive_accept_button, feedback_clicked,
                      LV_EVENT_CLICKED,
                      (void *)(uintptr_t)HOME_PROACTIVE_ACCEPT);

  g_ui.proactive_automation_button = home_ui_make_action_button(
    g_ui.content, "设为自动", left_margin + 176, 316, 160);
  lv_obj_remove_event_cb(g_ui.proactive_automation_button,
                         home_ui_action_clicked);
  lv_obj_add_event_cb(g_ui.proactive_automation_button, feedback_clicked,
                      LV_EVENT_CLICKED,
                      (void *)(uintptr_t)HOME_PROACTIVE_ENABLE_AUTOMATION);

  g_ui.proactive_ignore_button = home_ui_make_action_button(
    g_ui.content, "忽略", left_margin + 352, 316, 120);
  home_ui_style_secondary_action(g_ui.proactive_ignore_button);
  lv_obj_remove_event_cb(g_ui.proactive_ignore_button,
                         home_ui_action_clicked);
  lv_obj_add_event_cb(g_ui.proactive_ignore_button, feedback_clicked,
                      LV_EVENT_CLICKED,
                      (void *)(uintptr_t)HOME_PROACTIVE_IGNORE_TODAY);

  g_ui.proactive_less_button = home_ui_make_action_button(
    g_ui.content, "以后少提醒", left_margin + 488, 316, 160);
  home_ui_style_secondary_action(g_ui.proactive_less_button);
  lv_obj_remove_event_cb(g_ui.proactive_less_button,
                         home_ui_action_clicked);
  lv_obj_add_event_cb(g_ui.proactive_less_button, feedback_clicked,
                      LV_EVENT_CLICKED,
                      (void *)(uintptr_t)HOME_PROACTIVE_LESS_OFTEN);

  /* Feedback label */

  g_ui.proactive_feedback_label = home_ui_make_label(
    g_ui.content, "", left_margin, 382,
    lv_color_hex(COLOR_MUTED), home_panel_font_get());
  lv_obj_set_width(g_ui.proactive_feedback_label, content_width);
  lv_label_set_long_mode(g_ui.proactive_feedback_label, LV_LABEL_LONG_WRAP);

  /* Automation status row */

  {
    lv_obj_t *status_row = lv_obj_create(g_ui.content);

    lv_obj_set_pos(status_row, left_margin, 426);
    lv_obj_set_size(status_row, content_width, 92);
    lv_obj_set_style_radius(status_row, THEME_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(status_row, lv_color_hex(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_COVER, 0);
    theme_apply_surface_gradient(status_row);
    lv_obj_set_style_border_width(status_row, 1, 0);
    lv_obj_set_style_border_color(status_row, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_shadow_width(status_row, 0, 0);
    lv_obj_set_style_pad_all(status_row, 12, 0);
    lv_obj_clear_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);

    home_ui_make_label(status_row, "离线自动化", 0, 0,
                       lv_color_hex(COLOR_TEXT), home_panel_font_get());
    g_ui.proactive_history_label = home_ui_make_label(
      status_row, "--", 120, 0, lv_color_hex(COLOR_MUTED),
      home_panel_font_get());
    g_ui.proactive_time_label = home_ui_make_label(
      status_row, "--", 190, 0, lv_color_hex(COLOR_MUTED),
      home_panel_font_get());
    g_ui.proactive_progress_label = home_ui_make_label(
      status_row, "", 260, 0, lv_color_hex(COLOR_SECONDARY),
      home_panel_font_get());

    g_ui.proactive_automation_list = lv_obj_create(status_row);
    lv_obj_set_pos(g_ui.proactive_automation_list, 0, 28);
    lv_obj_set_size(g_ui.proactive_automation_list, content_width - 24,
                    THEME_MIN_TOUCH);
    lv_obj_set_style_bg_opa(g_ui.proactive_automation_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_ui.proactive_automation_list, 0, 0);
    lv_obj_set_style_pad_all(g_ui.proactive_automation_list, 0, 0);
    lv_obj_set_scroll_dir(g_ui.proactive_automation_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_ui.proactive_automation_list,
                              LV_SCROLLBAR_MODE_AUTO);
    g_ui.proactive_automation_signature = 0;
  }

  /* Admin buttons */

  g_ui.proactive_replay_button = home_ui_make_action_button(
    g_ui.content, "回放 7 天", left_margin + content_width - 328, 12, 152);
  home_ui_style_secondary_action(g_ui.proactive_replay_button);
  lv_obj_remove_event_cb(g_ui.proactive_replay_button,
                         home_ui_action_clicked);
  lv_obj_add_event_cb(g_ui.proactive_replay_button, replay_clicked,
                      LV_EVENT_CLICKED, NULL);

  g_ui.proactive_return_button = home_ui_make_action_button(
    g_ui.content, "返回真实", left_margin + content_width - 160, 12, 152);
  home_ui_style_secondary_action(g_ui.proactive_return_button);
  lv_obj_remove_event_cb(g_ui.proactive_return_button,
                         home_ui_action_clicked);
  lv_obj_add_event_cb(g_ui.proactive_return_button, return_clicked,
                      LV_EVENT_CLICKED, NULL);

  g_ui.proactive_reset_button = home_ui_make_action_button(
    g_ui.content, "重置画像", left_margin + content_width - 160, 12, 152);
  home_ui_style_secondary_action(g_ui.proactive_reset_button);
  lv_obj_remove_event_cb(g_ui.proactive_reset_button,
                         home_ui_action_clicked);
  lv_obj_add_event_cb(g_ui.proactive_reset_button, reset_clicked,
                      LV_EVENT_CLICKED, NULL);

  /* Initial render */

  proactive_ctrl_get_viewmodel(&g_vm);
  apply_viewmodel_to_widgets();
  g_ui.status_label = g_ui.proactive_feedback_label;
}

void proactive_page_update_widgets(void)
{
  proactive_ctrl_get_viewmodel(&g_vm);
  apply_viewmodel_to_widgets();
  home_page_update_suggestion();
}

void proactive_page_show_confirm(void)
{
  show_automation_confirm();
}
