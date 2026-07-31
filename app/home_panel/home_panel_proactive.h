/****************************************************************************
 * D13x home panel proactive intelligence engine
 ****************************************************************************/

#ifndef __HOME_PANEL_PROACTIVE_H
#define __HOME_PANEL_PROACTIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOME_PROACTIVE_DID_SIZE  80
#define HOME_PROACTIVE_NAME_SIZE 48

enum home_proactive_mode_e
{
  HOME_PROACTIVE_REAL = 0,
  HOME_PROACTIVE_REPLAY,
  HOME_PROACTIVE_DEMO_READY
};

enum home_proactive_feedback_e
{
  HOME_PROACTIVE_ACCEPT = 0,
  HOME_PROACTIVE_IGNORE_TODAY,
  HOME_PROACTIVE_LESS_OFTEN,
  HOME_PROACTIVE_ENABLE_AUTOMATION
};

enum home_proactive_source_e
{
  HOME_PROACTIVE_SOURCE_RULE = 0,
  HOME_PROACTIVE_SOURCE_LEARNED,
  HOME_PROACTIVE_SOURCE_DEMO
};

enum home_proactive_event_kind_e
{
  HOME_PROACTIVE_EVENT_BOOLEAN = 0,
  HOME_PROACTIVE_EVENT_NUMBER,
  HOME_PROACTIVE_EVENT_ONLINE
};

enum home_proactive_candidate_kind_e
{
  HOME_PROACTIVE_CANDIDATE_SLEEP = 0,
  HOME_PROACTIVE_CANDIDATE_TIME_ROUTINE,
  HOME_PROACTIVE_CANDIDATE_EVENT_ROUTINE
};

enum home_proactive_update_kind_e
{
  HOME_PROACTIVE_UPDATE_OBSERVATION = 0,
  HOME_PROACTIVE_UPDATE_ACCEPTED,
  HOME_PROACTIVE_UPDATE_REJECTED,
  HOME_PROACTIVE_UPDATE_AUTOMATION_ENABLED,
  HOME_PROACTIVE_UPDATE_AUTOMATION_DISABLED,
  HOME_PROACTIVE_UPDATE_EXECUTION_RESULT
};

struct home_proactive_event_s
{
  uint32_t device_hash;
  uint32_t day_ordinal;
  uint32_t now_ms;
  uint16_t minute_of_day;
  uint16_t siid;
  uint16_t piid;
  int value;
  enum home_proactive_event_kind_e kind;
  bool writable;
  bool self_generated;
};

struct home_proactive_context_s
{
  uint32_t day_ordinal;
  uint16_t minute_of_day;
  unsigned int lights_on;
  bool clock_valid;
  bool network_online;
  bool air_conditioner_on;
  bool target_available;
  char target_did[HOME_PROACTIVE_DID_SIZE];
  char target_name[HOME_PROACTIVE_NAME_SIZE];
  uint16_t target_siid;
  uint16_t target_piid;
};

struct home_proactive_snapshot_s
{
  uint32_t revision;
  uint32_t decision_key;
  enum home_proactive_mode_e mode;
  unsigned int replay_day;
  unsigned int history_days;
  unsigned int feedback_count;
  unsigned int accepted_count;
  unsigned int ignored_count;
  unsigned int confidence;
  unsigned int preferred_hour;
  unsigned int preferred_minute;
  unsigned int current_minute_of_day;
  unsigned int reminder_cooldown_days;
  unsigned int routine_count;
  unsigned int automation_count;
  unsigned int routine_observations;
  uint32_t routine_id;
  unsigned int candidate_observations;
  unsigned int candidate_accepted;
  unsigned int candidate_rejected;
  unsigned int candidate_deviation_minutes;
  uint32_t learning_revision;
  uint32_t learning_routine_id;
  unsigned int learning_observations;
  unsigned int learning_accepted;
  unsigned int learning_rejected;
  unsigned int learning_confidence;
  unsigned int learning_mean_minute;
  unsigned int learning_deviation_minutes;
  unsigned int learning_delay_seconds;
  enum home_proactive_candidate_kind_e learning_kind;
  enum home_proactive_update_kind_e learning_update_kind;
  bool learning_automation_enabled;
  enum home_proactive_source_e source;
  enum home_proactive_candidate_kind_e candidate_kind;
  bool suggestion_available;
  bool profile_learned;
  bool clock_valid;
  bool network_online;
  bool target_available;
  bool air_conditioner_on;
  unsigned int lights_on;
  char target_did[HOME_PROACTIVE_DID_SIZE];
  char target_name[HOME_PROACTIVE_NAME_SIZE];
  uint16_t target_siid;
  uint16_t target_piid;
  uint32_t action_device_hash;
  uint32_t trigger_device_hash;
  uint16_t action_siid;
  uint16_t action_piid;
  int action_value;
  bool action_is_boolean;
  bool automation_enabled;
  bool automation_due;
  unsigned int predicted_delay_seconds;
};

void home_proactive_initialize(void);
void home_proactive_reset(void);
void home_proactive_set_context(
  const struct home_proactive_context_s *context);
void home_proactive_observe_event(
  const struct home_proactive_event_s *event);
void home_proactive_start_replay(uint32_t now_ms);
void home_proactive_stop_replay(void);
void home_proactive_tick(uint32_t now_ms);
void home_proactive_feedback(enum home_proactive_feedback_e feedback);
bool home_proactive_claim_automation(uint32_t decision_key);
void home_proactive_automation_result(bool success);
bool home_proactive_ack_learning(uint32_t learning_revision,
                                 uint32_t routine_id);
int home_proactive_disable_automation(uint32_t action_device_hash,
                                      uint16_t siid, uint16_t piid);
void home_proactive_get_snapshot(struct home_proactive_snapshot_s *snapshot);
int home_proactive_export_profile(void *buffer, size_t capacity,
                                  size_t *length);
int home_proactive_import_profile(const void *buffer, size_t length);

#endif
