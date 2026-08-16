/****************************************************************************
 * D13x home panel - Proactive intelligence controller
 *
 * Business coordination layer for proactive intelligence.
 * Separates cloud analysis, automation execution, MIoT target resolution,
 * command tracking, profile persistence, and safety checks from UI.
 *
 * Communicates with page_proactive.c via view-model + callbacks.
 * Does NOT touch the learning algorithm (home_panel_proactive.c).
 ****************************************************************************/

#ifndef __HOME_PANEL_PROACTIVE_CONTROLLER_H
#define __HOME_PANEL_PROACTIVE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "home_panel_mijia_model.h"
#include "home_panel_mijia_client.h"
#include "home_panel_proactive.h"

/* Maximum cloud automation records */

#define PROACTIVE_CTRL_CLOUD_MAX  4

/* Cloud automation record (matches main.c original) */

struct proactive_ctrl_cloud_action_s
{
  uint32_t device_hash;
  uint16_t siid;
  uint16_t piid;
  int32_t value;
  uint8_t value_is_boolean;
  uint8_t reserved[3];
};

struct proactive_ctrl_cloud_record_s
{
  uint32_t routine_id;
  uint32_t trigger_device_hash;
  uint32_t last_executed_day;
  int32_t trigger_value;
  uint16_t trigger_siid;
  uint16_t trigger_piid;
  uint16_t minute_of_day;
  uint16_t window_minutes;
  uint16_t delay_seconds;
  uint8_t trigger_kind;
  uint8_t trigger_value_is_boolean;
  uint8_t days_mask;
  uint8_t action_count;
  struct proactive_ctrl_cloud_action_s actions[HOME_PANEL_CLOUD_MAX_ACTIONS];
};

/* Automation list entry for UI rendering */

struct proactive_ctrl_automation_entry_s
{
  uint32_t routine_id;
  bool cloud;
  bool event_triggered;
  uint16_t mean_minute_of_day;
  uint16_t mean_delay_seconds;
  int action_value;
  enum home_proactive_event_kind_e action_kind;
  uint32_t action_device_hash;
  uint32_t trigger_device_hash;
  unsigned int action_count;
};

/* View-model: controller fills this, UI reads it */

struct proactive_viewmodel_s
{
  /* Core snapshots */

  struct home_proactive_snapshot_s proactive;
  struct home_panel_agent_snapshot_s agent;
  struct home_panel_cloud_proposal_s cloud;

  /* Computed presentation state */

  bool agent_matches;
  bool cloud_blocks;
  bool actionable;
  bool generic;               /* true if candidate != SLEEP */
  bool target_available;

  /* Resolved action target (when generic && target_available) */

  char action_device_name[48];
  char action_control_name[24];
  int action_current_value;
  bool action_already_satisfied;

  /* Automation list */

  struct proactive_ctrl_automation_entry_s
    automations[HOME_PROACTIVE_MAX_AUTOMATIONS + PROACTIVE_CTRL_CLOUD_MAX];
  unsigned int automation_count;
  uint32_t automation_signature;

  /* UI callback results (set by controller after processing feedback) */

  char feedback_message[160];
};

/****************************************************************************
 * Controller lifecycle
 ****************************************************************************/

/* Initialize controller. Call once at startup after proactive_init. */

void proactive_ctrl_initialize(void);

/* Update proactive context from current model state.
 * Call periodically (every ~1s) from main loop.
 */

void proactive_ctrl_update_context(void);

/* Load persisted profile from board storage.
 * Call once at startup.
 */

void proactive_ctrl_load_profile(void);

/* Set the family model pointer for controller access.
 * Call from main.c when the model changes.
 */

void proactive_ctrl_set_family_model(
  const struct home_panel_family_model_s *model);

/* Set network online state for controller access.
 * Call from main.c when network state changes.
 */

void proactive_ctrl_set_network_online(bool online);

/****************************************************************************
 * View-model interface
 ****************************************************************************/

/* Fill the view-model with current state.
 * UI calls this before rendering.
 */

void proactive_ctrl_get_viewmodel(struct proactive_viewmodel_s *vm);

/****************************************************************************
 * UI → Controller callbacks
 ****************************************************************************/

/* Process user feedback (accept, ignore, less often, enable automation).
 * May send MIoT commands, update profile, schedule persist.
 * Writes feedback_message to vm if provided.
 */

void proactive_ctrl_handle_feedback(enum home_proactive_feedback_e feedback,
                                    bool confirmed,
                                    struct proactive_viewmodel_s *vm);

/* Delete an automation (local or cloud).
 * Returns 0 on success.
 */

int proactive_ctrl_delete_automation(uint32_t routine_id, bool cloud);

/* Request cloud analysis/learning/sync. Call from main loop periodically. */

void proactive_ctrl_request_cloud_analysis(
  enum home_panel_mijia_state_e mijia_state);

void proactive_ctrl_request_cloud_learning(
  enum home_panel_mijia_state_e mijia_state);

void proactive_ctrl_request_board_sync(
  enum home_panel_mijia_state_e mijia_state);

/* Process cloud automations and pending commands.
 * Call from main loop every ~50ms.
 */

void proactive_ctrl_process_tick(void);

/* Process proactive automation execution.
 * Call from main loop every ~50ms.
 */

void proactive_ctrl_process_automation(void);

/* Check if persist retry is needed.
 * Returns true if schedule_proactive_persist should be called.
 */

bool proactive_ctrl_persist_retry_ready(void);

/* Schedule profile persistence to board storage.
 * Non-blocking: spawns a worker thread.
 */

void proactive_ctrl_schedule_persist(void);

/* Observe model changes and feed events to proactive engine.
 * Call when family model is refreshed.
 */

void proactive_ctrl_observe_model_changes(
  const struct home_panel_family_model_s *previous,
  const struct home_panel_family_model_s *next);

/* Replay / reset wrappers (UI calls these instead of engine directly) */

void proactive_ctrl_start_replay(uint32_t now_ms);
void proactive_ctrl_stop_replay(void);
void proactive_ctrl_reset(void);

#endif
