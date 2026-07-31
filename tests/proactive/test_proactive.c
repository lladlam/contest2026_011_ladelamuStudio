#include "home_panel_proactive.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct test_routine_v4_s
{
  uint32_t routine_id;
  uint32_t trigger_device_hash;
  uint32_t action_device_hash;
  uint32_t last_observation_day;
  uint32_t last_executed_day;
  int32_t trigger_value;
  int32_t action_value;
  uint16_t trigger_siid;
  uint16_t trigger_piid;
  uint16_t action_siid;
  uint16_t action_piid;
  uint16_t mean_shifted_minute;
  uint16_t mean_deviation_minutes;
  uint16_t mean_delay_seconds;
  uint16_t observation_count;
  uint16_t accepted_count;
  uint16_t rejected_count;
  uint8_t trigger_kind;
  uint8_t action_kind;
  uint8_t flags;
  uint8_t reserved;
};

struct test_profile_v4_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t history_days;
  uint32_t feedback_count;
  uint32_t accepted_count;
  uint32_t ignored_count;
  uint32_t sleep_sample_count;
  uint32_t sleep_shifted_minute_total;
  uint32_t reminder_cooldown_days;
  uint32_t last_observation_day;
  uint32_t cloud_budget_day;
  uint32_t last_learning_day;
  uint32_t next_routine_id;
  uint16_t cloud_budget_count;
  uint16_t routine_count;
  struct test_routine_v4_s routines[8];
  uint32_t checksum;
};

static uint32_t test_crc32(const void *data, size_t length)
{
  const uint8_t *bytes = data;
  uint32_t crc = UINT32_MAX;
  size_t index;
  unsigned int bit;

  for (index = 0; index < length; index++)
    {
      crc ^= bytes[index];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc >> 1) ^
                ((crc & 1) != 0 ? 0xedb88320u : 0u);
        }
    }
  return ~crc;
}

static struct home_proactive_context_s test_context(void)
{
  struct home_proactive_context_s context;

  memset(&context, 0, sizeof(context));
  context.day_ordinal = 100;
  context.minute_of_day = 23 * 60 + 8;
  context.clock_valid = true;
  context.network_online = true;
  context.lights_on = 2;
  context.air_conditioner_on = true;
  context.target_available = true;
  strcpy(context.target_did, "test-light");
  strcpy(context.target_name, "客厅灯");
  context.target_siid = 2;
  context.target_piid = 1;
  return context;
}

static struct home_proactive_event_s test_boolean_event(
  uint32_t day, uint16_t minute, uint32_t now_ms,
  uint32_t device_hash, bool value, bool writable)
{
  struct home_proactive_event_s event;

  memset(&event, 0, sizeof(event));
  event.device_hash = device_hash;
  event.day_ordinal = day;
  event.now_ms = now_ms;
  event.minute_of_day = minute;
  event.siid = 2;
  event.piid = 1;
  event.value = value ? 1 : 0;
  event.kind = HOME_PROACTIVE_EVENT_BOOLEAN;
  event.writable = writable;
  return event;
}

static struct home_proactive_event_s test_number_event(
  uint32_t day, uint16_t minute, uint32_t now_ms,
  uint32_t device_hash, uint16_t siid, uint16_t piid,
  int value, bool writable)
{
  struct home_proactive_event_s event;

  memset(&event, 0, sizeof(event));
  event.device_hash = device_hash;
  event.day_ordinal = day;
  event.now_ms = now_ms;
  event.minute_of_day = minute;
  event.siid = siid;
  event.piid = piid;
  event.value = value;
  event.kind = HOME_PROACTIVE_EVENT_NUMBER;
  event.writable = writable;
  return event;
}

static struct home_proactive_event_s test_online_event(
  uint32_t day, uint16_t minute, uint32_t now_ms,
  uint32_t device_hash, bool online)
{
  struct home_proactive_event_s event;

  memset(&event, 0, sizeof(event));
  event.device_hash = device_hash;
  event.day_ordinal = day;
  event.now_ms = now_ms;
  event.minute_of_day = minute;
  event.value = online ? 1 : 0;
  event.kind = HOME_PROACTIVE_EVENT_ONLINE;
  return event;
}

static void test_cold_start(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;

  home_proactive_initialize();
  home_proactive_set_context(&context);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.history_days == 0);
  assert(snapshot.confidence == 15);
  assert(snapshot.source == HOME_PROACTIVE_SOURCE_RULE);
  assert(snapshot.suggestion_available);
  assert(strcmp(snapshot.target_name, "客厅灯") == 0);
}

static void test_replay_and_feedback(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;
  uint8_t profile[768];
  size_t profile_size = 0;

  home_proactive_initialize();
  home_proactive_set_context(&context);
  home_proactive_start_replay(1000);
  home_proactive_tick(1000 + 7 * 3500);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.mode == HOME_PROACTIVE_DEMO_READY);
  assert(snapshot.history_days == 7);
  assert(snapshot.accepted_count == 6);
  assert(snapshot.ignored_count == 1);
  assert(snapshot.confidence >= 80);
  assert(snapshot.source == HOME_PROACTIVE_SOURCE_DEMO);
  assert(snapshot.preferred_hour == 23);
  assert(snapshot.suggestion_available);

  home_proactive_feedback(HOME_PROACTIVE_LESS_OFTEN);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.mode == HOME_PROACTIVE_DEMO_READY);
  assert(snapshot.history_days == 8);
  assert(snapshot.ignored_count == 2);
  assert(snapshot.reminder_cooldown_days == 2);

  home_proactive_stop_replay();
  home_proactive_set_context(&context);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.mode == HOME_PROACTIVE_REAL);
  assert(snapshot.history_days == 0);

  assert(home_proactive_export_profile(profile, sizeof(profile),
                                       &profile_size) == 0);
  home_proactive_reset();
  assert(home_proactive_import_profile(profile, profile_size) == 0);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.history_days == 0);
  assert(snapshot.reminder_cooldown_days == 1);
}

static void test_real_feedback_persists(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;
  uint8_t profile[768];
  size_t profile_size = 0;

  home_proactive_initialize();
  home_proactive_set_context(&context);
  home_proactive_feedback(HOME_PROACTIVE_ACCEPT);
  assert(home_proactive_export_profile(profile, sizeof(profile),
                                       &profile_size) == 0);
  home_proactive_reset();
  assert(home_proactive_import_profile(profile, profile_size) == 0);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.history_days == 1);
  assert(snapshot.accepted_count == 1);
}

static void test_no_light_no_suggestion(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;

  context.lights_on = 0;
  context.target_available = false;
  home_proactive_initialize();
  home_proactive_set_context(&context);
  home_proactive_get_snapshot(&snapshot);
  assert(!snapshot.suggestion_available);
}

static void test_invalid_clock_has_no_real_suggestion(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;

  context.clock_valid = false;
  home_proactive_initialize();
  home_proactive_set_context(&context);
  home_proactive_get_snapshot(&snapshot);
  assert(!snapshot.suggestion_available);
  assert(!snapshot.clock_valid);
}

static void test_offline_keeps_local_suggestion(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;

  context.network_online = false;
  home_proactive_initialize();
  home_proactive_set_context(&context);
  home_proactive_get_snapshot(&snapshot);
  assert(!snapshot.network_online);
  assert(snapshot.suggestion_available);
  assert(snapshot.source == HOME_PROACTIVE_SOURCE_RULE);
}

static void test_midnight_learning_uses_circular_time(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;

  home_proactive_initialize();
  context.minute_of_day = 23 * 60 + 55;
  home_proactive_set_context(&context);
  home_proactive_feedback(HOME_PROACTIVE_ACCEPT);

  context.day_ordinal++;
  context.minute_of_day = 5;
  home_proactive_set_context(&context);
  home_proactive_feedback(HOME_PROACTIVE_ACCEPT);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.preferred_hour == 0);
  assert(snapshot.preferred_minute == 0);
  assert(snapshot.profile_learned);
  assert(snapshot.source == HOME_PROACTIVE_SOURCE_LEARNED);
}

static void test_rejection_does_not_train_sleep_time(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;

  home_proactive_initialize();
  context.minute_of_day = 23 * 60 + 10;
  home_proactive_set_context(&context);
  home_proactive_feedback(HOME_PROACTIVE_ACCEPT);

  context.day_ordinal++;
  context.minute_of_day = 23 * 60 + 40;
  home_proactive_set_context(&context);
  home_proactive_feedback(HOME_PROACTIVE_IGNORE_TODAY);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.preferred_hour == 23);
  assert(snapshot.preferred_minute == 10);
  assert(snapshot.accepted_count == 1);
  assert(snapshot.ignored_count == 1);
}

static void test_invalid_feedback_is_ignored(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;

  home_proactive_initialize();
  home_proactive_set_context(&context);
  home_proactive_feedback((enum home_proactive_feedback_e)99);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.feedback_count == 0);
  assert(snapshot.suggestion_available);
}

static void test_daily_dismissal_and_cooldown(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;

  home_proactive_initialize();
  home_proactive_set_context(&context);
  home_proactive_feedback(HOME_PROACTIVE_LESS_OFTEN);
  context.minute_of_day++;
  home_proactive_set_context(&context);
  home_proactive_get_snapshot(&snapshot);
  assert(!snapshot.suggestion_available);

  context.day_ordinal++;
  home_proactive_set_context(&context);
  home_proactive_get_snapshot(&snapshot);
  assert(!snapshot.suggestion_available);

  context.day_ordinal++;
  home_proactive_set_context(&context);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.suggestion_available);
}

static void test_corrupt_profile_is_rejected(void)
{
  uint8_t profile[768];
  uint8_t corrupted[768];
  size_t profile_size = 0;
  size_t index;

  home_proactive_initialize();
  assert(home_proactive_export_profile(profile, sizeof(profile),
                                       &profile_size) == 0);
  for (index = 0; index < profile_size; index++)
    {
      memcpy(corrupted, profile, profile_size);
      corrupted[index] ^= 0x01;
      assert(home_proactive_import_profile(corrupted, profile_size) < 0);
    }
  assert(home_proactive_import_profile(profile + 1, profile_size - 1) < 0);
}

static void test_repeated_rejection_suppresses_low_value_suggestion(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;
  unsigned int day;

  home_proactive_initialize();
  for (day = 0; day < 4; day++)
    {
      context.day_ordinal = 200 + day;
      home_proactive_set_context(&context);
      home_proactive_feedback(HOME_PROACTIVE_IGNORE_TODAY);
    }
  context.day_ordinal++;
  home_proactive_set_context(&context);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.feedback_count == 4);
  assert(snapshot.accepted_count == 0);
  assert(snapshot.confidence < 45);
  assert(!snapshot.suggestion_available);
}

static void test_long_running_profile_round_trips(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s before;
  struct home_proactive_snapshot_s after;
  uint8_t profile[768];
  size_t profile_size = 0;
  unsigned int day;

  home_proactive_initialize();
  for (day = 0; day < 20000; day++)
    {
      context.day_ordinal = 1000 + day;
      context.minute_of_day = 23 * 60 + (day % 21);
      home_proactive_set_context(&context);
      home_proactive_feedback(day % 5 == 0 ?
                              HOME_PROACTIVE_IGNORE_TODAY :
                              HOME_PROACTIVE_ACCEPT);
    }

  home_proactive_get_snapshot(&before);
  assert(before.feedback_count == 20000);
  assert(before.accepted_count + before.ignored_count ==
         before.feedback_count);
  assert(home_proactive_export_profile(profile, sizeof(profile),
                                       &profile_size) == 0);
  home_proactive_initialize();
  assert(home_proactive_import_profile(profile, profile_size) == 0);
  home_proactive_get_snapshot(&after);
  assert(after.feedback_count == before.feedback_count);
  assert(after.accepted_count == before.accepted_count);
  assert(after.ignored_count == before.ignored_count);
  assert(after.preferred_hour == before.preferred_hour);
  assert(after.preferred_minute == before.preferred_minute);
}

static void test_time_routine_learns_incrementally(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;
  struct home_proactive_event_s event;
  unsigned int day;

  context.lights_on = 0;
  context.target_available = false;
  context.minute_of_day = 18 * 60;
  home_proactive_initialize();
  home_proactive_set_context(&context);

  for (day = 1; day <= 5; day++)
    {
      event = test_boolean_event(day, 18 * 60,
                                 day * 1000000u, 0x200u,
                                 true, true);
      home_proactive_observe_event(&event);
    }

  context.day_ordinal = 6;
  context.minute_of_day = 18 * 60;
  home_proactive_set_context(&context);
  home_proactive_tick(6000000u);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.routine_count >= 1);
  assert(snapshot.routine_observations >= 5);
  assert(snapshot.suggestion_available);
  assert(snapshot.candidate_kind == HOME_PROACTIVE_CANDIDATE_TIME_ROUTINE);
  assert(snapshot.action_device_hash == 0x200u);
  assert(snapshot.action_value == 1);
  assert(snapshot.confidence >= 65);
  assert(snapshot.routine_id != 0);
  assert(snapshot.learning_revision != 0);
  assert(snapshot.learning_routine_id == snapshot.routine_id);
  assert(snapshot.learning_observations == 5);
  assert(snapshot.learning_kind ==
         HOME_PROACTIVE_CANDIDATE_TIME_ROUTINE);

  home_proactive_feedback(HOME_PROACTIVE_ACCEPT);
  home_proactive_get_snapshot(&snapshot);
  assert(!snapshot.suggestion_available);
  assert(snapshot.accepted_count == 1);
  assert(snapshot.learning_accepted == 1);
  assert(snapshot.learning_update_kind == HOME_PROACTIVE_UPDATE_ACCEPTED);
}

static void test_event_routine_can_become_offline_automation(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;
  struct home_proactive_event_s trigger;
  struct home_proactive_event_s action;
  uint8_t profile[768];
  size_t profile_size = 0;
  unsigned int day;
  uint32_t event_routine_id;

  context.lights_on = 0;
  context.target_available = false;
  context.network_online = false;
  context.minute_of_day = 18 * 60;
  home_proactive_initialize();
  home_proactive_set_context(&context);

  for (day = 1; day <= 3; day++)
    {
      trigger = test_boolean_event(day, 18 * 60,
                                   day * 1000000u,
                                   0x100u, true, false);
      action = test_boolean_event(day, 18 * 60 + 1,
                                  day * 1000000u + 60000u,
                                  0x200u, true, true);
      home_proactive_observe_event(&trigger);
      home_proactive_observe_event(&action);
    }

  trigger = test_boolean_event(4, 18 * 60, 4000000u,
                               0x100u, true, false);
  home_proactive_observe_event(&trigger);
  home_proactive_tick(4060000u);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.suggestion_available);
  assert(snapshot.candidate_kind ==
         HOME_PROACTIVE_CANDIDATE_EVENT_ROUTINE);
  assert(snapshot.trigger_device_hash == 0x100u);
  assert(snapshot.action_device_hash == 0x200u);
  assert(snapshot.predicted_delay_seconds == 60);
  event_routine_id = snapshot.routine_id;

  home_proactive_feedback(HOME_PROACTIVE_ENABLE_AUTOMATION);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.automation_count == 1);
  while (snapshot.learning_routine_id != event_routine_id)
    {
      assert(snapshot.learning_revision != 0);
      assert(home_proactive_ack_learning(snapshot.learning_revision,
                                         snapshot.learning_routine_id));
      home_proactive_get_snapshot(&snapshot);
    }
  assert(snapshot.learning_automation_enabled);
  assert(snapshot.learning_update_kind ==
         HOME_PROACTIVE_UPDATE_AUTOMATION_ENABLED);

  trigger = test_boolean_event(5, 18 * 60, 5000000u,
                               0x100u, true, false);
  home_proactive_observe_event(&trigger);
  home_proactive_tick(5060000u);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.suggestion_available);
  assert(snapshot.automation_enabled);
  assert(snapshot.automation_due);
  assert(home_proactive_claim_automation(snapshot.decision_key));
  assert(!home_proactive_claim_automation(snapshot.decision_key));
  home_proactive_automation_result(true);
  home_proactive_get_snapshot(&snapshot);
  assert(!snapshot.suggestion_available);
  assert(snapshot.automation_count == 1);

  assert(home_proactive_export_profile(profile, sizeof(profile),
                                       &profile_size) == 0);
  home_proactive_initialize();
  assert(home_proactive_import_profile(profile, profile_size) == 0);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.automation_count == 1);
}

static void test_numeric_behavior_triggers_same_day_learning(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;
  struct home_proactive_event_s trigger;
  struct home_proactive_event_s action;
  static const int temperatures[] = {23, 24, 26};
  unsigned int occurrence;

  context.lights_on = 0;
  context.target_available = false;
  context.day_ordinal = 1;
  context.minute_of_day = 18 * 60;
  home_proactive_initialize();
  home_proactive_set_context(&context);

  for (occurrence = 0;
       occurrence < sizeof(temperatures) / sizeof(temperatures[0]);
       occurrence++)
    {
      uint32_t base_ms = 1000000u + occurrence * 120000u;

      trigger = test_number_event(
        1, 18 * 60 + occurrence * 2, base_ms,
        0x501u, 3, 1, temperatures[occurrence], false);
      action = test_boolean_event(
        1, 18 * 60 + occurrence * 2, base_ms + 30000u,
        0x502u, true, true);
      home_proactive_observe_event(&trigger);
      home_proactive_observe_event(&action);
    }

  context.day_ordinal = 2;
  context.minute_of_day = 18 * 60;
  home_proactive_set_context(&context);
  trigger = test_number_event(
    2, 18 * 60, 2000000u, 0x501u, 3, 1, 24, false);
  home_proactive_observe_event(&trigger);
  home_proactive_tick(2030000u);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.suggestion_available);
  assert(snapshot.candidate_kind ==
         HOME_PROACTIVE_CANDIDATE_EVENT_ROUTINE);
  assert(snapshot.trigger_device_hash == 0x501u);
  assert(snapshot.action_device_hash == 0x502u);
  assert(snapshot.action_is_boolean);
  assert(snapshot.action_value == 1);
}

static void test_online_behavior_can_trigger_numeric_action(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;
  struct home_proactive_event_s trigger;
  struct home_proactive_event_s action;
  unsigned int occurrence;

  context.lights_on = 0;
  context.target_available = false;
  context.day_ordinal = 1;
  context.minute_of_day = 9 * 60;
  home_proactive_initialize();
  home_proactive_set_context(&context);

  for (occurrence = 0; occurrence < 3; occurrence++)
    {
      uint32_t base_ms = 3000000u + occurrence * 120000u;

      trigger = test_online_event(
        1, 9 * 60 + occurrence * 2, base_ms, 0x601u, true);
      action = test_number_event(
        1, 9 * 60 + occurrence * 2, base_ms + 20000u,
        0x602u, 2, 3, 75, true);
      home_proactive_observe_event(&trigger);
      home_proactive_observe_event(&action);
    }

  context.day_ordinal = 2;
  context.minute_of_day = 9 * 60;
  home_proactive_set_context(&context);
  trigger = test_online_event(2, 9 * 60, 4000000u, 0x601u, true);
  home_proactive_observe_event(&trigger);
  home_proactive_tick(4020000u);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.suggestion_available);
  assert(snapshot.candidate_kind ==
         HOME_PROACTIVE_CANDIDATE_EVENT_ROUTINE);
  assert(snapshot.trigger_device_hash == 0x601u);
  assert(snapshot.action_device_hash == 0x602u);
  assert(!snapshot.action_is_boolean);
  assert(snapshot.action_value == 75);
}

static void test_self_generated_behavior_does_not_trigger(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;
  struct home_proactive_event_s trigger;
  struct home_proactive_event_s action;
  unsigned int occurrence;

  context.lights_on = 0;
  context.target_available = false;
  home_proactive_initialize();
  home_proactive_set_context(&context);

  for (occurrence = 1; occurrence <= 3; occurrence++)
    {
      trigger = test_boolean_event(
        occurrence, 12 * 60, occurrence * 1000000u,
        0x701u, true, false);
      action = test_boolean_event(
        occurrence, 12 * 60, occurrence * 1000000u + 10000u,
        0x702u, true, true);
      home_proactive_observe_event(&trigger);
      home_proactive_observe_event(&action);
    }

  context.day_ordinal = 4;
  context.minute_of_day = 12 * 60;
  home_proactive_set_context(&context);
  trigger = test_boolean_event(
    4, 12 * 60, 4000000u, 0x701u, true, false);
  trigger.self_generated = true;
  home_proactive_observe_event(&trigger);
  home_proactive_tick(4010000u);
  home_proactive_get_snapshot(&snapshot);
  assert(!snapshot.suggestion_available);
}

static void test_v4_profile_migrates_without_losing_routine(void)
{
  struct test_profile_v4_s legacy;
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s snapshot;
  struct home_proactive_event_s trigger;

  memset(&legacy, 0, sizeof(legacy));
  legacy.magic = 0x48505234u;
  legacy.version = 4;
  legacy.size = sizeof(legacy);
  legacy.history_days = 3;
  legacy.reminder_cooldown_days = 1;
  legacy.last_learning_day = 3;
  legacy.next_routine_id = 2;
  legacy.routine_count = 1;
  legacy.routines[0].routine_id = 1;
  legacy.routines[0].trigger_device_hash = 0x801u;
  legacy.routines[0].action_device_hash = 0x802u;
  legacy.routines[0].last_observation_day = 3;
  legacy.routines[0].trigger_value = 1;
  legacy.routines[0].action_value = 1;
  legacy.routines[0].trigger_siid = 2;
  legacy.routines[0].trigger_piid = 1;
  legacy.routines[0].action_siid = 2;
  legacy.routines[0].action_piid = 1;
  legacy.routines[0].mean_shifted_minute = 18 * 60;
  legacy.routines[0].mean_deviation_minutes = 0;
  legacy.routines[0].mean_delay_seconds = 10;
  legacy.routines[0].observation_count = 3;
  legacy.routines[0].trigger_kind = HOME_PROACTIVE_EVENT_BOOLEAN;
  legacy.routines[0].action_kind = HOME_PROACTIVE_EVENT_BOOLEAN;
  legacy.routines[0].flags = 1;
  legacy.checksum = test_crc32(
    &legacy, offsetof(struct test_profile_v4_s, checksum));

  home_proactive_initialize();
  assert(home_proactive_import_profile(&legacy, sizeof(legacy)) == 0);
  context.lights_on = 0;
  context.target_available = false;
  context.day_ordinal = 4;
  context.minute_of_day = 18 * 60;
  home_proactive_set_context(&context);
  trigger = test_boolean_event(
    4, 18 * 60, 4000000u, 0x801u, true, false);
  home_proactive_observe_event(&trigger);
  home_proactive_tick(4010000u);
  home_proactive_get_snapshot(&snapshot);
  assert(snapshot.routine_count == 1);
  assert(snapshot.suggestion_available);
  assert(snapshot.trigger_device_hash == 0x801u);
  assert(snapshot.action_device_hash == 0x802u);
}

static void test_learning_queue_preserves_multiple_routines(void)
{
  struct home_proactive_context_s context = test_context();
  struct home_proactive_snapshot_s first;
  struct home_proactive_snapshot_s second;
  struct home_proactive_event_s event;
  unsigned int day;

  context.lights_on = 0;
  context.target_available = false;
  home_proactive_initialize();
  home_proactive_set_context(&context);
  for (day = 1; day <= 5; day++)
    {
      event = test_boolean_event(day, 8 * 60,
                                 day * 86400000u,
                                 0x301u, true, true);
      home_proactive_observe_event(&event);
      event = test_boolean_event(day, 18 * 60,
                                 day * 86400000u + 3600000u,
                                 0x302u, true, true);
      home_proactive_observe_event(&event);
    }

  home_proactive_get_snapshot(&first);
  assert(first.learning_revision != 0);
  assert(first.learning_routine_id != 0);
  assert(!home_proactive_ack_learning(first.learning_revision + 1,
                                      first.learning_routine_id));
  assert(home_proactive_ack_learning(first.learning_revision,
                                     first.learning_routine_id));
  home_proactive_get_snapshot(&second);
  assert(second.learning_revision != 0);
  assert(second.learning_routine_id != 0);
  assert(second.learning_routine_id != first.learning_routine_id);
}

int main(void)
{
  test_cold_start();
  test_replay_and_feedback();
  test_real_feedback_persists();
  test_no_light_no_suggestion();
  test_invalid_clock_has_no_real_suggestion();
  test_offline_keeps_local_suggestion();
  test_midnight_learning_uses_circular_time();
  test_rejection_does_not_train_sleep_time();
  test_invalid_feedback_is_ignored();
  test_daily_dismissal_and_cooldown();
  test_corrupt_profile_is_rejected();
  test_repeated_rejection_suppresses_low_value_suggestion();
  test_long_running_profile_round_trips();
  test_time_routine_learns_incrementally();
  test_event_routine_can_become_offline_automation();
  test_numeric_behavior_triggers_same_day_learning();
  test_online_behavior_can_trigger_numeric_action();
  test_self_generated_behavior_does_not_trigger();
  test_v4_profile_migrates_without_losing_routine();
  test_learning_queue_preserves_multiple_routines();
  puts("proactive tests: PASS");
  return 0;
}
