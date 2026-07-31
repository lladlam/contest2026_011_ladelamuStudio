/****************************************************************************
 * D13x home panel proactive intelligence engine
 ****************************************************************************/

#include "home_panel_proactive.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PROACTIVE_PROFILE_MAGIC_V1      0x48505231u /* HPR1 */
#define PROACTIVE_PROFILE_MAGIC_V2      0x48505232u /* HPR2 */
#define PROACTIVE_PROFILE_MAGIC_V4      0x48505234u /* HPR4 */
#define PROACTIVE_PROFILE_MAGIC         0x48505235u /* HPR5 */
#define PROACTIVE_PROFILE_VERSION       5u
#define PROACTIVE_REPLAY_STEP_MS        3500u
#define PROACTIVE_DEFAULT_SLEEP_MIN     (23u * 60u + 10u)
#define PROACTIVE_COLD_WINDOW_MINUTES   60u
#define PROACTIVE_MIN_WINDOW_MINUTES    30u
#define PROACTIVE_MAX_SAMPLES           10000u
#define PROACTIVE_MAX_FEEDBACK          1000000u
#define PROACTIVE_MAX_ROUTINES          12u
#define PROACTIVE_RECENT_EVENTS         16u
#define PROACTIVE_TRIGGER_WINDOW_MS     (10u * 60u * 1000u)
#define PROACTIVE_MIN_TIME_DAYS         5u
#define PROACTIVE_MIN_EVENT_OCCURRENCES 3u
#define PROACTIVE_ROUTINE_THRESHOLD     65u

#define ROUTINE_FLAG_ACTIVE             (1u << 0)
#define ROUTINE_FLAG_AUTOMATION         (1u << 1)

struct proactive_profile_v1_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t history_days;
  uint32_t feedback_count;
  uint32_t accepted_count;
  uint32_t ignored_count;
  uint32_t sleep_sample_count;
  uint32_t sleep_minute_total;
  uint32_t reminder_cooldown_days;
  uint32_t last_observation_day;
};

struct proactive_profile_v2_s
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
  uint32_t checksum;
};

struct proactive_routine_record_s
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

struct proactive_profile_v4_s
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
  struct proactive_routine_record_s routines[8];
  uint32_t checksum;
};

struct proactive_profile_record_s
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
  struct proactive_routine_record_s routines[PROACTIVE_MAX_ROUTINES];
  uint32_t checksum;
};

struct proactive_state_s
{
  struct proactive_profile_record_s profile;
  struct proactive_profile_record_s replay_backup;
  struct home_proactive_context_s context;
  enum home_proactive_mode_e mode;
  uint32_t revision;
  uint32_t replay_started_ms;
  unsigned int replay_day;
  bool replay_backup_valid;
  bool suggestion_dismissed;
  uint32_t dismissed_day;
  struct home_proactive_event_s recent_events[PROACTIVE_RECENT_EVENTS];
  unsigned int recent_event_count;
  int active_routine;
  int pending_routine;
  uint32_t pending_due_ms;
  uint32_t learning_revision;
  int last_updated_routine;
  enum home_proactive_update_kind_e last_update_kind;
  struct
  {
    uint32_t revision;
    int routine_index;
    enum home_proactive_update_kind_e kind;
  } pending_learning[PROACTIVE_MAX_ROUTINES];
  unsigned int pending_learning_count;
  bool automation_inflight;
};

struct proactive_replay_event_s
{
  uint16_t minute_of_day;
  enum home_proactive_feedback_e feedback;
};

static const struct proactive_replay_event_s g_replay_events[] =
{
  {23 * 60 + 7, HOME_PROACTIVE_ACCEPT},
  {23 * 60 + 12, HOME_PROACTIVE_ACCEPT},
  {23 * 60 + 5, HOME_PROACTIVE_IGNORE_TODAY},
  {23 * 60 + 9, HOME_PROACTIVE_ACCEPT},
  {23 * 60 + 11, HOME_PROACTIVE_ACCEPT},
  {23 * 60 + 6, HOME_PROACTIVE_ACCEPT},
  {23 * 60 + 8, HOME_PROACTIVE_ACCEPT}
};

static struct proactive_state_s g_proactive;

_Static_assert(sizeof(struct proactive_profile_record_s) <= 768,
               "proactive profile exceeds board persistence payload");

static uint32_t proactive_crc32(const void *data, size_t length)
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
                (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }

  return ~crc;
}

static void proactive_bump_revision(void)
{
  g_proactive.revision++;
  if (g_proactive.revision == 0)
    {
      g_proactive.revision = 1;
    }
}

static void proactive_mark_routine_updated(
  int index, enum home_proactive_update_kind_e kind)
{
  unsigned int pending;

  if (index < 0 ||
      (unsigned int)index >= g_proactive.profile.routine_count)
    {
      return;
    }

  g_proactive.last_updated_routine = index;
  g_proactive.last_update_kind = kind;
  g_proactive.learning_revision++;
  if (g_proactive.learning_revision == 0)
    {
      g_proactive.learning_revision = 1;
    }

  for (pending = 0;
       pending < g_proactive.pending_learning_count;
       pending++)
    {
      if (g_proactive.pending_learning[pending].routine_index == index)
        {
          g_proactive.pending_learning[pending].revision =
            g_proactive.learning_revision;
          g_proactive.pending_learning[pending].kind = kind;
          return;
        }
    }

  if (g_proactive.pending_learning_count >= PROACTIVE_MAX_ROUTINES)
    {
      memmove(&g_proactive.pending_learning[0],
              &g_proactive.pending_learning[1],
              sizeof(g_proactive.pending_learning[0]) *
                (PROACTIVE_MAX_ROUTINES - 1));
      g_proactive.pending_learning_count--;
    }
  pending = g_proactive.pending_learning_count++;
  g_proactive.pending_learning[pending].revision =
    g_proactive.learning_revision;
  g_proactive.pending_learning[pending].routine_index = index;
  g_proactive.pending_learning[pending].kind = kind;
}

static void proactive_hash_u32(uint32_t *hash, uint32_t value)
{
  unsigned int shift;

  for (shift = 0; shift < 32; shift += 8)
    {
      *hash = (*hash ^ ((value >> shift) & 0xffu)) * 16777619u;
    }
}

static uint32_t proactive_decision_key(void)
{
  const unsigned char *text =
    (const unsigned char *)g_proactive.context.target_did;
  uint32_t hash = 2166136261u;

  proactive_hash_u32(&hash, (uint32_t)g_proactive.mode);
  proactive_hash_u32(&hash, g_proactive.context.day_ordinal);
  proactive_hash_u32(&hash, g_proactive.profile.feedback_count);
  proactive_hash_u32(&hash, g_proactive.profile.accepted_count);
  proactive_hash_u32(&hash, g_proactive.profile.reminder_cooldown_days);
  proactive_hash_u32(&hash, g_proactive.context.lights_on);
  proactive_hash_u32(&hash, g_proactive.context.air_conditioner_on ? 1 : 0);
  proactive_hash_u32(&hash, g_proactive.context.target_available ? 1 : 0);
  if (g_proactive.active_routine >= 0 &&
      (unsigned int)g_proactive.active_routine <
        g_proactive.profile.routine_count)
    {
      const struct proactive_routine_record_s *routine =
        &g_proactive.profile.routines[g_proactive.active_routine];

      proactive_hash_u32(&hash, routine->routine_id);
      proactive_hash_u32(&hash, routine->action_device_hash);
      proactive_hash_u32(&hash,
                         ((uint32_t)routine->action_siid << 16) |
                         routine->action_piid);
      proactive_hash_u32(&hash, (uint32_t)routine->action_value);
      proactive_hash_u32(&hash, routine->trigger_device_hash);
    }
  while (*text != '\0')
    {
      hash = (hash ^ *text++) * 16777619u;
    }
  return hash == 0 ? 1 : hash;
}

static void proactive_profile_defaults(
  struct proactive_profile_record_s *profile)
{
  memset(profile, 0, sizeof(*profile));
  profile->magic = PROACTIVE_PROFILE_MAGIC;
  profile->version = PROACTIVE_PROFILE_VERSION;
  profile->size = sizeof(*profile);
  profile->reminder_cooldown_days = 1;
  profile->next_routine_id = 1;
}

/* Sleep samples are shifted across midnight before averaging.  This makes
 * 23:55 and 00:05 converge to midnight instead of noon.
 */

static unsigned int proactive_shift_sleep_minute(unsigned int minute)
{
  minute %= 24u * 60u;
  return minute < 12u * 60u ? minute + 24u * 60u : minute;
}

static unsigned int proactive_preferred_minute(void)
{
  unsigned int shifted;

  if (g_proactive.profile.sleep_sample_count == 0)
    {
      return PROACTIVE_DEFAULT_SLEEP_MIN;
    }

  shifted = g_proactive.profile.sleep_shifted_minute_total /
            g_proactive.profile.sleep_sample_count;
  return shifted % (24u * 60u);
}

static unsigned int proactive_minute_distance(unsigned int first,
                                               unsigned int second)
{
  unsigned int distance;

  first %= 24u * 60u;
  second %= 24u * 60u;
  distance = first > second ? first - second : second - first;
  return distance > 12u * 60u ? 24u * 60u - distance : distance;
}

static unsigned int proactive_routine_confidence(
  const struct proactive_routine_record_s *routine)
{
  unsigned int feedback_count =
    routine->accepted_count + routine->rejected_count;
  unsigned int evidence;
  unsigned int feedback;
  unsigned int consistency;
  unsigned int penalty;
  unsigned int score;

  evidence = routine->observation_count *
             (routine->trigger_device_hash == 0 ? 12u : 16u);
  if (evidence > 60)
    {
      evidence = 60;
    }

  feedback = feedback_count == 0 ? 10 :
             routine->accepted_count * 25u / feedback_count;
  consistency = routine->mean_deviation_minutes <= 15 ? 15 :
                routine->mean_deviation_minutes <= 30 ? 10 :
                routine->mean_deviation_minutes <= 60 ? 5 : 0;
  penalty = routine->rejected_count * 8u;
  if (penalty > 30)
    {
      penalty = 30;
    }

  score = evidence + feedback + consistency;
  return score > penalty ? score - penalty : 0;
}

static bool proactive_routine_ready(
  const struct proactive_routine_record_s *routine)
{
  unsigned int minimum = routine->trigger_device_hash == 0 ?
                         PROACTIVE_MIN_TIME_DAYS :
                         PROACTIVE_MIN_EVENT_OCCURRENCES;

  return (routine->flags & ROUTINE_FLAG_ACTIVE) != 0 &&
         routine->observation_count >= minimum &&
         proactive_routine_confidence(routine) >=
           PROACTIVE_ROUTINE_THRESHOLD;
}

static bool proactive_routine_time_matches(
  const struct proactive_routine_record_s *routine,
  unsigned int minute_of_day)
{
  unsigned int window;

  if (routine->trigger_device_hash != 0 &&
      routine->observation_count >= 10 &&
      routine->accepted_count >= 3)
    {
      return true;
    }

  window = 120;
  if (routine->observation_count > 2)
    {
      unsigned int reduction = (routine->observation_count - 2) * 10u;

      window = reduction >= 90 ? 30 : 120 - reduction;
    }
  if (routine->mean_deviation_minutes > window)
    {
      window = routine->mean_deviation_minutes;
    }
  if (window > 180)
    {
      window = 180;
    }

  return proactive_minute_distance(
           minute_of_day, routine->mean_shifted_minute %
                          (24u * 60u)) <= window;
}

static int proactive_trigger_value(
  enum home_proactive_event_kind_e kind, int value)
{
  int64_t extended = value;
  int64_t magnitude;
  int64_t step;

  if (kind != HOME_PROACTIVE_EVENT_NUMBER)
    {
      return value;
    }

  magnitude = extended < 0 ? -extended : extended;
  step = magnitude <= 100 ? 5 : magnitude <= 1000 ? 10 : 50;
  return (int)(extended >= 0 ?
               ((extended + step / 2) / step) * step :
               ((extended - step / 2) / step) * step);
}

static bool proactive_event_matches_trigger(
  const struct home_proactive_event_s *event,
  const struct proactive_routine_record_s *routine)
{
  return routine->trigger_device_hash != 0 &&
         routine->trigger_device_hash == event->device_hash &&
         routine->trigger_siid == event->siid &&
         routine->trigger_piid == event->piid &&
         routine->trigger_kind == (uint8_t)event->kind &&
         proactive_trigger_value(event->kind, routine->trigger_value) ==
           proactive_trigger_value(event->kind, event->value);
}

static bool proactive_event_matches_action(
  const struct home_proactive_event_s *event,
  const struct proactive_routine_record_s *routine)
{
  return routine->action_device_hash == event->device_hash &&
         routine->action_siid == event->siid &&
         routine->action_piid == event->piid &&
         routine->action_kind == (uint8_t)event->kind &&
         routine->action_value == event->value;
}

static int proactive_find_routine(
  const struct home_proactive_event_s *trigger,
  const struct home_proactive_event_s *action)
{
  unsigned int index;

  for (index = 0; index < g_proactive.profile.routine_count; index++)
    {
      struct proactive_routine_record_s *routine =
        &g_proactive.profile.routines[index];

      if (!proactive_event_matches_action(action, routine))
        {
          continue;
        }
      if (trigger == NULL && routine->trigger_device_hash == 0)
        {
          return (int)index;
        }
      if (trigger != NULL &&
          proactive_event_matches_trigger(trigger, routine))
        {
          return (int)index;
        }
    }
  return -1;
}

static int proactive_allocate_routine(void)
{
  unsigned int index;
  unsigned int weakest = 0;
  unsigned int weakest_score = UINT32_MAX;

  if (g_proactive.profile.routine_count < PROACTIVE_MAX_ROUTINES)
    {
      index = g_proactive.profile.routine_count++;
      memset(&g_proactive.profile.routines[index], 0,
             sizeof(g_proactive.profile.routines[index]));
      g_proactive.profile.routines[index].routine_id =
        g_proactive.profile.next_routine_id++;
      if (g_proactive.profile.next_routine_id == 0)
        {
          g_proactive.profile.next_routine_id = 1;
        }
      return (int)index;
    }

  for (index = 0; index < PROACTIVE_MAX_ROUTINES; index++)
    {
      const struct proactive_routine_record_s *routine =
        &g_proactive.profile.routines[index];
      unsigned int score;

      if ((routine->flags & ROUTINE_FLAG_AUTOMATION) != 0)
        {
          continue;
        }
      score = proactive_routine_confidence(routine);
      if (score < weakest_score)
        {
          weakest = index;
          weakest_score = score;
        }
    }

  if (weakest_score == UINT32_MAX)
    {
      return -1;
    }
  memset(&g_proactive.profile.routines[weakest], 0,
         sizeof(g_proactive.profile.routines[weakest]));
  g_proactive.profile.routines[weakest].routine_id =
    g_proactive.profile.next_routine_id++;
  if (g_proactive.profile.next_routine_id == 0)
    {
      g_proactive.profile.next_routine_id = 1;
    }
  return (int)weakest;
}

static void proactive_update_average(uint16_t *average,
                                     uint16_t sample,
                                     uint16_t count)
{
  int difference;
  int next;

  if (count <= 1)
    {
      *average = sample;
      return;
    }
  difference = (int)sample - (int)*average;
  next = (int)*average + difference / (int)count;
  if (next < 0)
    {
      next = 0;
    }
  else if (next > UINT16_MAX)
    {
      next = UINT16_MAX;
    }
  *average = (uint16_t)next;
}

static void proactive_learn_routine(
  const struct home_proactive_event_s *trigger,
  const struct home_proactive_event_s *action)
{
  struct proactive_routine_record_s *routine;
  unsigned int shifted;
  unsigned int deviation;
  unsigned int delay_seconds = 0;
  int index;

  index = proactive_find_routine(trigger, action);
  if (index < 0)
    {
      index = proactive_allocate_routine();
      if (index < 0)
        {
          return;
        }

      routine = &g_proactive.profile.routines[index];
      routine->flags = ROUTINE_FLAG_ACTIVE;
      routine->action_device_hash = action->device_hash;
      routine->action_siid = action->siid;
      routine->action_piid = action->piid;
      routine->action_kind = (uint8_t)action->kind;
      routine->action_value = action->value;
      if (trigger != NULL)
        {
          routine->trigger_device_hash = trigger->device_hash;
          routine->trigger_siid = trigger->siid;
          routine->trigger_piid = trigger->piid;
          routine->trigger_kind = (uint8_t)trigger->kind;
          routine->trigger_value =
            proactive_trigger_value(trigger->kind, trigger->value);
        }
    }
  else
    {
      routine = &g_proactive.profile.routines[index];
    }

  if (trigger == NULL &&
      routine->last_observation_day == action->day_ordinal)
    {
      return;
    }

  if (g_proactive.profile.last_learning_day != action->day_ordinal)
    {
      g_proactive.profile.history_days++;
      g_proactive.profile.last_learning_day = action->day_ordinal;
    }
  if (routine->observation_count < UINT16_MAX)
    {
      routine->observation_count++;
    }
  routine->last_observation_day = action->day_ordinal;
  shifted = proactive_shift_sleep_minute(action->minute_of_day);
  deviation = routine->observation_count <= 1 ? 0 :
              proactive_minute_distance(
                action->minute_of_day,
                routine->mean_shifted_minute % (24u * 60u));
  proactive_update_average(&routine->mean_shifted_minute,
                           (uint16_t)shifted,
                           routine->observation_count);
  proactive_update_average(&routine->mean_deviation_minutes,
                           (uint16_t)deviation,
                           routine->observation_count);

  if (trigger != NULL)
    {
      delay_seconds = (uint32_t)(action->now_ms - trigger->now_ms) / 1000u;
      if (delay_seconds > 3600)
        {
          delay_seconds = 3600;
        }
      proactive_update_average(&routine->mean_delay_seconds,
                               (uint16_t)delay_seconds,
                               routine->observation_count);
    }
  proactive_mark_routine_updated(index, HOME_PROACTIVE_UPDATE_OBSERVATION);
}

static void proactive_activate_routine(unsigned int index,
                                       uint32_t now_ms)
{
  struct proactive_routine_record_s *routine;

  if (index >= g_proactive.profile.routine_count)
    {
      return;
    }
  routine = &g_proactive.profile.routines[index];
  if (!proactive_routine_ready(routine) ||
      routine->last_executed_day == g_proactive.context.day_ordinal ||
      !proactive_routine_time_matches(routine,
                                      g_proactive.context.minute_of_day))
    {
      return;
    }

  g_proactive.pending_routine = (int)index;
  g_proactive.pending_due_ms =
    now_ms + (uint32_t)routine->mean_delay_seconds * 1000u;
  if (routine->mean_delay_seconds == 0)
    {
      g_proactive.active_routine = (int)index;
      g_proactive.pending_routine = -1;
    }
  proactive_bump_revision();
}

static unsigned int proactive_confidence(void)
{
  unsigned int count = g_proactive.profile.feedback_count;
  unsigned int evidence;
  unsigned int acceptance;
  unsigned int score;

  if (count == 0)
    {
      return 15;
    }

  evidence = count > 6 ? 40 : count * 6;
  acceptance = g_proactive.profile.accepted_count * 45 / count;
  score = 10 + evidence + acceptance;
  return score > 95 ? 95 : score;
}

static enum home_proactive_source_e proactive_source(void)
{
  if (g_proactive.mode != HOME_PROACTIVE_REAL)
    {
      return HOME_PROACTIVE_SOURCE_DEMO;
    }

  return (g_proactive.profile.sleep_sample_count >= 2 ||
          g_proactive.profile.routine_count > 0) ?
         HOME_PROACTIVE_SOURCE_LEARNED : HOME_PROACTIVE_SOURCE_RULE;
}

static bool proactive_should_suggest(void)
{
  unsigned int confidence;
  unsigned int preferred;
  unsigned int window;

  if (g_proactive.active_routine >= 0 &&
      (unsigned int)g_proactive.active_routine <
        g_proactive.profile.routine_count)
    {
      const struct proactive_routine_record_s *routine =
        &g_proactive.profile.routines[g_proactive.active_routine];

      return g_proactive.context.clock_valid &&
             g_proactive.context.day_ordinal != 0 &&
             proactive_routine_ready(routine) &&
             routine->last_executed_day !=
               g_proactive.context.day_ordinal;
    }

  if (g_proactive.context.lights_on == 0 ||
      !g_proactive.context.target_available)
    {
      return false;
    }
  if (g_proactive.suggestion_dismissed)
    {
      return false;
    }
  if (g_proactive.profile.last_observation_day != 0 &&
      g_proactive.context.day_ordinal != 0 &&
      g_proactive.context.day_ordinal <
      g_proactive.profile.last_observation_day +
      g_proactive.profile.reminder_cooldown_days)
    {
      return false;
    }
  if (g_proactive.mode == HOME_PROACTIVE_REPLAY)
    {
      return false;
    }
  if (g_proactive.mode == HOME_PROACTIVE_DEMO_READY)
    {
      return true;
    }
  if (!g_proactive.context.clock_valid ||
      g_proactive.context.day_ordinal == 0)
    {
      return false;
    }

  confidence = proactive_confidence();
  if (g_proactive.profile.feedback_count >= 4 && confidence < 45)
    {
      return false;
    }

  preferred = proactive_preferred_minute();
  window = PROACTIVE_COLD_WINDOW_MINUTES;
  if (g_proactive.profile.sleep_sample_count > 0)
    {
      unsigned int reduction =
        g_proactive.profile.sleep_sample_count > 6 ? 30 :
        g_proactive.profile.sleep_sample_count * 5;

      window = PROACTIVE_COLD_WINDOW_MINUTES - reduction;
      if (window < PROACTIVE_MIN_WINDOW_MINUTES)
        {
          window = PROACTIVE_MIN_WINDOW_MINUTES;
        }
    }

  return proactive_minute_distance(g_proactive.context.minute_of_day,
                                   preferred) <= window;
}

static bool proactive_apply_feedback(
  uint32_t day_ordinal, unsigned int minute_of_day,
  enum home_proactive_feedback_e feedback)
{
  if (feedback < HOME_PROACTIVE_ACCEPT ||
      feedback > HOME_PROACTIVE_ENABLE_AUTOMATION || day_ordinal == 0)
    {
      return false;
    }
  if (g_proactive.profile.last_observation_day != 0 &&
      day_ordinal < g_proactive.profile.last_observation_day)
    {
      return false;
    }

  if (g_proactive.profile.feedback_count >= PROACTIVE_MAX_FEEDBACK)
    {
      g_proactive.profile.accepted_count /= 2;
      g_proactive.profile.ignored_count /= 2;
      g_proactive.profile.feedback_count =
        g_proactive.profile.accepted_count +
        g_proactive.profile.ignored_count;
      g_proactive.profile.history_days /= 2;
      if (g_proactive.profile.history_days >
          g_proactive.profile.feedback_count)
        {
          g_proactive.profile.history_days =
            g_proactive.profile.feedback_count;
        }
    }

  if (day_ordinal != g_proactive.profile.last_learning_day)
    {
      g_proactive.profile.history_days++;
      g_proactive.profile.last_learning_day = day_ordinal;
    }
  g_proactive.profile.feedback_count++;
  g_proactive.profile.last_observation_day = day_ordinal;

  if (feedback == HOME_PROACTIVE_ACCEPT ||
      feedback == HOME_PROACTIVE_ENABLE_AUTOMATION)
    {
      if (g_proactive.profile.sleep_sample_count >= PROACTIVE_MAX_SAMPLES)
        {
          g_proactive.profile.sleep_sample_count /= 2;
          g_proactive.profile.sleep_shifted_minute_total /= 2;
        }
      g_proactive.profile.accepted_count++;
      g_proactive.profile.sleep_sample_count++;
      g_proactive.profile.sleep_shifted_minute_total +=
        proactive_shift_sleep_minute(minute_of_day);
      if (g_proactive.profile.reminder_cooldown_days > 1)
        {
          g_proactive.profile.reminder_cooldown_days--;
        }
    }
  else
    {
      g_proactive.profile.ignored_count++;
      if (feedback == HOME_PROACTIVE_LESS_OFTEN &&
          g_proactive.profile.reminder_cooldown_days < 7)
        {
          g_proactive.profile.reminder_cooldown_days++;
        }
    }

  return true;
}

void home_proactive_initialize(void)
{
  memset(&g_proactive, 0, sizeof(g_proactive));
  proactive_profile_defaults(&g_proactive.profile);
  g_proactive.mode = HOME_PROACTIVE_REAL;
  g_proactive.revision = 1;
  g_proactive.active_routine = -1;
  g_proactive.pending_routine = -1;
  g_proactive.last_updated_routine = -1;
}

void home_proactive_reset(void)
{
  struct home_proactive_context_s context = g_proactive.context;

  home_proactive_initialize();
  g_proactive.context = context;
  proactive_bump_revision();
}

void home_proactive_set_context(
  const struct home_proactive_context_s *context)
{
  struct home_proactive_context_s next;

  if (context == NULL)
    {
      return;
    }

  memcpy(&next, context, sizeof(next));
  next.minute_of_day %= 24u * 60u;
  next.target_did[sizeof(next.target_did) - 1] = '\0';
  next.target_name[sizeof(next.target_name) - 1] = '\0';
  if (g_proactive.mode != HOME_PROACTIVE_REAL)
    {
      next.clock_valid = true;
      next.day_ordinal = g_proactive.context.day_ordinal;
      next.minute_of_day = g_proactive.context.minute_of_day;
    }

  if (memcmp(&g_proactive.context, &next, sizeof(next)) != 0)
    {
      if (next.day_ordinal != g_proactive.dismissed_day &&
          g_proactive.mode == HOME_PROACTIVE_REAL)
        {
          g_proactive.suggestion_dismissed = false;
        }
      memcpy(&g_proactive.context, &next, sizeof(next));
      proactive_bump_revision();
    }
}

void home_proactive_observe_event(
  const struct home_proactive_event_s *event)
{
  unsigned int index;

  if (event == NULL || event->device_hash == 0 ||
      event->day_ordinal == 0 ||
      event->minute_of_day >= 24u * 60u ||
      event->kind > HOME_PROACTIVE_EVENT_ONLINE)
    {
      return;
    }

  g_proactive.context.day_ordinal = event->day_ordinal;
  g_proactive.context.minute_of_day = event->minute_of_day;
  if (!event->self_generated)
    {
      int best = -1;
      unsigned int best_score = 0;

      for (index = 0; index < g_proactive.profile.routine_count; index++)
        {
          struct proactive_routine_record_s *routine =
            &g_proactive.profile.routines[index];
          unsigned int score;

          if (!proactive_event_matches_trigger(event, routine) ||
              !proactive_routine_ready(routine))
            {
              continue;
            }
          score = proactive_routine_confidence(routine);
          if (best < 0 || score > best_score)
            {
              best = (int)index;
              best_score = score;
            }
        }
      if (best >= 0)
        {
          proactive_activate_routine((unsigned int)best, event->now_ms);
        }
    }

  if (!event->self_generated && g_proactive.active_routine >= 0 &&
      (unsigned int)g_proactive.active_routine <
        g_proactive.profile.routine_count &&
      proactive_event_matches_action(
        event, &g_proactive.profile.routines[g_proactive.active_routine]))
    {
      struct proactive_routine_record_s *routine =
        &g_proactive.profile.routines[g_proactive.active_routine];

      if (routine->accepted_count < UINT16_MAX)
        {
          routine->accepted_count++;
        }
      if (g_proactive.profile.accepted_count < UINT32_MAX)
        {
          g_proactive.profile.accepted_count++;
          g_proactive.profile.feedback_count++;
        }
      routine->last_executed_day = event->day_ordinal;
      proactive_mark_routine_updated(
        g_proactive.active_routine, HOME_PROACTIVE_UPDATE_ACCEPTED);
      g_proactive.active_routine = -1;
      g_proactive.pending_routine = -1;
      proactive_bump_revision();
    }

  if (event->writable && !event->self_generated)
    {
      proactive_learn_routine(NULL, event);
      for (index = g_proactive.recent_event_count; index > 0; index--)
        {
          const struct home_proactive_event_s *candidate =
            &g_proactive.recent_events[index - 1];
          unsigned int newer;
          bool duplicate = false;

          if ((uint32_t)(event->now_ms - candidate->now_ms) >
                PROACTIVE_TRIGGER_WINDOW_MS)
            {
              break;
            }
          if ((candidate->device_hash == event->device_hash &&
               candidate->siid == event->siid &&
               candidate->piid == event->piid) ||
              candidate->self_generated)
            {
              continue;
            }

          for (newer = index;
               newer < g_proactive.recent_event_count; newer++)
            {
              const struct home_proactive_event_s *other =
                &g_proactive.recent_events[newer];

              if (other->device_hash == candidate->device_hash &&
                  other->siid == candidate->siid &&
                  other->piid == candidate->piid &&
                  other->kind == candidate->kind &&
                  proactive_trigger_value(other->kind, other->value) ==
                    proactive_trigger_value(candidate->kind,
                                            candidate->value))
                {
                  duplicate = true;
                  break;
                }
            }
          if (!duplicate)
            {
              proactive_learn_routine(candidate, event);
            }
        }
      proactive_bump_revision();
    }

  if (!event->self_generated)
    {
      if (g_proactive.recent_event_count == PROACTIVE_RECENT_EVENTS)
        {
          memmove(&g_proactive.recent_events[0],
                  &g_proactive.recent_events[1],
                  sizeof(g_proactive.recent_events[0]) *
                  (PROACTIVE_RECENT_EVENTS - 1));
          g_proactive.recent_event_count--;
        }
      g_proactive.recent_events[g_proactive.recent_event_count++] = *event;
    }
}

void home_proactive_start_replay(uint32_t now_ms)
{
  if (!g_proactive.replay_backup_valid)
    {
      g_proactive.replay_backup = g_proactive.profile;
      g_proactive.replay_backup_valid = true;
    }
  proactive_profile_defaults(&g_proactive.profile);
  g_proactive.mode = HOME_PROACTIVE_REPLAY;
  g_proactive.replay_started_ms = now_ms;
  g_proactive.replay_day = 0;
  g_proactive.suggestion_dismissed = false;
  g_proactive.active_routine = -1;
  g_proactive.pending_routine = -1;
  g_proactive.last_updated_routine = -1;
  g_proactive.recent_event_count = 0;
  g_proactive.automation_inflight = false;
  proactive_bump_revision();
}

void home_proactive_stop_replay(void)
{
  if (g_proactive.replay_backup_valid)
    {
      g_proactive.profile = g_proactive.replay_backup;
    }
  g_proactive.replay_backup_valid = false;
  g_proactive.mode = HOME_PROACTIVE_REAL;
  g_proactive.replay_day = 0;
  g_proactive.suggestion_dismissed = false;
  g_proactive.active_routine = -1;
  g_proactive.pending_routine = -1;
  g_proactive.last_updated_routine = -1;
  g_proactive.recent_event_count = 0;
  g_proactive.automation_inflight = false;
  proactive_bump_revision();
}

void home_proactive_tick(uint32_t now_ms)
{
  unsigned int target_day;
  unsigned int index;

  if (g_proactive.mode != HOME_PROACTIVE_REPLAY)
    {
      if (g_proactive.pending_routine >= 0 &&
          (int32_t)(now_ms - g_proactive.pending_due_ms) >= 0)
        {
          g_proactive.active_routine = g_proactive.pending_routine;
          g_proactive.pending_routine = -1;
          proactive_bump_revision();
        }
      if (g_proactive.active_routine < 0 &&
          g_proactive.pending_routine < 0 &&
          g_proactive.context.clock_valid)
        {
          for (index = 0;
               index < g_proactive.profile.routine_count; index++)
            {
              const struct proactive_routine_record_s *routine =
                &g_proactive.profile.routines[index];

              if (routine->trigger_device_hash == 0 &&
                  proactive_routine_time_matches(
                    routine, g_proactive.context.minute_of_day))
                {
                  proactive_activate_routine(index, now_ms);
                  if (g_proactive.active_routine >= 0 ||
                      g_proactive.pending_routine >= 0)
                    {
                      break;
                    }
                }
            }
        }
      return;
    }

  target_day = (now_ms - g_proactive.replay_started_ms) /
               PROACTIVE_REPLAY_STEP_MS;
  if (target_day > sizeof(g_replay_events) / sizeof(g_replay_events[0]))
    {
      target_day = sizeof(g_replay_events) / sizeof(g_replay_events[0]);
    }

  while (g_proactive.replay_day < target_day)
    {
      const struct proactive_replay_event_s *event =
        &g_replay_events[g_proactive.replay_day];

      proactive_apply_feedback(g_proactive.replay_day + 1,
                               event->minute_of_day, event->feedback);
      g_proactive.replay_day++;
      proactive_bump_revision();
    }

  if (g_proactive.replay_day ==
      sizeof(g_replay_events) / sizeof(g_replay_events[0]))
    {
      g_proactive.mode = HOME_PROACTIVE_DEMO_READY;
      g_proactive.context.clock_valid = true;
      g_proactive.context.day_ordinal = 8;
      g_proactive.context.minute_of_day = 23 * 60 + 8;
      proactive_bump_revision();
    }
}

void home_proactive_feedback(enum home_proactive_feedback_e feedback)
{
  if (g_proactive.active_routine >= 0 &&
      (unsigned int)g_proactive.active_routine <
        g_proactive.profile.routine_count)
    {
      struct proactive_routine_record_s *routine =
        &g_proactive.profile.routines[g_proactive.active_routine];

      if (feedback < HOME_PROACTIVE_ACCEPT ||
          feedback > HOME_PROACTIVE_ENABLE_AUTOMATION)
        {
          return;
        }
      if (feedback == HOME_PROACTIVE_ACCEPT ||
          feedback == HOME_PROACTIVE_ENABLE_AUTOMATION)
        {
          if (routine->accepted_count < UINT16_MAX)
            {
              routine->accepted_count++;
            }
          g_proactive.profile.accepted_count++;
          if (feedback == HOME_PROACTIVE_ENABLE_AUTOMATION)
            {
              routine->flags |= ROUTINE_FLAG_AUTOMATION;
            }
        }
      else
        {
          if (routine->rejected_count < UINT16_MAX)
            {
              routine->rejected_count++;
            }
          g_proactive.profile.ignored_count++;
        }
      g_proactive.profile.feedback_count++;
      routine->last_executed_day = g_proactive.context.day_ordinal;
      proactive_mark_routine_updated(
        g_proactive.active_routine,
        feedback == HOME_PROACTIVE_ENABLE_AUTOMATION ?
          HOME_PROACTIVE_UPDATE_AUTOMATION_ENABLED :
        feedback == HOME_PROACTIVE_ACCEPT ?
          HOME_PROACTIVE_UPDATE_ACCEPTED :
          HOME_PROACTIVE_UPDATE_REJECTED);
      g_proactive.active_routine = -1;
      g_proactive.pending_routine = -1;
      g_proactive.automation_inflight = false;
      proactive_bump_revision();
      return;
    }

  if (!proactive_should_suggest() ||
      !proactive_apply_feedback(g_proactive.context.day_ordinal,
                                g_proactive.context.minute_of_day,
                                feedback))
    {
      return;
    }

  g_proactive.suggestion_dismissed = true;
  g_proactive.dismissed_day = g_proactive.context.day_ordinal;
  proactive_bump_revision();
}

void home_proactive_automation_result(bool success)
{
  struct proactive_routine_record_s *routine;

  if (!g_proactive.automation_inflight ||
      g_proactive.active_routine < 0 ||
      (unsigned int)g_proactive.active_routine >=
        g_proactive.profile.routine_count)
    {
      return;
    }

  routine = &g_proactive.profile.routines[g_proactive.active_routine];
  routine->last_executed_day = g_proactive.context.day_ordinal;
  proactive_mark_routine_updated(
    g_proactive.active_routine, HOME_PROACTIVE_UPDATE_EXECUTION_RESULT);
  (void)success;
  g_proactive.automation_inflight = false;
  g_proactive.active_routine = -1;
  g_proactive.pending_routine = -1;
  proactive_bump_revision();
}

bool home_proactive_claim_automation(uint32_t decision_key)
{
  struct proactive_routine_record_s *routine;

  if (g_proactive.automation_inflight ||
      g_proactive.active_routine < 0 ||
      (unsigned int)g_proactive.active_routine >=
        g_proactive.profile.routine_count ||
      decision_key != proactive_decision_key())
    {
      return false;
    }

  routine = &g_proactive.profile.routines[g_proactive.active_routine];
  if ((routine->flags & ROUTINE_FLAG_AUTOMATION) == 0)
    {
      return false;
    }
  g_proactive.automation_inflight = true;
  return true;
}

int home_proactive_disable_automation(uint32_t action_device_hash,
                                      uint16_t siid, uint16_t piid)
{
  unsigned int index;

  for (index = 0; index < g_proactive.profile.routine_count; index++)
    {
      struct proactive_routine_record_s *routine =
        &g_proactive.profile.routines[index];

      if (routine->action_device_hash == action_device_hash &&
          routine->action_siid == siid && routine->action_piid == piid &&
          (routine->flags & ROUTINE_FLAG_AUTOMATION) != 0)
        {
          routine->flags &= (uint8_t)~ROUTINE_FLAG_AUTOMATION;
          proactive_mark_routine_updated(
            (int)index, HOME_PROACTIVE_UPDATE_AUTOMATION_DISABLED);
          proactive_bump_revision();
          return 0;
        }
    }
  return -ENOENT;
}

void home_proactive_get_snapshot(struct home_proactive_snapshot_s *snapshot)
{
  unsigned int preferred;
  unsigned int index;

  if (snapshot == NULL)
    {
      return;
    }

  memset(snapshot, 0, sizeof(*snapshot));
  preferred = proactive_preferred_minute();
  snapshot->revision = g_proactive.revision;
  snapshot->decision_key = proactive_decision_key();
  snapshot->mode = g_proactive.mode;
  snapshot->replay_day = g_proactive.replay_day;
  snapshot->history_days = g_proactive.profile.history_days;
  snapshot->feedback_count = g_proactive.profile.feedback_count;
  snapshot->accepted_count = g_proactive.profile.accepted_count;
  snapshot->ignored_count = g_proactive.profile.ignored_count;
  snapshot->confidence = proactive_confidence();
  snapshot->preferred_hour = preferred / 60;
  snapshot->preferred_minute = preferred % 60;
  snapshot->current_minute_of_day =
    g_proactive.context.minute_of_day;
  snapshot->reminder_cooldown_days =
    g_proactive.profile.reminder_cooldown_days;
  snapshot->routine_count = g_proactive.profile.routine_count;
  for (index = 0; index < g_proactive.profile.routine_count; index++)
    {
      const struct proactive_routine_record_s *routine =
        &g_proactive.profile.routines[index];

      snapshot->routine_observations += routine->observation_count;
      if ((routine->flags & ROUTINE_FLAG_AUTOMATION) != 0)
        {
          snapshot->automation_count++;
        }
    }
  if (g_proactive.pending_learning_count > 0 &&
      g_proactive.pending_learning[0].routine_index >= 0 &&
      (unsigned int)g_proactive.pending_learning[0].routine_index <
        g_proactive.profile.routine_count)
    {
      int learning_index =
        g_proactive.pending_learning[0].routine_index;
      const struct proactive_routine_record_s *learned =
        &g_proactive.profile.routines[learning_index];

      snapshot->learning_revision =
        g_proactive.pending_learning[0].revision;
      snapshot->learning_routine_id = learned->routine_id;
      snapshot->learning_observations = learned->observation_count;
      snapshot->learning_accepted = learned->accepted_count;
      snapshot->learning_rejected = learned->rejected_count;
      snapshot->learning_confidence =
        proactive_routine_confidence(learned);
      snapshot->learning_mean_minute =
        learned->mean_shifted_minute % (24u * 60u);
      snapshot->learning_deviation_minutes =
        learned->mean_deviation_minutes;
      snapshot->learning_delay_seconds = learned->mean_delay_seconds;
      snapshot->learning_kind =
        learned->trigger_device_hash == 0 ?
          HOME_PROACTIVE_CANDIDATE_TIME_ROUTINE :
          HOME_PROACTIVE_CANDIDATE_EVENT_ROUTINE;
      snapshot->learning_update_kind =
        g_proactive.pending_learning[0].kind;
      snapshot->learning_automation_enabled =
        (learned->flags & ROUTINE_FLAG_AUTOMATION) != 0;
    }
  snapshot->source = proactive_source();
  snapshot->suggestion_available = proactive_should_suggest();
  snapshot->profile_learned =
    g_proactive.profile.sleep_sample_count >= 2 ||
    g_proactive.profile.routine_count > 0;
  snapshot->clock_valid = g_proactive.context.clock_valid;
  snapshot->network_online = g_proactive.context.network_online;
  snapshot->target_available = g_proactive.context.target_available;
  snapshot->air_conditioner_on = g_proactive.context.air_conditioner_on;
  snapshot->lights_on = g_proactive.context.lights_on;
  memcpy(snapshot->target_did, g_proactive.context.target_did,
         sizeof(snapshot->target_did));
  memcpy(snapshot->target_name, g_proactive.context.target_name,
         sizeof(snapshot->target_name));
  snapshot->target_siid = g_proactive.context.target_siid;
  snapshot->target_piid = g_proactive.context.target_piid;
  snapshot->candidate_kind = HOME_PROACTIVE_CANDIDATE_SLEEP;
  if (g_proactive.active_routine >= 0 &&
      (unsigned int)g_proactive.active_routine <
        g_proactive.profile.routine_count)
    {
      struct proactive_routine_record_s *routine =
        &g_proactive.profile.routines[g_proactive.active_routine];

      snapshot->candidate_kind =
        routine->trigger_device_hash == 0 ?
          HOME_PROACTIVE_CANDIDATE_TIME_ROUTINE :
          HOME_PROACTIVE_CANDIDATE_EVENT_ROUTINE;
      snapshot->routine_id = routine->routine_id;
      snapshot->candidate_observations = routine->observation_count;
      snapshot->candidate_accepted = routine->accepted_count;
      snapshot->candidate_rejected = routine->rejected_count;
      snapshot->candidate_deviation_minutes =
        routine->mean_deviation_minutes;
      snapshot->confidence = proactive_routine_confidence(routine);
      snapshot->preferred_hour =
        (routine->mean_shifted_minute % (24u * 60u)) / 60u;
      snapshot->preferred_minute =
        routine->mean_shifted_minute % 60u;
      snapshot->action_device_hash = routine->action_device_hash;
      snapshot->trigger_device_hash = routine->trigger_device_hash;
      snapshot->action_siid = routine->action_siid;
      snapshot->action_piid = routine->action_piid;
      snapshot->action_value = routine->action_value;
      snapshot->action_is_boolean =
        routine->action_kind == HOME_PROACTIVE_EVENT_BOOLEAN;
      snapshot->automation_enabled =
        (routine->flags & ROUTINE_FLAG_AUTOMATION) != 0;
      snapshot->automation_due =
        snapshot->automation_enabled && !g_proactive.automation_inflight;
      snapshot->predicted_delay_seconds = routine->mean_delay_seconds;
    }
}

bool home_proactive_ack_learning(uint32_t learning_revision,
                                 uint32_t routine_id)
{
  int index;

  if (g_proactive.pending_learning_count == 0 ||
      g_proactive.pending_learning[0].revision != learning_revision)
    {
      return false;
    }
  index = g_proactive.pending_learning[0].routine_index;
  if (index < 0 ||
      (unsigned int)index >= g_proactive.profile.routine_count ||
      g_proactive.profile.routines[index].routine_id != routine_id)
    {
      return false;
    }

  g_proactive.pending_learning_count--;
  if (g_proactive.pending_learning_count > 0)
    {
      memmove(&g_proactive.pending_learning[0],
              &g_proactive.pending_learning[1],
              sizeof(g_proactive.pending_learning[0]) *
                g_proactive.pending_learning_count);
    }
  return true;
}

int home_proactive_export_profile(void *buffer, size_t capacity,
                                  size_t *length)
{
  struct proactive_profile_record_s profile;

  if (buffer == NULL || length == NULL)
    {
      return -EINVAL;
    }
  if (capacity < sizeof(profile))
    {
      return -ENOSPC;
    }

  profile = g_proactive.replay_backup_valid ? g_proactive.replay_backup :
                                              g_proactive.profile;
  profile.checksum = 0;
  profile.checksum = proactive_crc32(&profile,
                                     offsetof(
                                       struct proactive_profile_record_s,
                                       checksum));
  memcpy(buffer, &profile, sizeof(profile));
  *length = sizeof(profile);
  memset(&profile, 0, sizeof(profile));
  return 0;
}

static bool proactive_profile_valid(
  const struct proactive_profile_record_s *profile)
{
  uint32_t checksum;
  unsigned int index;

  checksum = proactive_crc32(
    profile, offsetof(struct proactive_profile_record_s, checksum));
  if (profile->magic != PROACTIVE_PROFILE_MAGIC ||
      profile->version != PROACTIVE_PROFILE_VERSION ||
      profile->size != sizeof(*profile) ||
      profile->checksum != checksum ||
      profile->accepted_count > profile->feedback_count ||
      profile->ignored_count !=
        profile->feedback_count - profile->accepted_count ||
      profile->sleep_sample_count > profile->accepted_count ||
      (profile->sleep_sample_count == 0) !=
        (profile->last_observation_day == 0) ||
      profile->reminder_cooldown_days < 1 ||
      profile->reminder_cooldown_days > 7 ||
      profile->routine_count > PROACTIVE_MAX_ROUTINES ||
      profile->next_routine_id == 0 ||
      (profile->sleep_sample_count != 0 &&
       profile->sleep_shifted_minute_total /
         profile->sleep_sample_count >= 36u * 60u))
    {
      return false;
    }

  for (index = 0; index < profile->routine_count; index++)
    {
      const struct proactive_routine_record_s *routine =
        &profile->routines[index];

      if ((routine->flags & ROUTINE_FLAG_ACTIVE) == 0 ||
          routine->routine_id == 0 ||
          routine->action_device_hash == 0 ||
          routine->action_siid == 0 || routine->action_piid == 0 ||
          routine->action_kind > HOME_PROACTIVE_EVENT_ONLINE ||
          routine->trigger_kind > HOME_PROACTIVE_EVENT_ONLINE ||
          routine->observation_count == 0 ||
          routine->mean_shifted_minute >= 36u * 60u ||
          routine->mean_deviation_minutes > 12u * 60u ||
          routine->mean_delay_seconds > 3600)
        {
          return false;
        }
    }
  return true;
}

static bool proactive_migrate_v2(
  const struct proactive_profile_v2_s *legacy,
  struct proactive_profile_record_s *profile)
{
  uint32_t checksum;

  checksum = proactive_crc32(
    legacy, offsetof(struct proactive_profile_v2_s, checksum));
  if (legacy->magic != PROACTIVE_PROFILE_MAGIC_V2 ||
      legacy->version != 2 || legacy->size != sizeof(*legacy) ||
      legacy->checksum != checksum ||
      legacy->accepted_count > legacy->feedback_count ||
      legacy->ignored_count !=
        legacy->feedback_count - legacy->accepted_count ||
      legacy->sleep_sample_count > legacy->accepted_count ||
      legacy->reminder_cooldown_days < 1 ||
      legacy->reminder_cooldown_days > 7)
    {
      return false;
    }

  proactive_profile_defaults(profile);
  profile->history_days = legacy->history_days;
  profile->feedback_count = legacy->feedback_count;
  profile->accepted_count = legacy->accepted_count;
  profile->ignored_count = legacy->ignored_count;
  profile->sleep_sample_count = legacy->sleep_sample_count;
  profile->sleep_shifted_minute_total =
    legacy->sleep_shifted_minute_total;
  profile->reminder_cooldown_days = legacy->reminder_cooldown_days;
  profile->last_observation_day = legacy->last_observation_day;
  profile->last_learning_day = legacy->last_observation_day;
  profile->last_learning_day = legacy->last_observation_day;
  return true;
}

static bool proactive_migrate_v4(
  const struct proactive_profile_v4_s *legacy,
  struct proactive_profile_record_s *profile)
{
  uint32_t checksum;

  checksum = proactive_crc32(
    legacy, offsetof(struct proactive_profile_v4_s, checksum));
  if (legacy->magic != PROACTIVE_PROFILE_MAGIC_V4 ||
      legacy->version != 4 || legacy->size != sizeof(*legacy) ||
      legacy->checksum != checksum || legacy->routine_count > 8)
    {
      return false;
    }

  proactive_profile_defaults(profile);
  profile->history_days = legacy->history_days;
  profile->feedback_count = legacy->feedback_count;
  profile->accepted_count = legacy->accepted_count;
  profile->ignored_count = legacy->ignored_count;
  profile->sleep_sample_count = legacy->sleep_sample_count;
  profile->sleep_shifted_minute_total =
    legacy->sleep_shifted_minute_total;
  profile->reminder_cooldown_days = legacy->reminder_cooldown_days;
  profile->last_observation_day = legacy->last_observation_day;
  profile->cloud_budget_day = legacy->cloud_budget_day;
  profile->last_learning_day = legacy->last_learning_day;
  profile->next_routine_id = legacy->next_routine_id;
  profile->cloud_budget_count = legacy->cloud_budget_count;
  profile->routine_count = legacy->routine_count;
  memcpy(profile->routines, legacy->routines,
         sizeof(legacy->routines));
  profile->checksum = proactive_crc32(
    profile, offsetof(struct proactive_profile_record_s, checksum));
  return proactive_profile_valid(profile);
}

static bool proactive_migrate_v1(
  const struct proactive_profile_v1_s *legacy,
  struct proactive_profile_record_s *profile)
{
  unsigned int average;

  if (legacy->magic != PROACTIVE_PROFILE_MAGIC_V1 ||
      legacy->version != 1 || legacy->size != sizeof(*legacy) ||
      legacy->accepted_count > legacy->feedback_count ||
      legacy->ignored_count !=
        legacy->feedback_count - legacy->accepted_count ||
      legacy->sleep_sample_count != legacy->feedback_count ||
      legacy->history_days > legacy->feedback_count ||
      (legacy->feedback_count == 0) !=
        (legacy->last_observation_day == 0) ||
      legacy->reminder_cooldown_days < 1 ||
      legacy->reminder_cooldown_days > 7)
    {
      return false;
    }

  proactive_profile_defaults(profile);
  profile->history_days = legacy->history_days;
  profile->feedback_count = legacy->feedback_count;
  profile->accepted_count = legacy->accepted_count;
  profile->ignored_count = legacy->ignored_count;
  profile->reminder_cooldown_days = legacy->reminder_cooldown_days;
  profile->last_observation_day = legacy->last_observation_day;
  if (legacy->sleep_sample_count > 0 && legacy->accepted_count > 0)
    {
      average = legacy->sleep_minute_total / legacy->sleep_sample_count;
      profile->sleep_sample_count = legacy->accepted_count;
      profile->sleep_shifted_minute_total =
        proactive_shift_sleep_minute(average) * legacy->accepted_count;
    }
  return true;
}

int home_proactive_import_profile(const void *buffer, size_t length)
{
  struct proactive_profile_record_s profile;
  bool valid = false;

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  memset(&profile, 0, sizeof(profile));
  if (length == sizeof(profile))
    {
      memcpy(&profile, buffer, sizeof(profile));
      valid = proactive_profile_valid(&profile);
    }
  else if (length == sizeof(struct proactive_profile_v4_s))
    {
      struct proactive_profile_v4_s legacy;

      memcpy(&legacy, buffer, sizeof(legacy));
      valid = proactive_migrate_v4(&legacy, &profile);
      memset(&legacy, 0, sizeof(legacy));
    }
  else if (length == sizeof(struct proactive_profile_v2_s))
    {
      struct proactive_profile_v2_s legacy;

      memcpy(&legacy, buffer, sizeof(legacy));
      valid = proactive_migrate_v2(&legacy, &profile);
      memset(&legacy, 0, sizeof(legacy));
    }
  else if (length == sizeof(struct proactive_profile_v1_s))
    {
      struct proactive_profile_v1_s legacy;

      memcpy(&legacy, buffer, sizeof(legacy));
      valid = proactive_migrate_v1(&legacy, &profile);
      memset(&legacy, 0, sizeof(legacy));
    }

  if (!valid)
    {
      memset(&profile, 0, sizeof(profile));
      return -EINVAL;
    }

  memcpy(&g_proactive.profile, &profile, sizeof(profile));
  memset(&profile, 0, sizeof(profile));
  g_proactive.replay_backup_valid = false;
  g_proactive.suggestion_dismissed = false;
  g_proactive.active_routine = -1;
  g_proactive.pending_routine = -1;
  g_proactive.recent_event_count = 0;
  g_proactive.automation_inflight = false;
  g_proactive.mode = HOME_PROACTIVE_REAL;
  proactive_bump_revision();
  return 0;
}
