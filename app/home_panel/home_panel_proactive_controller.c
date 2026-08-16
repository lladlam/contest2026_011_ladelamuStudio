/****************************************************************************
 * D13x home panel - Proactive intelligence controller
 *
 * Business logic extracted from home_panel_main.c.
 * Cloud analysis, automation execution, MIoT target resolution,
 * command tracking, profile persistence, and safety checks.
 *
 * Communicates with page_proactive.c via view-model + callbacks.
 * Does NOT touch the learning algorithm (home_panel_proactive.c).
 ****************************************************************************/

#include <nuttx/config.h>

#include <arch/board/board.h>
#include <errno.h>
#include <nuttx/arch.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include <lvgl/lvgl.h>

#include "home_panel_proactive_controller.h"
#include "home_panel_mijia_client.h"
#include "home_panel_mijia_model.h"
#include "home_panel_proactive.h"

/****************************************************************************
 * Constants (moved from main.c)
 ****************************************************************************/

#define PROACTIVE_PROFILE_SIZE    768
#define AGENT_PERSIST_SIZE        3072
#define AGENT_PERSIST_MAGIC       0x48415031u /* HAP1 */
#define AGENT_PERSIST_VERSION     1u
#define CLOUD_AUTOMATION_MAX      PROACTIVE_CTRL_CLOUD_MAX
#define PROACTIVE_THREAD_STACK    8192
#define PROACTIVE_PERSIST_RETRY_MS 1000
#define CLOUD_FEEDBACK_QUEUE_MAX  4
#define CLOUD_STATE_UPLOAD_MS     60000
#define CLOUD_PROPOSAL_POLL_MS    10000
#define TIME_VALID_EPOCH          1735689600

/****************************************************************************
 * Type aliases to preserve original naming
 ****************************************************************************/

#define cloud_automation_action_s  proactive_ctrl_cloud_action_s
#define cloud_automation_record_s  proactive_ctrl_cloud_record_s

/****************************************************************************
 * Internal structs (moved from main.c)
 ****************************************************************************/

struct agent_persist_record_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint16_t profile_length;
  uint8_t cloud_count;
  uint8_t reserved;
  uint8_t profile[PROACTIVE_PROFILE_SIZE];
  struct cloud_automation_record_s cloud[CLOUD_AUTOMATION_MAX];
};

struct cloud_execution_s
{
  bool active;
  bool one_time;
  unsigned int action_index;
  struct cloud_automation_record_s record;
  char proposal_id[65];
};

struct cloud_feedback_s
{
  char proposal_id[65];
  char result[24];
};

struct proactive_persist_work_s
{
  size_t length;
  uint8_t data[AGENT_PERSIST_SIZE];
};

struct proactive_action_target_s
{
  const struct home_panel_device_s *device;
  const struct home_panel_control_s *control;
  int current_value;
  bool available;
  bool already_satisfied;
};

struct proactive_command_tracker_s
{
  uint32_t device_hash;
  uint32_t decision_key;
  uint32_t deadline_ms;
  uint16_t siid;
  uint16_t piid;
  int value;
  enum home_proactive_event_kind_e kind;
  bool active;
  bool automation;
  bool feedback_pending;
  enum home_proactive_feedback_e feedback_on_success;
  bool cloud_execution;
};

/****************************************************************************
 * Static variables (moved from main.c)
 ****************************************************************************/

static struct cloud_automation_record_s
  g_cloud_automations[CLOUD_AUTOMATION_MAX];
static unsigned int g_cloud_automation_count;
static struct cloud_execution_s g_cloud_execution;
static uint32_t g_cloud_due_at[CLOUD_AUTOMATION_MAX];
static uint32_t g_cloud_state_revision;
static uint32_t g_last_cloud_state_upload;
static uint32_t g_last_cloud_proposal_poll;
static uint32_t g_cloud_plan_handled_revision;
static struct cloud_feedback_s
  g_cloud_feedback_queue[CLOUD_FEEDBACK_QUEUE_MAX];
static unsigned int g_cloud_feedback_count;
static struct proactive_command_tracker_s g_proactive_command;
static bool g_proactive_persist_busy;
static bool g_proactive_persist_pending;
static uint32_t g_proactive_persist_retry_at;
static uint32_t g_requested_proactive_key;

/* Controller bridge state */

static const struct home_panel_family_model_s *g_ctrl_family_model;
static bool g_ctrl_network_online;
static uint32_t g_ctrl_automation_signature;
static char g_last_feedback_message[160];

/****************************************************************************
 * Forward declarations
 ****************************************************************************/

static uint32_t proactive_hash_device_id(const char *did);
static const struct home_panel_device_s *proactive_find_device(
  const struct home_panel_family_model_s *model, const char *did);
static const struct home_panel_observable_s *proactive_find_observable(
  const struct home_panel_device_s *device, uint16_t siid, uint16_t piid);
static const struct home_panel_control_s *proactive_find_control(
  const struct home_panel_device_s *device, uint16_t siid, uint16_t piid);
static bool proactive_safe_action_device(
  const struct home_panel_device_s *device);
static bool proactive_safe_action_control(
  const struct home_panel_control_s *control);
static const struct home_panel_device_s *cloud_find_device(uint32_t hash);
static bool cloud_number_allowed(const struct home_panel_control_s *control,
                                 int value);
static bool proactive_event_time(uint32_t *day_ordinal,
                                  uint16_t *minute_of_day);
static bool proactive_command_matches(
  const struct home_proactive_event_s *event);
static bool proactive_resolve_action(
  const struct home_proactive_snapshot_s *snapshot,
  struct proactive_action_target_s *target);
static int proactive_request_action(
  const struct home_proactive_snapshot_s *snapshot,
  const struct proactive_action_target_s *target,
  bool automation,
  enum home_proactive_feedback_e feedback_on_success,
  bool feedback_pending);
static uint64_t proactive_monotonic_ms(void);
static bool proactive_agent_matches(
  const struct home_panel_agent_snapshot_s *agent,
  const struct home_proactive_snapshot_s *snapshot);
static void schedule_proactive_persist(void);

/****************************************************************************
 * Helper functions
 ****************************************************************************/

static bool text_contains(const char *text, const char *needle)
{
  return text != NULL && needle != NULL && strstr(text, needle) != NULL;
}

/****************************************************************************
 * Device/control lookup helpers (moved from main.c)
 ****************************************************************************/

static uint32_t proactive_hash_device_id(const char *did)
{
  const unsigned char *text = (const unsigned char *)did;
  uint32_t hash = 2166136261u;

  if (did == NULL)
    {
      return 0;
    }

  while (*text != '\0')
    {
      hash = (hash ^ *text++) * 16777619u;
    }

  return hash == 0 ? 1 : hash;
}

static const struct home_panel_device_s *proactive_find_device(
  const struct home_panel_family_model_s *model, const char *did)
{
  unsigned int index;

  if (model == NULL || did == NULL)
    {
      return NULL;
    }

  for (index = 0; index < model->device_count; index++)
    {
      if (strcmp(model->devices[index].did, did) == 0)
        {
          return &model->devices[index];
        }
    }

  return NULL;
}

static const struct home_panel_observable_s *proactive_find_observable(
  const struct home_panel_device_s *device, uint16_t siid, uint16_t piid)
{
  unsigned int index;

  if (device == NULL)
    {
      return NULL;
    }

  for (index = 0; index < device->observable_count; index++)
    {
      if (device->observables[index].siid == siid &&
          device->observables[index].piid == piid)
        {
          return &device->observables[index];
        }
    }

  return NULL;
}

static const struct home_panel_control_s *proactive_find_control(
  const struct home_panel_device_s *device, uint16_t siid, uint16_t piid)
{
  unsigned int index;

  if (device == NULL)
    {
      return NULL;
    }

  for (index = 0; index < device->control_count; index++)
    {
      if (device->controls[index].siid == siid &&
          device->controls[index].piid == piid)
        {
          return &device->controls[index];
        }
    }

  return NULL;
}

static bool proactive_safe_action_device(
  const struct home_panel_device_s *device)
{
  static const char *const allowed_types[] =
  {
    "light", "outlet", "switch", "speaker", "air-conditioner",
    "heater", "fan", "curtain"
  };
  unsigned int index;

  if (device == NULL)
    {
      return false;
    }

  for (index = 0;
       index < sizeof(allowed_types) / sizeof(allowed_types[0]); index++)
    {
      if (strcmp(device->type, allowed_types[index]) == 0)
        {
          return true;
        }
    }

  return false;
}

static bool proactive_safe_action_control(
  const struct home_panel_control_s *control)
{
  static const char *const allowed_properties[] =
  {
    "on", "brightness", "color-temperature", "volume",
    "target-temperature", "fan-level", "mode", "target-position",
    "playing-state"
  };
  unsigned int index;

  if (control == NULL || control->name[0] == '\0' ||
      control->siid == 0 || control->piid == 0)
    {
      return false;
    }

  for (index = 0;
       index < sizeof(allowed_properties) /
               sizeof(allowed_properties[0]); index++)
    {
      if (strcmp(control->name, allowed_properties[index]) == 0)
        {
          return true;
        }
    }

  return false;
}

static const struct home_panel_device_s *cloud_find_device(uint32_t hash)
{
  unsigned int index;

  if (g_ctrl_family_model == NULL)
    {
      return NULL;
    }

  for (index = 0; index < g_ctrl_family_model->device_count; index++)
    {
      if (proactive_hash_device_id(
            g_ctrl_family_model->devices[index].did) == hash)
        {
          return &g_ctrl_family_model->devices[index];
        }
    }
  return NULL;
}

static bool cloud_number_allowed(const struct home_panel_control_s *control,
                                 int value)
{
  unsigned int index;

  if (control->type == HOME_PANEL_CONTROL_ENUM)
    {
      for (index = 0; index < control->option_count; index++)
        {
          if (control->options[index].value == value)
            {
              return true;
            }
        }
      return false;
    }
  return control->type == HOME_PANEL_CONTROL_NUMBER && control->has_range &&
         value >= control->minimum && value <= control->maximum &&
         (control->step <= 1 ||
          (value - control->minimum) % control->step == 0);
}

static const struct home_panel_control_s *cloud_resolve_action(
  const struct home_panel_cloud_action_s *action,
  const struct home_panel_device_s **device_out)
{
  const struct home_panel_device_s *device;
  const struct home_panel_control_s *control;

  device = cloud_find_device(action->device_key);
  if (device == NULL || !device->online ||
      !proactive_safe_action_device(device))
    {
      return NULL;
    }
  control = proactive_find_control(device, action->siid, action->piid);
  if (control == NULL || !control->has_value ||
      strcmp(control->name, action->semantic) != 0 ||
      !proactive_safe_action_control(control) ||
      (action->value_is_boolean ?
        control->type != HOME_PANEL_CONTROL_BOOLEAN :
        !cloud_number_allowed(control, action->value)))
    {
      return NULL;
    }
  *device_out = device;
  return control;
}

static bool cloud_resolve_trigger(
  const struct home_panel_cloud_trigger_s *trigger,
  struct cloud_automation_record_s *record)
{
  const struct home_panel_device_s *device;
  unsigned int index;

  if (trigger->kind == HOME_PANEL_CLOUD_TRIGGER_TIME)
    {
      if (trigger->minute_of_day >= 1440 || trigger->window_minutes == 0 ||
          trigger->window_minutes > 120 || trigger->days_mask == 0)
        {
          return false;
        }
      record->trigger_kind = HOME_PANEL_CLOUD_TRIGGER_TIME;
      record->minute_of_day = trigger->minute_of_day;
      record->window_minutes = trigger->window_minutes;
      record->days_mask = trigger->days_mask;
      return true;
    }
  if (trigger->kind != HOME_PANEL_CLOUD_TRIGGER_PROPERTY)
    {
      return false;
    }
  device = cloud_find_device(trigger->device_key);
  if (device == NULL)
    {
      return false;
    }
  for (index = 0; index < device->observable_count; index++)
    {
      const struct home_panel_observable_s *observable =
        &device->observables[index];

      if (strcmp(observable->name, trigger->semantic) == 0 &&
          observable->has_value &&
          ((trigger->value_is_boolean &&
            observable->type == HOME_PANEL_CONTROL_BOOLEAN) ||
           (!trigger->value_is_boolean &&
            observable->type == HOME_PANEL_CONTROL_NUMBER)))
        {
          record->trigger_kind = HOME_PANEL_CLOUD_TRIGGER_PROPERTY;
          record->trigger_device_hash = trigger->device_key;
          record->trigger_siid = observable->siid;
          record->trigger_piid = observable->piid;
          record->trigger_value = trigger->value;
          record->trigger_value_is_boolean = trigger->value_is_boolean;
          record->delay_seconds = trigger->delay_seconds;
          return true;
        }
    }
  for (index = 0; index < device->control_count; index++)
    {
      const struct home_panel_control_s *control = &device->controls[index];

      if (strcmp(control->name, trigger->semantic) == 0 &&
          control->has_value &&
          ((trigger->value_is_boolean &&
            control->type == HOME_PANEL_CONTROL_BOOLEAN) ||
           (!trigger->value_is_boolean &&
            (control->type == HOME_PANEL_CONTROL_NUMBER ||
             control->type == HOME_PANEL_CONTROL_ENUM))))
        {
          record->trigger_kind = HOME_PANEL_CLOUD_TRIGGER_PROPERTY;
          record->trigger_device_hash = trigger->device_key;
          record->trigger_siid = control->siid;
          record->trigger_piid = control->piid;
          record->trigger_value = trigger->value;
          record->trigger_value_is_boolean = trigger->value_is_boolean;
          record->delay_seconds = trigger->delay_seconds;
          return true;
        }
    }
  return false;
}

static bool cloud_build_record(
  const struct home_panel_cloud_proposal_s *proposal,
  struct cloud_automation_record_s *record, bool require_trigger)
{
  unsigned int index;

  memset(record, 0, sizeof(*record));
  if (proposal == NULL || !proposal->valid || proposal->action_count == 0 ||
      proposal->action_count > HOME_PANEL_CLOUD_MAX_ACTIONS)
    {
      return false;
    }
  record->routine_id = proactive_hash_device_id(proposal->proposal_id);
  record->action_count = proposal->action_count;
  for (index = 0; index < proposal->action_count; index++)
    {
      const struct home_panel_device_s *device;
      const struct home_panel_control_s *control =
        cloud_resolve_action(&proposal->actions[index], &device);

      if (control == NULL)
        {
          return false;
        }
      record->actions[index].device_hash =
        proactive_hash_device_id(device->did);
      record->actions[index].siid = control->siid;
      record->actions[index].piid = control->piid;
      record->actions[index].value = proposal->actions[index].value;
      record->actions[index].value_is_boolean =
        proposal->actions[index].value_is_boolean;
    }
  return !require_trigger || cloud_resolve_trigger(&proposal->trigger, record);
}

/****************************************************************************
 * Cloud feedback queue (moved from main.c)
 ****************************************************************************/

static void cloud_queue_feedback(const char *proposal_id,
                                 const char *result)
{
  struct cloud_feedback_s *feedback;

  if (proposal_id == NULL || strlen(proposal_id) < 16)
    {
      return;
    }
  if (g_cloud_feedback_count > 0)
    {
      feedback = &g_cloud_feedback_queue[g_cloud_feedback_count - 1];
      if (strcmp(feedback->proposal_id, proposal_id) == 0 &&
          strcmp(feedback->result, result == NULL ? "" : result) == 0)
        {
          return;
        }
    }
  if (g_cloud_feedback_count >= CLOUD_FEEDBACK_QUEUE_MAX)
    {
      syslog(LOG_WARNING,
             "[HOME][AGENT] feedback queue full result=%s\n",
             result == NULL ? "" : result);
      return;
    }
  feedback = &g_cloud_feedback_queue[g_cloud_feedback_count++];
  snprintf(feedback->proposal_id, sizeof(feedback->proposal_id), "%s",
           proposal_id);
  snprintf(feedback->result, sizeof(feedback->result), "%s",
           result == NULL ? "" : result);
}

/****************************************************************************
 * Time helpers (moved from main.c)
 ****************************************************************************/

static bool proactive_event_time(uint32_t *day_ordinal,
                                  uint16_t *minute_of_day)
{
  struct tm local_time;
  time_t now = time(NULL);

  if (now < TIME_VALID_EPOCH)
    {
      return false;
    }

  now += 8 * 60 * 60;
  *day_ordinal = (uint32_t)(now / (24 * 60 * 60));
  gmtime_r(&now, &local_time);
  *minute_of_day =
    (uint16_t)(local_time.tm_hour * 60 + local_time.tm_min);
  return true;
}

/****************************************************************************
 * Command tracker helpers (moved from main.c)
 ****************************************************************************/

static bool proactive_command_matches(
  const struct home_proactive_event_s *event)
{
  if (!g_proactive_command.active ||
      (int32_t)(event->now_ms - g_proactive_command.deadline_ms) > 0 ||
      event->device_hash != g_proactive_command.device_hash ||
      event->siid != g_proactive_command.siid ||
      event->piid != g_proactive_command.piid ||
      event->kind != g_proactive_command.kind ||
      event->value != g_proactive_command.value)
    {
      return false;
    }

  return true;
}

/****************************************************************************
 * Persistence (moved from main.c)
 ****************************************************************************/

_Static_assert(sizeof(struct agent_persist_record_s) <= AGENT_PERSIST_SIZE,
               "agent persistence record exceeds QSPI slot payload");

static int build_agent_persist_record(void *buffer, size_t capacity,
                                       size_t *length)
{
  struct agent_persist_record_s *record = buffer;
  int ret;

  if (buffer == NULL || length == NULL || capacity < sizeof(*record))
    {
      return -ENOSPC;
    }
  memset(record, 0, sizeof(*record));
  record->magic = AGENT_PERSIST_MAGIC;
  record->version = AGENT_PERSIST_VERSION;
  record->size = sizeof(*record);
  ret = home_proactive_export_profile(record->profile,
                                      sizeof(record->profile), length);
  if (ret < 0 || *length > UINT16_MAX)
    {
      memset(record, 0, sizeof(*record));
      return ret < 0 ? ret : -EOVERFLOW;
    }
  record->profile_length = (uint16_t)*length;
  record->cloud_count = g_cloud_automation_count;
  memcpy(record->cloud, g_cloud_automations,
         sizeof(g_cloud_automations));
  *length = sizeof(*record);
  return 0;
}

static void *proactive_persist_worker(void *arg)
{
  struct proactive_persist_work_s *work = arg;
  irqstate_t flags;
  int ret = board_agent_persist_write(work->data, work->length);

  syslog(ret == 0 ? LOG_INFO : LOG_WARNING,
         "[HOME][AGENT] profile persist ret=%d bytes=%u\n",
         ret, (unsigned int)work->length);
  free(work);
  flags = up_irq_save();
  g_proactive_persist_busy = false;
  up_irq_restore(flags);
  return NULL;
}

static void schedule_proactive_persist(void)
{
  struct proactive_persist_work_s *work;
  struct sched_param param;
  irqstate_t flags;
  pthread_attr_t attr;
  pthread_t thread;
  int ret;

  flags = up_irq_save();
  if (g_proactive_persist_busy)
    {
      g_proactive_persist_pending = true;
      up_irq_restore(flags);
      return;
    }
  g_proactive_persist_busy = true;
  g_proactive_persist_pending = false;
  up_irq_restore(flags);

  work = calloc(1, sizeof(*work));
  if (work == NULL)
    {
      ret = -ENOMEM;
      goto fail;
    }

  ret = build_agent_persist_record(work->data, sizeof(work->data),
                                   &work->length);
  if (ret < 0)
    {
      free(work);
      work = NULL;
      goto fail;
    }

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, PROACTIVE_THREAD_STACK);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_RR);
  memset(&param, 0, sizeof(param));
  param.sched_priority = 45;
  pthread_attr_setschedparam(&attr, &param);
  ret = pthread_create(&thread, &attr, proactive_persist_worker, work);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      free(work);
      goto fail;
    }
  return;

fail:
  flags = up_irq_save();
  g_proactive_persist_busy = false;
  g_proactive_persist_pending = true;
  g_proactive_persist_retry_at =
    lv_tick_get() + PROACTIVE_PERSIST_RETRY_MS;
  up_irq_restore(flags);
  syslog(LOG_WARNING,
         "[HOME][AGENT] profile worker start failed ret=%d\n", ret);
}

/****************************************************************************
 * Cloud automation record validation (moved from main.c)
 ****************************************************************************/

static bool cloud_persist_record_valid(
  const struct cloud_automation_record_s *record)
{
  unsigned int index;

  if (record == NULL || record->routine_id == 0 ||
      record->action_count == 0 ||
      record->action_count > HOME_PANEL_CLOUD_MAX_ACTIONS)
    {
      return false;
    }
  if (record->trigger_kind == HOME_PANEL_CLOUD_TRIGGER_TIME)
    {
      if (record->minute_of_day >= 1440 || record->window_minutes == 0 ||
          record->window_minutes > 120 || record->days_mask == 0)
        {
          return false;
        }
    }
  else if (record->trigger_kind == HOME_PANEL_CLOUD_TRIGGER_PROPERTY)
    {
      if (record->trigger_device_hash == 0 || record->trigger_siid == 0 ||
          record->trigger_piid == 0 || record->delay_seconds > 3600)
        {
          return false;
        }
    }
  else
    {
      return false;
    }
  for (index = 0; index < record->action_count; index++)
    {
      if (record->actions[index].device_hash == 0 ||
          record->actions[index].siid == 0 ||
          record->actions[index].piid == 0)
        {
          return false;
        }
    }
  return true;
}

/****************************************************************************
 * Action resolution (moved from main.c)
 ****************************************************************************/

static uint64_t proactive_monotonic_ms(void)
{
  struct timespec value;

  if (clock_gettime(CLOCK_MONOTONIC, &value) < 0)
    {
      return 0;
    }
  return (uint64_t)value.tv_sec * 1000u +
         (uint64_t)value.tv_nsec / 1000000u;
}

static bool proactive_agent_matches(
  const struct home_panel_agent_snapshot_s *agent,
  const struct home_proactive_snapshot_s *snapshot)
{
  uint64_t now;

  if (agent->context_revision != snapshot->decision_key ||
      agent->state == HOME_PANEL_AGENT_IDLE)
    {
      return false;
    }
  if (!snapshot->network_online &&
      (agent->state == HOME_PANEL_AGENT_PENDING ||
       agent->state == HOME_PANEL_AGENT_CLOUD_READY))
    {
      return false;
    }
  now = proactive_monotonic_ms();
  return agent->valid_until_monotonic_ms != 0 &&
         (now == 0 || now < agent->valid_until_monotonic_ms);
}

static const struct home_panel_device_s *proactive_find_device_hash(
  uint32_t hash)
{
  unsigned int index;

  if (g_ctrl_family_model == NULL)
    {
      return NULL;
    }

  for (index = 0; index < g_ctrl_family_model->device_count; index++)
    {
      if (proactive_hash_device_id(
            g_ctrl_family_model->devices[index].did) == hash)
        {
          return &g_ctrl_family_model->devices[index];
        }
    }
  return NULL;
}

static bool proactive_resolve_action(
  const struct home_proactive_snapshot_s *snapshot,
  struct proactive_action_target_s *target)
{
  unsigned int index;

  memset(target, 0, sizeof(*target));
  if (snapshot->candidate_kind == HOME_PROACTIVE_CANDIDATE_SLEEP)
    {
      return false;
    }

  if (g_ctrl_family_model == NULL)
    {
      return false;
    }

  for (index = 0; index < g_ctrl_family_model->device_count; index++)
    {
      const struct home_panel_device_s *device =
        &g_ctrl_family_model->devices[index];
      const struct home_panel_control_s *control;

      if (proactive_hash_device_id(device->did) !=
          snapshot->action_device_hash)
        {
          continue;
        }

      control = proactive_find_control(device, snapshot->action_siid,
                                       snapshot->action_piid);
      if (control == NULL || !control->has_value || !device->online ||
          !proactive_safe_action_device(device) ||
          !proactive_safe_action_control(control) ||
          (snapshot->action_is_boolean &&
           control->type != HOME_PANEL_CONTROL_BOOLEAN) ||
          (!snapshot->action_is_boolean &&
           control->type != HOME_PANEL_CONTROL_NUMBER))
        {
          return false;
        }

      target->device = device;
      target->control = control;
      target->current_value =
        control->type == HOME_PANEL_CONTROL_BOOLEAN ?
          (control->boolean_value ? 1 : 0) : control->value;
      target->available = true;
      target->already_satisfied =
        target->current_value == snapshot->action_value;
      return true;
    }

  return false;
}

static int proactive_request_action(
  const struct home_proactive_snapshot_s *snapshot,
  const struct proactive_action_target_s *target,
  bool automation,
  enum home_proactive_feedback_e feedback_on_success,
  bool feedback_pending)
{
  int ret;

  if (!target->available || target->already_satisfied)
    {
      return target->already_satisfied ? 1 : -ENODEV;
    }

  if (snapshot->action_is_boolean)
    {
      ret = home_panel_mijia_request_bool_property(
        target->device->did, target->device->name,
        target->control->name, target->control->siid,
        target->control->piid, snapshot->action_value != 0);
    }
  else
    {
      if (!target->control->has_range ||
          snapshot->action_value < target->control->minimum ||
          snapshot->action_value > target->control->maximum)
        {
          return -ERANGE;
        }
      ret = home_panel_mijia_request_number_property(
        target->device->did, target->device->name,
        target->control->name, target->control->siid,
        target->control->piid, snapshot->action_value);
    }

  if (ret == 0)
    {
      memset(&g_proactive_command, 0, sizeof(g_proactive_command));
      g_proactive_command.device_hash = snapshot->action_device_hash;
      g_proactive_command.decision_key = snapshot->decision_key;
      g_proactive_command.deadline_ms = lv_tick_get() + 15000u;
      g_proactive_command.siid = snapshot->action_siid;
      g_proactive_command.piid = snapshot->action_piid;
      g_proactive_command.value = snapshot->action_value;
      g_proactive_command.kind =
        snapshot->action_is_boolean ? HOME_PROACTIVE_EVENT_BOOLEAN :
                                      HOME_PROACTIVE_EVENT_NUMBER;
      g_proactive_command.active = true;
      g_proactive_command.automation = automation;
      g_proactive_command.feedback_pending = feedback_pending;
      g_proactive_command.feedback_on_success = feedback_on_success;
    }

  return ret;
}

/****************************************************************************
 * Automation signature (moved from main.c)
 ****************************************************************************/

static uint32_t compute_automation_signature(
  const struct home_proactive_automation_s *items,
  unsigned int count)
{
  uint32_t hash = 2166136261u;
  unsigned int index;

  for (index = 0; index < count; index++)
    {
      hash = (hash ^ items[index].routine_id) * 16777619u;
      hash = (hash ^ items[index].trigger_device_hash) * 16777619u;
      hash = (hash ^ items[index].action_device_hash) * 16777619u;
      hash = (hash ^ items[index].action_siid) * 16777619u;
      hash = (hash ^ items[index].action_piid) * 16777619u;
      hash = (hash ^ items[index].mean_minute_of_day) * 16777619u;
      hash = (hash ^ items[index].mean_delay_seconds) * 16777619u;
      hash = (hash ^ items[index].action_kind) * 16777619u;
      hash = (hash ^ (items[index].event_triggered ? 1u : 0u)) *
             16777619u;
      hash = (hash ^ (uint32_t)items[index].action_value) * 16777619u;
    }
  return (hash ^ count) == 0 ? 1 : (hash ^ count);
}

/****************************************************************************
 * Cloud execution (moved from main.c)
 ****************************************************************************/

static void cloud_finish_execution(bool success)
{
  if (!g_cloud_execution.active)
    {
      return;
    }
  cloud_queue_feedback(g_cloud_execution.proposal_id,
                       success ? "execution_succeeded" :
                                 "execution_failed");
  if (!g_cloud_execution.one_time && success)
    {
      unsigned int index;
      uint32_t day;
      uint16_t minute;

      if (proactive_event_time(&day, &minute))
        {
          for (index = 0; index < g_cloud_automation_count; index++)
            {
              if (g_cloud_automations[index].routine_id ==
                  g_cloud_execution.record.routine_id)
                {
                  g_cloud_automations[index].last_executed_day = day;
                  break;
                }
            }
          schedule_proactive_persist();
        }
    }
  memset(&g_cloud_execution, 0, sizeof(g_cloud_execution));
}

static int cloud_send_next_action(void)
{
  while (g_cloud_execution.active &&
         g_cloud_execution.action_index <
           g_cloud_execution.record.action_count)
    {
      const struct cloud_automation_action_s *action =
        &g_cloud_execution.record.actions[g_cloud_execution.action_index];
      const struct home_panel_device_s *device =
        cloud_find_device(action->device_hash);
      const struct home_panel_control_s *control;
      int current;
      int ret;

      if (device == NULL || !device->online ||
          !proactive_safe_action_device(device))
        {
          return -ENODEV;
        }
      control = proactive_find_control(device, action->siid, action->piid);
      if (control == NULL || !proactive_safe_action_control(control) ||
          !control->has_value ||
          (action->value_is_boolean ?
            control->type != HOME_PANEL_CONTROL_BOOLEAN :
            !cloud_number_allowed(control, action->value)))
        {
          return -EPERM;
        }
      current = control->type == HOME_PANEL_CONTROL_BOOLEAN ?
                  (control->boolean_value ? 1 : 0) : control->value;
      if (current == action->value)
        {
          g_cloud_execution.action_index++;
          continue;
        }
      ret = action->value_is_boolean ?
        home_panel_mijia_request_bool_property(
          device->did, device->name, control->name,
          control->siid, control->piid, action->value != 0) :
        home_panel_mijia_request_number_property(
          device->did, device->name, control->name,
          control->siid, control->piid, action->value);
      if (ret != 0)
        {
          return ret;
        }
      memset(&g_proactive_command, 0, sizeof(g_proactive_command));
      g_proactive_command.device_hash = action->device_hash;
      g_proactive_command.deadline_ms = lv_tick_get() + 15000u;
      g_proactive_command.siid = action->siid;
      g_proactive_command.piid = action->piid;
      g_proactive_command.value = action->value;
      g_proactive_command.kind = action->value_is_boolean ?
        HOME_PROACTIVE_EVENT_BOOLEAN : HOME_PROACTIVE_EVENT_NUMBER;
      g_proactive_command.active = true;
      g_proactive_command.cloud_execution = true;
      return 0;
    }
  if (g_cloud_execution.active)
    {
      cloud_finish_execution(true);
    }
  return 1;
}

static bool cloud_start_execution(
  const struct cloud_automation_record_s *record, bool one_time,
  const char *proposal_id)
{
  int ret;

  if (record == NULL || g_cloud_execution.active ||
      g_proactive_command.active)
    {
      return false;
    }
  memset(&g_cloud_execution, 0, sizeof(g_cloud_execution));
  g_cloud_execution.active = true;
  g_cloud_execution.one_time = one_time;
  g_cloud_execution.record = *record;
  snprintf(g_cloud_execution.proposal_id,
           sizeof(g_cloud_execution.proposal_id), "%s",
           proposal_id == NULL ? "" : proposal_id);
  ret = cloud_send_next_action();
  if (ret < 0 && ret != -EBUSY)
    {
      cloud_finish_execution(false);
      return false;
    }
  return true;
}

/****************************************************************************
 * Cloud proposal processing (moved from main.c)
 ****************************************************************************/

static void process_cloud_proposal(void)
{
  struct home_panel_cloud_proposal_s proposal;
  struct cloud_automation_record_s record;
  unsigned int index;
  bool changed = false;

  home_panel_mijia_get_board_proposal(&proposal);
  if (!proposal.valid || proposal.kind != HOME_PANEL_CLOUD_AUTOMATION ||
      proposal.status != HOME_PANEL_CLOUD_PROPOSAL_PLAN_READY)
    {
      return;
    }
  if (proposal.update_revision == g_cloud_plan_handled_revision)
    {
      return;
    }
  if (!cloud_build_record(&proposal, &record, true))
    {
      g_cloud_plan_handled_revision = proposal.update_revision;
      cloud_queue_feedback(proposal.proposal_id, "execution_failed");
      snprintf(g_last_feedback_message, sizeof(g_last_feedback_message),
               "服务端计划未通过板端能力与安全校验，未保存");
      return;
    }
  for (index = 0; index < g_cloud_automation_count; index++)
    {
      if (g_cloud_automations[index].routine_id == record.routine_id)
        {
          if (memcmp(&g_cloud_automations[index], &record,
                     sizeof(record)) != 0)
            {
              g_cloud_automations[index] = record;
              changed = true;
            }
          break;
        }
    }
  if (index == g_cloud_automation_count)
    {
      if (g_cloud_automation_count >= CLOUD_AUTOMATION_MAX)
        {
          g_cloud_plan_handled_revision = proposal.update_revision;
          cloud_queue_feedback(proposal.proposal_id, "execution_failed");
          snprintf(g_last_feedback_message, sizeof(g_last_feedback_message),
                   "离线自动化已满，请先删除旧规则");
          return;
        }
      g_cloud_automations[g_cloud_automation_count++] = record;
      changed = true;
    }
  if (changed)
    {
      schedule_proactive_persist();
      g_ctrl_automation_signature = 0;
    }
  g_cloud_plan_handled_revision = proposal.update_revision;
  cloud_queue_feedback(proposal.proposal_id, "automation_saved");
  snprintf(g_last_feedback_message, sizeof(g_last_feedback_message),
           "多设备自动化已保存，可在断网时运行");
}

/****************************************************************************
 * Cloud automation tick processing (moved from main.c)
 ****************************************************************************/

static void process_cloud_automations(void)
{
  uint32_t day;
  uint16_t minute;
  unsigned int index;

  if (g_cloud_feedback_count > 0)
    {
      const struct cloud_feedback_s *feedback = &g_cloud_feedback_queue[0];
      int ret = home_panel_mijia_feedback_board_proposal(
        feedback->proposal_id, feedback->result);
      if (ret == 0)
        {
          g_cloud_feedback_count--;
          if (g_cloud_feedback_count > 0)
            {
              memmove(&g_cloud_feedback_queue[0],
                      &g_cloud_feedback_queue[1],
                      sizeof(g_cloud_feedback_queue[0]) *
                        g_cloud_feedback_count);
            }
          memset(&g_cloud_feedback_queue[g_cloud_feedback_count], 0,
                 sizeof(g_cloud_feedback_queue[0]));
        }
    }
  process_cloud_proposal();
  if (g_cloud_execution.active)
    {
      if (!g_proactive_command.active)
        {
          int ret = cloud_send_next_action();
          if (ret < 0 && ret != -EBUSY)
            {
              cloud_finish_execution(false);
            }
        }
      return;
    }
  if (g_proactive_command.active || !proactive_event_time(&day, &minute))
    {
      return;
    }
  for (index = 0; index < g_cloud_automation_count; index++)
    {
      struct cloud_automation_record_s *record =
        &g_cloud_automations[index];
      bool due = false;

      if (record->last_executed_day == day)
        {
          continue;
        }
      if (record->trigger_kind == HOME_PANEL_CLOUD_TRIGGER_TIME)
        {
          time_t now = time(NULL) + 8 * 60 * 60;
          struct tm current;
          unsigned int weekday;

          gmtime_r(&now, &current);
          weekday = (current.tm_wday + 6) % 7;
          due = (record->days_mask & (1u << weekday)) != 0 &&
                ((record->minute_of_day + record->window_minutes < 1440 &&
                  minute >= record->minute_of_day &&
                  minute <= record->minute_of_day +
                            record->window_minutes) ||
                 (record->minute_of_day + record->window_minutes >= 1440 &&
                  (minute >= record->minute_of_day ||
                   minute <= record->minute_of_day +
                             record->window_minutes - 1440)));
        }
      else if (record->trigger_kind == HOME_PANEL_CLOUD_TRIGGER_PROPERTY &&
               g_cloud_due_at[index] != 0 &&
               (int32_t)(lv_tick_get() - g_cloud_due_at[index]) >= 0)
        {
          due = true;
          g_cloud_due_at[index] = 0;
        }
      if (due && cloud_start_execution(record, false, NULL))
        {
          syslog(LOG_INFO,
                 "[HOME][AGENT] offline cloud automation=%u actions=%u\n",
                 (unsigned int)record->routine_id,
                 (unsigned int)record->action_count);
          break;
        }
    }
}

/****************************************************************************
 * Event observation (moved from main.c)
 ****************************************************************************/

static void proactive_observe_model_event(
  struct home_proactive_event_s *event)
{
  struct home_proactive_snapshot_s before;
  struct home_proactive_snapshot_s after;
  bool command_match;

  home_proactive_get_snapshot(&before);
  command_match = proactive_command_matches(event);
  event->self_generated = command_match;
  home_proactive_observe_event(event);
  if (!command_match)
    {
      unsigned int index;

      for (index = 0; index < g_cloud_automation_count; index++)
        {
          const struct cloud_automation_record_s *record =
            &g_cloud_automations[index];

          if (record->trigger_kind ==
                HOME_PANEL_CLOUD_TRIGGER_PROPERTY &&
              record->trigger_device_hash == event->device_hash &&
              record->trigger_siid == event->siid &&
              record->trigger_piid == event->piid &&
              record->trigger_value == event->value &&
              ((record->trigger_value_is_boolean &&
                event->kind == HOME_PROACTIVE_EVENT_BOOLEAN) ||
               (!record->trigger_value_is_boolean &&
                event->kind == HOME_PROACTIVE_EVENT_NUMBER)))
            {
              g_cloud_due_at[index] =
                event->now_ms + record->delay_seconds * 1000u;
            }
        }
    }
  if (command_match)
    {
      if (g_proactive_command.cloud_execution)
        {
          g_cloud_execution.action_index++;
        }
      else if (g_proactive_command.automation)
        {
          home_proactive_automation_result(true);
        }
      if (g_proactive_command.feedback_pending)
        {
          home_proactive_feedback(
            g_proactive_command.feedback_on_success);
          schedule_proactive_persist();
          syslog(LOG_INFO,
                 "[HOME][AGENT] confirmed feedback=%u key=%u\n",
                 (unsigned int)g_proactive_command.feedback_on_success,
                 (unsigned int)g_proactive_command.decision_key);
        }
      memset(&g_proactive_command, 0, sizeof(g_proactive_command));
    }
  home_proactive_get_snapshot(&after);

  if (after.mode == HOME_PROACTIVE_REAL &&
      (after.routine_count != before.routine_count ||
       after.routine_observations != before.routine_observations ||
       after.automation_count != before.automation_count))
    {
      schedule_proactive_persist();
      syslog(LOG_INFO,
             "[HOME][AGENT] learned routines=%u observations=%u "
             "automations=%u\n",
             after.routine_count, after.routine_observations,
             after.automation_count);
    }
}

/****************************************************************************
 * Public API: controller lifecycle
 ****************************************************************************/

void proactive_ctrl_initialize(void)
{
  memset(g_cloud_automations, 0, sizeof(g_cloud_automations));
  g_cloud_automation_count = 0;
  memset(&g_cloud_execution, 0, sizeof(g_cloud_execution));
  memset(g_cloud_due_at, 0, sizeof(g_cloud_due_at));
  g_cloud_state_revision = 0;
  g_last_cloud_state_upload = 0;
  g_last_cloud_proposal_poll = 0;
  g_cloud_plan_handled_revision = 0;
  memset(g_cloud_feedback_queue, 0, sizeof(g_cloud_feedback_queue));
  g_cloud_feedback_count = 0;
  memset(&g_proactive_command, 0, sizeof(g_proactive_command));
  g_proactive_persist_busy = false;
  g_proactive_persist_pending = false;
  g_proactive_persist_retry_at = 0;
  g_requested_proactive_key = 0;
  g_ctrl_automation_signature = 0;
  memset(g_last_feedback_message, 0, sizeof(g_last_feedback_message));
}

void proactive_ctrl_set_family_model(
  const struct home_panel_family_model_s *model)
{
  g_ctrl_family_model = model;
}

void proactive_ctrl_set_network_online(bool online)
{
  g_ctrl_network_online = online;
}

/****************************************************************************
 * Public API: profile loading (moved from main.c load_proactive_profile)
 ****************************************************************************/

void proactive_ctrl_load_profile(void)
{
  uint8_t data[AGENT_PERSIST_SIZE];
  size_t length = 0;
  int ret;

  home_proactive_initialize();
  ret = board_agent_persist_read(data, sizeof(data), &length);
  if (ret == 0)
    {
      const struct agent_persist_record_s *record =
        (const struct agent_persist_record_s *)data;

      if (length == sizeof(*record) &&
          record->magic == AGENT_PERSIST_MAGIC &&
          record->version == AGENT_PERSIST_VERSION &&
          record->size == sizeof(*record) &&
          record->profile_length > 0 &&
          record->profile_length <= sizeof(record->profile) &&
          record->cloud_count <= CLOUD_AUTOMATION_MAX)
        {
          ret = home_proactive_import_profile(record->profile,
                                               record->profile_length);
          if (ret == 0)
            {
              unsigned int index;

              g_cloud_automation_count = 0;
              memset(g_cloud_automations, 0,
                     sizeof(g_cloud_automations));
              for (index = 0; index < record->cloud_count; index++)
                {
                  if (cloud_persist_record_valid(&record->cloud[index]))
                    {
                      g_cloud_automations[g_cloud_automation_count++] =
                        record->cloud[index];
                    }
                }
            }
        }
      else
        {
          /* Records written before HAP1 contain only the local profile. */

          ret = home_proactive_import_profile(data, length);
        }
    }

  syslog(LOG_INFO, "[HOME][AGENT] profile %s ret=%d bytes=%u\n",
         ret == 0 ? "restored" : "cold-start", ret,
         (unsigned int)length);
}

/****************************************************************************
 * Public API: context update (moved from main.c update_proactive_context)
 ****************************************************************************/

void proactive_ctrl_update_context(void)
{
  struct home_proactive_context_s context;
  bool target_is_living_room = false;
  time_t now = time(NULL);
  struct tm local_time;
  unsigned int index;

  memset(&context, 0, sizeof(context));
  context.network_online = g_ctrl_network_online;
  if (now >= TIME_VALID_EPOCH)
    {
      context.clock_valid = true;
      now += 8 * 60 * 60;
      context.day_ordinal = (uint32_t)(now / (24 * 60 * 60));
      gmtime_r(&now, &local_time);
      context.minute_of_day = local_time.tm_hour * 60 + local_time.tm_min;
    }
  else
    {
      context.day_ordinal = 1;
      context.minute_of_day = 23 * 60 + 10;
    }

  if (g_ctrl_family_model == NULL)
    {
      home_proactive_set_context(&context);
      return;
    }

  for (index = 0; index < g_ctrl_family_model->device_count; index++)
    {
      const struct home_panel_device_s *device =
        &g_ctrl_family_model->devices[index];
      bool is_light = text_contains(device->type, "light") ||
                      text_contains(device->model, ".light.") ||
                      text_contains(device->name, "灯");
      bool is_air_conditioner =
        text_contains(device->type, "air-conditioner") ||
        text_contains(device->model, "aircondition") ||
        text_contains(device->name, "空调");

      if (is_air_conditioner && device->has_power && device->power)
        {
          context.air_conditioner_on = true;
        }

      if (!is_light || !device->has_power || !device->power)
        {
          continue;
        }

      context.lights_on++;
      if (device->online && device->power_writable &&
          (!context.target_available ||
           (!target_is_living_room && strcmp(device->room, "客厅") == 0)))
        {
          context.target_available = true;
          target_is_living_room = strcmp(device->room, "客厅") == 0;
          snprintf(context.target_did, sizeof(context.target_did), "%s",
                   device->did);
          snprintf(context.target_name, sizeof(context.target_name), "%s",
                   device->name);
          context.target_siid = device->power_siid;
          context.target_piid = device->power_piid;
        }
    }

  home_proactive_set_context(&context);
}

/****************************************************************************
 * Public API: persist helpers
 ****************************************************************************/

bool proactive_ctrl_persist_retry_ready(void)
{
  irqstate_t flags;
  bool ready;

  flags = up_irq_save();
  ready = g_proactive_persist_pending && !g_proactive_persist_busy &&
          (int32_t)(lv_tick_get() - g_proactive_persist_retry_at) >= 0;
  up_irq_restore(flags);
  return ready;
}

void proactive_ctrl_schedule_persist(void)
{
  schedule_proactive_persist();
}

/****************************************************************************
 * Public API: cloud sync requests (moved from main.c)
 ****************************************************************************/

void proactive_ctrl_request_cloud_analysis(
  enum home_panel_mijia_state_e mijia_state)
{
  struct home_panel_agent_request_s request;
  struct home_panel_agent_snapshot_s current;
  struct proactive_action_target_s target;
  struct home_proactive_snapshot_s snapshot;
  struct home_proactive_snapshot_s *snap_ptr = &snapshot;
  bool request_current = false;
  bool target_available;
  int ret;

  home_proactive_get_snapshot(&snapshot);

  if (snap_ptr != NULL &&
      g_requested_proactive_key == snap_ptr->decision_key)
    {
      home_panel_mijia_get_agent_snapshot(&current);
      request_current = proactive_agent_matches(&current, snap_ptr);
    }

  target_available =
    snap_ptr != NULL &&
    snap_ptr->candidate_kind != HOME_PROACTIVE_CANDIDATE_SLEEP ?
      proactive_resolve_action(snap_ptr, &target) :
      snap_ptr != NULL && snap_ptr->target_available;

  if (snap_ptr == NULL || !snap_ptr->suggestion_available ||
      snap_ptr->automation_enabled ||
      !snap_ptr->network_online || !target_available ||
      mijia_state != HOME_PANEL_MIJIA_AUTHENTICATED ||
      request_current)
    {
      return;
    }

  memset(&request, 0, sizeof(request));
  request.context_revision = snap_ptr->decision_key;
  request.routine_id = snap_ptr->routine_id;
  request.source = (unsigned int)snap_ptr->source;
  request.candidate_kind = (unsigned int)snap_ptr->candidate_kind;
  request.minute_of_day = snap_ptr->current_minute_of_day;
  request.local_confidence = snap_ptr->confidence;
  request.routine_observations = snap_ptr->candidate_observations;
  request.routine_accepted = snap_ptr->candidate_accepted;
  request.routine_rejected = snap_ptr->candidate_rejected;
  request.history_days = snap_ptr->history_days;
  request.feedback_count = snap_ptr->feedback_count;
  request.accepted_count = snap_ptr->accepted_count;
  request.ignored_count = snap_ptr->ignored_count;
  request.lights_on = snap_ptr->lights_on;
  request.local_eligible = snap_ptr->suggestion_available &&
                           target_available;
  request.automation_enabled = snap_ptr->automation_enabled;
  request.air_conditioner_on = snap_ptr->air_conditioner_on;
  request.network_online = snap_ptr->network_online;
  request.demo = snap_ptr->mode != HOME_PROACTIVE_REAL;
  ret = home_panel_mijia_request_agent_analysis(&request);
  if (ret == 0)
    {
      g_requested_proactive_key = snap_ptr->decision_key;
      syslog(LOG_INFO,
             "[HOME][AGENT] cloud analysis requested key=%u source=%u\n",
             (unsigned int)snap_ptr->decision_key,
             (unsigned int)snap_ptr->source);
    }
}

void proactive_ctrl_request_cloud_learning(
  enum home_panel_mijia_state_e mijia_state)
{
  struct home_panel_agent_learning_request_s request;
  struct home_proactive_snapshot_s snapshot;
  int ret;

  home_proactive_get_snapshot(&snapshot);

  if (snapshot.mode != HOME_PROACTIVE_REAL ||
      snapshot.learning_revision == 0 ||
      snapshot.learning_routine_id == 0 ||
      snapshot.learning_observations == 0 ||
      !snapshot.network_online ||
      mijia_state != HOME_PANEL_MIJIA_AUTHENTICATED)
    {
      return;
    }

  memset(&request, 0, sizeof(request));
  request.profile_revision = snapshot.learning_revision;
  request.routine_id = snapshot.learning_routine_id;
  request.routine_kind = (unsigned int)snapshot.learning_kind;
  request.update_kind =
    (unsigned int)snapshot.learning_update_kind;
  request.observation_count = snapshot.learning_observations;
  request.accepted_count = snapshot.learning_accepted;
  request.rejected_count = snapshot.learning_rejected;
  request.confidence = snapshot.learning_confidence;
  request.mean_minute_of_day = snapshot.learning_mean_minute;
  request.mean_deviation_minutes =
    snapshot.learning_deviation_minutes;
  request.mean_delay_seconds = snapshot.learning_delay_seconds;
  request.execution_success_count =
    snapshot.learning_execution_successes;
  request.execution_failure_count =
    snapshot.learning_execution_failures;
  request.consecutive_rejections =
    snapshot.learning_consecutive_rejections;
  request.automation_enabled =
    snapshot.learning_automation_enabled;
  ret = home_panel_mijia_request_agent_learning(&request);
  if (ret == -EALREADY)
    {
      if (home_proactive_ack_learning(snapshot.learning_revision,
                                      snapshot.learning_routine_id))
        {
          syslog(LOG_INFO,
                 "[HOME][AGENT] learning acknowledged revision=%u "
                 "routine=%u\n",
                 (unsigned int)snapshot.learning_revision,
                 (unsigned int)snapshot.learning_routine_id);
        }
    }
}

void proactive_ctrl_request_board_sync(
  enum home_panel_mijia_state_e mijia_state)
{
  struct home_panel_cloud_proposal_s proposal;
  uint32_t now_ms = lv_tick_get();
  time_t now;
  struct tm local;
  int ret;

  if (g_ctrl_family_model == NULL || !g_ctrl_network_online ||
      mijia_state != HOME_PANEL_MIJIA_AUTHENTICATED)
    {
      return;
    }
  now = time(NULL);
  if (now < TIME_VALID_EPOCH)
    {
      return;
    }
  home_panel_mijia_get_board_proposal(&proposal);
  if (g_last_cloud_state_upload == 0 ||
      lv_tick_elaps(g_last_cloud_state_upload) >= CLOUD_STATE_UPLOAD_MS)
    {
      time_t local_epoch = now + 8 * 60 * 60;

      gmtime_r(&local_epoch, &local);
      g_cloud_state_revision++;
      if (g_cloud_state_revision == 0)
        {
          g_cloud_state_revision = 1;
        }
      ret = home_panel_mijia_request_board_state(
        g_ctrl_family_model, g_cloud_state_revision,
        proposal.valid ? proposal.revision : 0,
        (uint64_t)now, local.tm_hour * 60u + local.tm_min);
      if (ret == 0)
        {
          g_last_cloud_state_upload = now_ms;
        }
      return;
    }
  if (g_last_cloud_proposal_poll == 0 ||
      lv_tick_elaps(g_last_cloud_proposal_poll) >= CLOUD_PROPOSAL_POLL_MS)
    {
      ret = home_panel_mijia_request_board_proposal(
        proposal.valid ? proposal.revision : 0);
      if (ret == 0)
        {
          g_last_cloud_proposal_poll = now_ms;
        }
    }
}

/****************************************************************************
 * Public API: cloud tick (moved from main.c process_cloud_automations)
 ****************************************************************************/

void proactive_ctrl_process_tick(void)
{
  process_cloud_automations();
}

/****************************************************************************
 * Public API: automation execution (moved from main.c
 *             process_proactive_automation)
 ****************************************************************************/

void proactive_ctrl_process_automation(void)
{
  struct home_proactive_snapshot_s snapshot;
  struct proactive_action_target_s target;
  uint32_t now_ms = lv_tick_get();
  int ret;

  if (g_proactive_command.active &&
      (int32_t)(now_ms - g_proactive_command.deadline_ms) > 0)
    {
      if (g_proactive_command.cloud_execution)
        {
          cloud_finish_execution(false);
        }
      else if (g_proactive_command.automation)
        {
          home_proactive_automation_result(false);
          schedule_proactive_persist();
          syslog(LOG_WARNING,
                 "[HOME][AGENT] automation confirmation timeout key=%u\n",
                 (unsigned int)g_proactive_command.decision_key);
        }
      memset(&g_proactive_command, 0, sizeof(g_proactive_command));
    }

  home_proactive_get_snapshot(&snapshot);
  if (!snapshot.automation_due ||
      snapshot.candidate_kind == HOME_PROACTIVE_CANDIDATE_SLEEP ||
      !proactive_resolve_action(&snapshot, &target))
    {
      return;
    }

  if (!home_proactive_claim_automation(snapshot.decision_key))
    {
      return;
    }

  ret = proactive_request_action(&snapshot, &target, true,
                                 HOME_PROACTIVE_ACCEPT, false);
  if (ret == 1)
    {
      home_proactive_automation_result(true);
      schedule_proactive_persist();
      syslog(LOG_INFO,
             "[HOME][AGENT] automation already satisfied key=%u\n",
             (unsigned int)snapshot.decision_key);
    }
  else if (ret < 0)
    {
      home_proactive_automation_result(false);
      schedule_proactive_persist();
      syslog(LOG_WARNING,
             "[HOME][AGENT] automation send failed key=%u ret=%d\n",
             (unsigned int)snapshot.decision_key, ret);
    }
  else
    {
      syslog(LOG_INFO,
             "[HOME][AGENT] automation sent key=%u action=%u/%u\n",
             (unsigned int)snapshot.decision_key,
             snapshot.action_siid, snapshot.action_piid);
    }
}

/****************************************************************************
 * Public API: model observation (moved from main.c
 *             proactive_observe_model_changes)
 ****************************************************************************/

void proactive_ctrl_observe_model_changes(
  const struct home_panel_family_model_s *previous,
  const struct home_panel_family_model_s *next)
{
  struct home_proactive_event_s event;
  uint32_t day_ordinal;
  uint16_t minute_of_day;
  uint32_t now_ms;
  unsigned int device_index;

  if (!proactive_event_time(&day_ordinal, &minute_of_day))
    {
      return;
    }

  now_ms = lv_tick_get();
  for (device_index = 0; device_index < next->device_count; device_index++)
    {
      const struct home_panel_device_s *device =
        &next->devices[device_index];
      const struct home_panel_device_s *old_device =
        proactive_find_device(previous, device->did);
      unsigned int observable_index;

      if (old_device == NULL)
        {
          continue;
        }

      if (old_device->online != device->online)
        {
          memset(&event, 0, sizeof(event));
          event.device_hash = proactive_hash_device_id(device->did);
          event.day_ordinal = day_ordinal;
          event.now_ms = now_ms;
          event.minute_of_day = minute_of_day;
          event.value = device->online ? 1 : 0;
          event.kind = HOME_PROACTIVE_EVENT_ONLINE;
          proactive_observe_model_event(&event);
        }

      for (observable_index = 0;
           observable_index < device->observable_count;
           observable_index++)
        {
          const struct home_panel_observable_s *observable =
            &device->observables[observable_index];
          const struct home_panel_observable_s *old_observable =
            proactive_find_observable(old_device, observable->siid,
                                      observable->piid);
          const struct home_panel_control_s *control;
          bool changed;

          if (old_observable == NULL || !observable->has_value ||
              !old_observable->has_value ||
              old_observable->type != observable->type)
            {
              continue;
            }

          changed = observable->type == HOME_PANEL_CONTROL_BOOLEAN ?
                    old_observable->boolean_value !=
                      observable->boolean_value :
                    old_observable->value != observable->value;
          if (!changed)
            {
              continue;
            }

          control = proactive_find_control(device, observable->siid,
                                           observable->piid);
          memset(&event, 0, sizeof(event));
          event.device_hash = proactive_hash_device_id(device->did);
          event.day_ordinal = day_ordinal;
          event.now_ms = now_ms;
          event.minute_of_day = minute_of_day;
          event.siid = observable->siid;
          event.piid = observable->piid;
          event.value =
            observable->type == HOME_PANEL_CONTROL_BOOLEAN ?
              (observable->boolean_value ? 1 : 0) : observable->value;
          event.kind =
            observable->type == HOME_PANEL_CONTROL_BOOLEAN ?
              HOME_PROACTIVE_EVENT_BOOLEAN :
              HOME_PROACTIVE_EVENT_NUMBER;
          event.writable =
            control != NULL && proactive_safe_action_device(device);
          proactive_observe_model_event(&event);
        }
    }
}

/****************************************************************************
 * Public API: feedback handling (moved from main.c
 *             proactive_handle_feedback)
 ****************************************************************************/

void proactive_ctrl_handle_feedback(enum home_proactive_feedback_e feedback,
                                    bool confirmed,
                                    struct proactive_viewmodel_s *vm)
{
  struct home_panel_agent_snapshot_s agent;
  struct home_panel_cloud_proposal_s cloud;
  struct home_proactive_snapshot_s before;
  struct proactive_action_target_s target;
  bool generic;
  int ret = 0;

  home_panel_mijia_get_board_proposal(&cloud);
  if (cloud.valid && cloud.expires_at > (uint64_t)time(NULL) &&
      cloud.status == HOME_PANEL_CLOUD_PROPOSAL_AWAITING_CONFIRMATION)
    {
      if (feedback == HOME_PROACTIVE_IGNORE_TODAY ||
          feedback == HOME_PROACTIVE_LESS_OFTEN)
        {
          cloud_queue_feedback(cloud.proposal_id, "dismissed");
          snprintf(g_last_feedback_message,
                   sizeof(g_last_feedback_message),
                   "已忽略云端建议，结果将用于后续学习");
        }
      else if (feedback == HOME_PROACTIVE_ACCEPT)
        {
          if (cloud.kind == HOME_PANEL_CLOUD_AUTOMATION)
            {
              ret = home_panel_mijia_confirm_board_proposal(
                cloud.proposal_id);
              snprintf(g_last_feedback_message,
                       sizeof(g_last_feedback_message),
                       ret == 0 ? "已确认，服务端正在生成受限离线计划" :
                                  "确认发送失败，请稍后重试");
            }
          else
            {
              struct cloud_automation_record_s record;

              if (!cloud_build_record(&cloud, &record, false))
                {
                  snprintf(g_last_feedback_message,
                           sizeof(g_last_feedback_message),
                           "建议包含不可执行或不安全能力，已拒绝");
                  cloud_queue_feedback(cloud.proposal_id,
                                       "execution_failed");
                }
              else
                {
                  if (g_cloud_execution.active ||
                      g_proactive_command.active)
                    {
                      snprintf(g_last_feedback_message,
                               sizeof(g_last_feedback_message),
                               "设备操作正忙，请稍后再次确认");
                    }
                  else
                    {
                      cloud_queue_feedback(cloud.proposal_id, "accepted");
                      if (!cloud_start_execution(&record, true,
                                                 cloud.proposal_id))
                        {
                          snprintf(g_last_feedback_message,
                                   sizeof(g_last_feedback_message),
                                   "建议已接受，但设备操作启动失败");
                        }
                      else
                        {
                          snprintf(g_last_feedback_message,
                                   sizeof(g_last_feedback_message),
                                   "已按顺序执行建议，等待每项设备状态确认");
                        }
                    }
                }
            }
        }
      if (vm != NULL)
        {
          snprintf(vm->feedback_message, sizeof(vm->feedback_message),
                   "%s", g_last_feedback_message);
        }
      return;
    }

  home_proactive_get_snapshot(&before);
  home_panel_mijia_get_agent_snapshot(&agent);
  generic = before.candidate_kind != HOME_PROACTIVE_CANDIDATE_SLEEP;
  if (feedback == HOME_PROACTIVE_ENABLE_AUTOMATION && !generic)
    {
      return;
    }
  if (feedback == HOME_PROACTIVE_ENABLE_AUTOMATION && !confirmed)
    {
      /* Signal that UI should show confirmation dialog */

      if (vm != NULL)
        {
          snprintf(vm->feedback_message, sizeof(vm->feedback_message),
                   "CONFIRM_NEEDED");
        }
      snprintf(g_last_feedback_message, sizeof(g_last_feedback_message),
               "CONFIRM_NEEDED");
      return;
    }
  if (feedback == HOME_PROACTIVE_ACCEPT ||
      feedback == HOME_PROACTIVE_ENABLE_AUTOMATION)
    {
      if (proactive_agent_matches(&agent, &before) &&
          (agent.state == HOME_PANEL_AGENT_PENDING ||
           (agent.state == HOME_PANEL_AGENT_CLOUD_READY &&
            agent.decision != HOME_PANEL_AGENT_PROPOSE)))
        {
          snprintf(g_last_feedback_message,
                   sizeof(g_last_feedback_message),
                   agent.state == HOME_PANEL_AGENT_PENDING ?
                     "云端仍在分析，请稍候；失败后会自动切换本地算法" :
                     "云端建议本次暂缓，未执行设备操作");
          if (vm != NULL)
            {
              snprintf(vm->feedback_message, sizeof(vm->feedback_message),
                       "%s", g_last_feedback_message);
            }
          return;
        }
      if (generic)
        {
          if (!proactive_resolve_action(&before, &target))
            {
              snprintf(g_last_feedback_message,
                       sizeof(g_last_feedback_message),
                       "目标设备离线或属性已不可写，本次未执行");
              if (vm != NULL)
                {
                  snprintf(vm->feedback_message,
                           sizeof(vm->feedback_message),
                           "%s", g_last_feedback_message);
                }
              return;
            }
          ret = proactive_request_action(&before, &target, false,
                                         feedback, true);
        }
      else if (!before.target_available)
        {
          return;
        }
      else
        {
          ret = home_panel_mijia_request_bool_property(
            before.target_did, before.target_name, "on",
            before.target_siid, before.target_piid, false);
          if (ret == 0)
            {
              memset(&g_proactive_command, 0,
                     sizeof(g_proactive_command));
              g_proactive_command.device_hash =
                proactive_hash_device_id(before.target_did);
              g_proactive_command.decision_key = before.decision_key;
              g_proactive_command.deadline_ms = lv_tick_get() + 15000u;
              g_proactive_command.siid = before.target_siid;
              g_proactive_command.piid = before.target_piid;
              g_proactive_command.value = 0;
              g_proactive_command.kind = HOME_PROACTIVE_EVENT_BOOLEAN;
              g_proactive_command.active = true;
              g_proactive_command.feedback_pending = true;
              g_proactive_command.feedback_on_success = feedback;
            }
        }
      if (ret < 0)
        {
          snprintf(g_last_feedback_message,
                   sizeof(g_last_feedback_message),
                   ret == -EBUSY ?
                   "请等待上一条设备指令完成" :
                   "设备指令发送失败");
          if (vm != NULL)
            {
              snprintf(vm->feedback_message, sizeof(vm->feedback_message),
                       "%s", g_last_feedback_message);
            }
          return;
        }
    }

  /* A device-changing acceptance is committed only after a matching MIoT
   * state event confirms the requested value.  This prevents a transport
   * failure from teaching the local profile that an action succeeded or
   * installing an offline automation that was never verified.
   */

  if (feedback != HOME_PROACTIVE_ACCEPT &&
      feedback != HOME_PROACTIVE_ENABLE_AUTOMATION)
    {
      home_proactive_feedback(feedback);
    }
  else if (ret == 1)
    {
      home_proactive_feedback(feedback);
    }
  if (before.mode == HOME_PROACTIVE_REAL &&
      ((feedback != HOME_PROACTIVE_ACCEPT &&
        feedback != HOME_PROACTIVE_ENABLE_AUTOMATION) || ret == 1))
    {
      schedule_proactive_persist();
    }
  snprintf(g_last_feedback_message, sizeof(g_last_feedback_message),
           feedback == HOME_PROACTIVE_ENABLE_AUTOMATION ?
             (ret == 1 ? "设备已处于建议状态，已建立本地断网自动化" :
                         "指令已发送，状态确认后才会建立离线自动化") :
           feedback == HOME_PROACTIVE_ACCEPT ?
             (ret == 1 ? "设备已处于建议状态，已记录本次选择" :
                         "指令已发送，等待设备状态确认") :
           feedback == HOME_PROACTIVE_IGNORE_TODAY ?
             "今天已忽略，已记录反馈" :
             "已降低提醒频率");
  if (vm != NULL)
    {
      snprintf(vm->feedback_message, sizeof(vm->feedback_message),
               "%s", g_last_feedback_message);
    }
}

/****************************************************************************
 * Public API: delete automation (business logic extracted from
 *             proactive_automation_delete_clicked)
 ****************************************************************************/

int proactive_ctrl_delete_automation(uint32_t routine_id, bool cloud)
{
  int ret;

  if (routine_id == 0)
    {
      return -EINVAL;
    }

  if (cloud)
    {
      unsigned int index;

      ret = -ENOENT;
      for (index = 0; index < g_cloud_automation_count; index++)
        {
          if (g_cloud_automations[index].routine_id == routine_id)
            {
              g_cloud_automation_count--;
              if (index < g_cloud_automation_count)
                {
                  memmove(&g_cloud_automations[index],
                          &g_cloud_automations[index + 1],
                          sizeof(g_cloud_automations[0]) *
                            (g_cloud_automation_count - index));
                  memmove(&g_cloud_due_at[index],
                          &g_cloud_due_at[index + 1],
                          sizeof(g_cloud_due_at[0]) *
                            (g_cloud_automation_count - index));
                }
              memset(&g_cloud_automations[g_cloud_automation_count], 0,
                     sizeof(g_cloud_automations[0]));
              g_cloud_due_at[g_cloud_automation_count] = 0;
              ret = 0;
              break;
            }
        }
    }
  else
    {
      ret = home_proactive_remove_automation(routine_id);
    }

  if (ret == 0)
    {
      schedule_proactive_persist();
      g_ctrl_automation_signature = 0;
      snprintf(g_last_feedback_message, sizeof(g_last_feedback_message),
               "离线自动化已删除并停止执行");
    }
  else
    {
      snprintf(g_last_feedback_message, sizeof(g_last_feedback_message),
               "自动化已不存在，无需重复删除");
    }
  return ret;
}

/****************************************************************************
 * Public API: view-model builder
 ****************************************************************************/

void proactive_ctrl_get_viewmodel(struct proactive_viewmodel_s *vm)
{
  struct home_proactive_automation_s
    local_items[HOME_PROACTIVE_MAX_AUTOMATIONS];
  unsigned int local_count;
  unsigned int index;
  uint32_t signature;
  unsigned int vm_index;

  if (vm == NULL)
    {
      return;
    }

  memset(vm, 0, sizeof(*vm));

  /* 1. Get snapshots */

  home_proactive_get_snapshot(&vm->proactive);
  home_panel_mijia_get_agent_snapshot(&vm->agent);
  home_panel_mijia_get_board_proposal(&vm->cloud);

  /* 2. Compute presentation state */

  vm->agent_matches = proactive_agent_matches(&vm->agent, &vm->proactive);
  vm->cloud_blocks = vm->cloud.valid &&
                     vm->cloud.expires_at > (uint64_t)time(NULL) &&
                     vm->cloud.status ==
                       HOME_PANEL_CLOUD_PROPOSAL_AWAITING_CONFIRMATION;
  vm->generic =
    vm->proactive.candidate_kind != HOME_PROACTIVE_CANDIDATE_SLEEP;
  vm->target_available = vm->proactive.target_available;
  vm->actionable = vm->proactive.suggestion_available &&
                   !vm->proactive.automation_enabled;

  /* 3. Resolve action target if generic && target_available */

  if (vm->generic && vm->target_available)
    {
      struct proactive_action_target_s target;

      if (proactive_resolve_action(&vm->proactive, &target))
        {
          if (target.device != NULL)
            {
              snprintf(vm->action_device_name,
                       sizeof(vm->action_device_name), "%s",
                       target.device->name);
            }
          if (target.control != NULL)
            {
              snprintf(vm->action_control_name,
                       sizeof(vm->action_control_name), "%s",
                       target.control->name);
            }
          vm->action_current_value = target.current_value;
          vm->action_already_satisfied = target.already_satisfied;
        }
    }

  /* 4. Build automation list (local + cloud) */

  memset(local_items, 0, sizeof(local_items));
  local_count = home_proactive_list_automations(
    local_items, HOME_PROACTIVE_MAX_AUTOMATIONS);
  if (local_count > HOME_PROACTIVE_MAX_AUTOMATIONS)
    {
      local_count = HOME_PROACTIVE_MAX_AUTOMATIONS;
    }

  signature = compute_automation_signature(local_items, local_count);
  for (index = 0; index < g_cloud_automation_count; index++)
    {
      const struct cloud_automation_record_s *record =
        &g_cloud_automations[index];
      unsigned int action;

      signature = (signature ^ record->routine_id) * 16777619u;
      signature = (signature ^ record->trigger_device_hash) * 16777619u;
      signature = (signature ^ record->trigger_siid) * 16777619u;
      signature = (signature ^ record->trigger_piid) * 16777619u;
      signature = (signature ^ record->minute_of_day) * 16777619u;
      signature = (signature ^ record->delay_seconds) * 16777619u;
      signature = (signature ^ record->days_mask) * 16777619u;
      for (action = 0; action < record->action_count; action++)
        {
          signature = (signature ^ record->actions[action].device_hash) *
                      16777619u;
          signature = (signature ^ record->actions[action].siid) *
                      16777619u;
          signature = (signature ^ record->actions[action].piid) *
                      16777619u;
          signature =
            (signature ^ (uint32_t)record->actions[action].value) *
            16777619u;
        }
    }

  vm->automation_signature = signature;
  vm_index = 0;

  /* Add local automations */

  for (index = 0; index < local_count &&
       vm_index < HOME_PROACTIVE_MAX_AUTOMATIONS + PROACTIVE_CTRL_CLOUD_MAX;
       index++)
    {
      struct proactive_ctrl_automation_entry_s *entry =
        &vm->automations[vm_index];

      entry->routine_id = local_items[index].routine_id;
      entry->cloud = false;
      entry->event_triggered = local_items[index].event_triggered;
      entry->mean_minute_of_day = local_items[index].mean_minute_of_day;
      entry->mean_delay_seconds = local_items[index].mean_delay_seconds;
      entry->action_value = local_items[index].action_value;
      entry->action_kind = local_items[index].action_kind;
      entry->action_device_hash = local_items[index].action_device_hash;
      entry->trigger_device_hash = local_items[index].trigger_device_hash;
      entry->action_count = 1;
      vm_index++;
    }

  /* Add cloud automations */

  for (index = 0; index < g_cloud_automation_count &&
       vm_index < HOME_PROACTIVE_MAX_AUTOMATIONS + PROACTIVE_CTRL_CLOUD_MAX;
       index++)
    {
      const struct cloud_automation_record_s *record =
        &g_cloud_automations[index];
      struct proactive_ctrl_automation_entry_s *entry =
        &vm->automations[vm_index];

      entry->routine_id = record->routine_id;
      entry->cloud = true;
      entry->event_triggered =
        record->trigger_kind == HOME_PANEL_CLOUD_TRIGGER_PROPERTY;
      entry->mean_minute_of_day = record->minute_of_day;
      entry->mean_delay_seconds = record->delay_seconds;
      entry->action_value =
        record->action_count > 0 ? record->actions[0].value : 0;
      entry->action_kind =
        (record->action_count > 0 && record->actions[0].value_is_boolean) ?
          HOME_PROACTIVE_EVENT_BOOLEAN : HOME_PROACTIVE_EVENT_NUMBER;
      entry->action_device_hash =
        record->action_count > 0 ? record->actions[0].device_hash : 0;
      entry->trigger_device_hash = record->trigger_device_hash;
      entry->action_count = record->action_count;
      vm_index++;
    }

  vm->automation_count = vm_index;

  /* 5. Copy feedback message */

  snprintf(vm->feedback_message, sizeof(vm->feedback_message), "%s",
           g_last_feedback_message);
}

/****************************************************************************
 * Replay / reset wrappers
 ****************************************************************************/

void proactive_ctrl_start_replay(uint32_t now_ms)
{
  home_proactive_start_replay(now_ms);
}

void proactive_ctrl_stop_replay(void)
{
  home_proactive_stop_replay();
}

void proactive_ctrl_reset(void)
{
  home_proactive_reset();
}
