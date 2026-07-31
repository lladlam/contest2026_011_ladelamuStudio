/****************************************************************************
 * D13x home panel Mijia API client
 ****************************************************************************/

#ifndef __HOME_PANEL_MIJIA_CLIENT_H
#define __HOME_PANEL_MIJIA_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum home_panel_mijia_state_e
{
  HOME_PANEL_MIJIA_IDLE = 0,
  HOME_PANEL_MIJIA_STARTING,
  HOME_PANEL_MIJIA_WAITING,
  HOME_PANEL_MIJIA_AUTHENTICATED,
  HOME_PANEL_MIJIA_EXPIRED,
  HOME_PANEL_MIJIA_ERROR
};

enum home_panel_mijia_command_state_e
{
  HOME_PANEL_MIJIA_COMMAND_IDLE = 0,
  HOME_PANEL_MIJIA_COMMAND_PENDING,
  HOME_PANEL_MIJIA_COMMAND_ACCEPTED,
  HOME_PANEL_MIJIA_COMMAND_CONFIRMED,
  HOME_PANEL_MIJIA_COMMAND_ERROR
};

enum home_panel_agent_state_e
{
  HOME_PANEL_AGENT_IDLE = 0,
  HOME_PANEL_AGENT_PENDING,
  HOME_PANEL_AGENT_CLOUD_READY,
  HOME_PANEL_AGENT_LOCAL_FALLBACK,
  HOME_PANEL_AGENT_ERROR
};

enum home_panel_agent_decision_e
{
  HOME_PANEL_AGENT_PROPOSE = 0,
  HOME_PANEL_AGENT_SUPPRESS,
  HOME_PANEL_AGENT_DEFER
};

enum home_panel_agent_policy_e
{
  HOME_PANEL_AGENT_POLICY_NONE = 0,
  HOME_PANEL_AGENT_POLICY_TURN_OFF_SELECTED_LIGHT_KEEP_AC,
  HOME_PANEL_AGENT_POLICY_TURN_OFF_SELECTED_LIGHT,
  HOME_PANEL_AGENT_POLICY_NOTIFY_ONLY,
  HOME_PANEL_AGENT_POLICY_EXECUTE_LOCAL_CANDIDATE
};

struct home_panel_agent_request_s
{
  uint32_t context_revision;
  uint32_t routine_id;
  unsigned int source;
  unsigned int candidate_kind;
  unsigned int minute_of_day;
  unsigned int local_confidence;
  unsigned int routine_observations;
  unsigned int routine_accepted;
  unsigned int routine_rejected;
  unsigned int history_days;
  unsigned int feedback_count;
  unsigned int accepted_count;
  unsigned int ignored_count;
  unsigned int lights_on;
  bool local_eligible;
  bool automation_enabled;
  bool air_conditioner_on;
  bool network_online;
  bool demo;
};

struct home_panel_agent_learning_request_s
{
  uint32_t profile_revision;
  uint32_t routine_id;
  unsigned int routine_kind;
  unsigned int update_kind;
  unsigned int observation_count;
  unsigned int accepted_count;
  unsigned int rejected_count;
  unsigned int confidence;
  unsigned int mean_minute_of_day;
  unsigned int mean_deviation_minutes;
  unsigned int mean_delay_seconds;
  bool automation_enabled;
};

struct home_panel_agent_snapshot_s
{
  enum home_panel_agent_state_e state;
  enum home_panel_agent_decision_e decision;
  enum home_panel_agent_policy_e policy;
  uint32_t revision;
  uint32_t context_revision;
  uint64_t valid_until_monotonic_ms;
  unsigned int adjusted_confidence;
  unsigned int valid_for_seconds;
  bool requires_confirmation;
  char summary[128];
  char reasons[192];
  char caution[96];
};

struct home_panel_mijia_snapshot_s
{
  enum home_panel_mijia_state_e state;
  uint32_t revision;
  uint32_t qr_revision;
  size_t qr_size;
  unsigned int device_count;
  unsigned int online_count;
  char home_name[64];
  char message[96];
};

struct home_panel_mijia_family_snapshot_s
{
  uint32_t revision;
  uint32_t server_revision;
  uint32_t generated_at;
  size_t json_size;
  unsigned int home_count;
  unsigned int room_count;
  unsigned int device_count;
  unsigned int online_count;
  unsigned int scene_count;
  unsigned int detail_error_count;
  unsigned int consecutive_failures;
  bool stale;
};

struct home_panel_mijia_command_snapshot_s
{
  enum home_panel_mijia_command_state_e state;
  uint32_t revision;
  int code;
  bool target;
  char device_name[48];
  char message[96];
};

struct home_panel_family_model_s;

int home_panel_mijia_initialize(void);
int home_panel_mijia_request_login(void);
void home_panel_mijia_get_snapshot(
  struct home_panel_mijia_snapshot_s *snapshot);
void home_panel_mijia_get_family_snapshot(
  struct home_panel_mijia_family_snapshot_s *snapshot);
int home_panel_mijia_get_family_update(
  uint32_t previous_revision,
  struct home_panel_mijia_family_snapshot_s *snapshot,
  struct home_panel_family_model_s *model);
int home_panel_mijia_get_family_model(
  uint32_t revision, struct home_panel_family_model_s *model);
int home_panel_mijia_request_bool_property(const char *did,
                                            const char *device_name,
                                            const char *property_name,
                                            uint16_t siid,
                                            uint16_t piid,
                                            bool value);
int home_panel_mijia_request_number_property(const char *did,
                                              const char *device_name,
                                              const char *property_name,
                                              uint16_t siid,
                                              uint16_t piid,
                                              int value);
int home_panel_mijia_request_scene(const char *scene_id,
                                   const char *scene_name);
int home_panel_mijia_request_agent_analysis(
  const struct home_panel_agent_request_s *request);
int home_panel_mijia_request_agent_learning(
  const struct home_panel_agent_learning_request_s *request);
void home_panel_mijia_get_agent_snapshot(
  struct home_panel_agent_snapshot_s *snapshot);
void home_panel_mijia_get_command_snapshot(
  struct home_panel_mijia_command_snapshot_s *snapshot);
int home_panel_mijia_copy_qr(uint32_t revision, void *buffer,
                             size_t capacity, size_t *size);
const char *home_panel_mijia_state_name(
  enum home_panel_mijia_state_e state);

#endif
