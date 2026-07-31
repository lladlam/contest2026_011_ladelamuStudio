/****************************************************************************
 * D13x Hengshan-Pi 1024x600 home control panel
 ****************************************************************************/

#include <nuttx/config.h>

#include <arch/board/board.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <nuttx/arch.h>
#include <nuttx/net/dns.h>
#include <nuttx/net/icmp.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <lvgl/lvgl.h>
#include <netutils/netlib.h>
#include <netutils/ntpclient.h>

#include "home_panel_mijia_client.h"
#include "home_panel_mijia_model.h"
#include "home_panel_font.h"
#include "home_panel_proactive.h"

LV_FONT_DECLARE(home_panel_digits_28);

#define PANEL_WIDTH       1024
#define PANEL_HEIGHT      600
#define TOPBAR_HEIGHT     72
#define NAV_WIDTH         154
#define ROOM_NAV_WIDTH    190
#define ROOM_CARD_WIDTH   292
#define ROOM_CARD_HEIGHT  216
#define ROOM_CARD_GAP     14
#define HOME_PAGE_COUNT   5
#define HOME_CARD_COUNT   3

#define COLOR_BG          0x101214
#define COLOR_NAV         0x151719
#define COLOR_SURFACE     0x1c1f21
#define COLOR_SURFACE_2   0x282d30
#define COLOR_BORDER      0x353b3e
#define COLOR_TEXT        0xf5f7f8
#define COLOR_MUTED       0x9ba3a7
#define COLOR_ORANGE      0xf3a447
#define COLOR_BLUE        0x63a7ff
#define COLOR_GREEN       0x55c996
#define COLOR_NAV_ACTIVE  0x213139

#define NETWORK_INTERFACE       "eth0"
#define NETWORK_PROBE_INTERVAL  60
#define NETWORK_THREAD_STACK    8192
#define NETWORK_THREAD_PRIORITY 50
#define NETWORK_PING_POLL_US    20000
#define NETWORK_PING_POLLS      75
#define NETWORK_PING_DATA_SIZE  16
#define NETWORK_DNS_BUFFER_SIZE 512
#define NETWORK_LINK_GRACE_SEC  12
#define UI_LOOP_MAX_DELAY_MS    5
#define MIJIA_UI_POLL_MS        100
#define PROACTIVE_TICK_MS       50
#define PROACTIVE_UI_POLL_MS    100
#define PROACTIVE_CLOUD_POLL_MS 500
#define UI_THREAD_PRIORITY      105
#define UI_SLOW_LOG_MS          24
#define UI_RADIUS               6
#define UI_CONTROL_RADIUS       16
#define TIME_VALID_EPOCH        1735689600
#define TIME_UPDATE_INTERVAL_MS 1000
#define NTP_RETRY_INTERVAL_MS   30000
#define PROACTIVE_PROFILE_SIZE  768
#define PROACTIVE_THREAD_STACK  8192
#define PROACTIVE_PERSIST_RETRY_MS 1000
#define PROACTIVE_PAGE          3

enum network_state_e
{
  NETWORK_INITIALIZING = 0,
  NETWORK_DISCONNECTED,
  NETWORK_CHECKING,
  NETWORK_NO_INTERNET,
  NETWORK_ONLINE
};

static lv_obj_t *g_status_label;
static lv_obj_t *g_content;
static lv_obj_t *g_page_host;
static lv_obj_t *g_pages[HOME_PAGE_COUNT];
static lv_obj_t *g_page_status_labels[HOME_PAGE_COUNT];
static lv_obj_t *g_page_settings_network_labels[HOME_PAGE_COUNT];
static lv_obj_t *g_page_settings_probe_labels[HOME_PAGE_COUNT];
static lv_obj_t *g_page_settings_account_labels[HOME_PAGE_COUNT];
static lv_obj_t *g_page_home_summary_labels[HOME_PAGE_COUNT];
static lv_obj_t *g_nav_buttons[HOME_PAGE_COUNT];
static lv_obj_t *g_nav_icons[HOME_PAGE_COUNT];
static lv_obj_t *g_nav_labels[HOME_PAGE_COUNT];
static lv_obj_t *g_nav_indicators[HOME_PAGE_COUNT];
static uint32_t g_page_model_revisions[HOME_PAGE_COUNT];
static lv_obj_t *g_network_label;
static lv_obj_t *g_settings_network_label;
static lv_obj_t *g_settings_probe_label;
static lv_obj_t *g_settings_account_label;
static lv_obj_t *g_login_top_label;
static lv_obj_t *g_login_shade;
static lv_obj_t *g_login_qr;
static lv_obj_t *g_login_message;
static lv_obj_t *g_login_action_label;
static lv_obj_t *g_home_summary_label;
static lv_obj_t *g_clock_label;
static lv_obj_t *g_date_label;
static lv_image_dsc_t g_login_qr_image;
static uint8_t *g_login_qr_data;
static uint32_t g_login_qr_revision;
static struct home_panel_family_model_s g_family_model;
static struct home_panel_family_model_s g_family_update_model;
static bool g_family_model_valid;
static bool g_model_refresh_pending;
static uint32_t g_family_ui_revision;
static unsigned int g_current_page;
static unsigned int g_selected_room;
static lv_obj_t *g_room_buttons[HOME_PANEL_MAX_ROOMS];
static lv_obj_t *g_room_title_label;
static lv_obj_t *g_room_summary_label;
static lv_obj_t *g_room_device_host;
static lv_obj_t *g_room_device_hosts[HOME_PANEL_MAX_ROOMS];
static unsigned int g_room_visible_counts[HOME_PANEL_MAX_ROOMS];
static unsigned int g_room_highlighted = HOME_PANEL_MAX_ROOMS;
static lv_obj_t *g_room_detail_host;
static unsigned int g_selected_device = HOME_PANEL_MAX_DEVICES;
static lv_obj_t *g_detail_subtitle_label;
static lv_obj_t *g_detail_temperature_label;
static lv_obj_t *g_detail_humidity_label;
static lv_obj_t *g_detail_battery_label;
static lv_obj_t *g_proactive_mode_label;
static lv_obj_t *g_proactive_history_label;
static lv_obj_t *g_proactive_time_label;
static lv_obj_t *g_proactive_confidence_label;
static lv_obj_t *g_proactive_progress_label;
static lv_obj_t *g_proactive_suggestion_title;
static lv_obj_t *g_proactive_reason_label;
static lv_obj_t *g_proactive_action_label;
static lv_obj_t *g_proactive_feedback_label;
static lv_obj_t *g_proactive_accept_button;
static lv_obj_t *g_proactive_automation_button;
static lv_obj_t *g_proactive_ignore_button;
static lv_obj_t *g_proactive_less_button;
static lv_obj_t *g_proactive_return_button;
static lv_obj_t *g_proactive_reset_button;
static bool g_proactive_persist_busy;
static bool g_proactive_persist_pending;
static uint32_t g_proactive_persist_retry_at;
static uint32_t g_displayed_proactive_revision;
static uint32_t g_displayed_agent_revision;
static uint32_t g_requested_proactive_key;

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
};

static struct proactive_command_tracker_s g_proactive_command;

struct home_panel_device_binding_s
{
  const struct home_panel_device_s *device;
  lv_obj_t *button;
  lv_obj_t *value_label;
  lv_obj_t *state_label;
};

struct home_panel_control_binding_s
{
  const struct home_panel_device_s *device;
  const struct home_panel_control_s *property;
  lv_obj_t *control;
  lv_obj_t *value_label;
};

struct home_panel_scene_binding_s
{
  const struct home_panel_scene_s *scene;
};

static struct home_panel_device_binding_s
  g_device_bindings[HOME_PAGE_COUNT][HOME_PANEL_MAX_DEVICES +
                                     HOME_CARD_COUNT];
static unsigned int g_device_binding_counts[HOME_PAGE_COUNT];
static struct home_panel_control_binding_s
  g_control_bindings[HOME_PANEL_MAX_CONTROLS];
static unsigned int g_control_binding_count;
static struct home_panel_scene_binding_s
  g_scene_bindings[HOME_PAGE_COUNT][HOME_PANEL_MAX_SCENES];
static volatile enum network_state_e g_network_state =
  NETWORK_INITIALIZING;
static volatile bool g_network_refresh_requested = true;
static uint16_t g_network_ping_id;
static uint16_t g_network_dns_id;
static bool g_ntp_started;
static bool g_time_synced;
static bool g_login_autostart_attempted;
static uint32_t g_last_time_update;
static uint32_t g_next_ntp_attempt;
static lv_style_transition_dsc_t g_fast_transition;
static const lv_style_prop_t g_fast_transition_props[] = {0};
static bool g_fast_transition_initialized;
static void nav_clicked(lv_event_t *event);
static void show_page(unsigned int page);
static void delete_page(unsigned int page);
static void apply_mijia_snapshot(
  const struct home_panel_mijia_snapshot_s *snapshot);
static void show_room_device_detail(unsigned int device_index);
static void render_room_devices(unsigned int room_index);
static void update_proactive_context(void);
static void update_proactive_widgets(void);
static void schedule_proactive_persist(void);
static uint32_t proactive_hash_device_id(const char *did);
static const struct home_panel_control_s *proactive_find_control(
  const struct home_panel_device_s *device, uint16_t siid, uint16_t piid);
static bool proactive_safe_action_device(
  const struct home_panel_device_s *device);

static bool set_label_text_if_changed(lv_obj_t *label, const char *text)
{
  const char *current;

  if (label == NULL || text == NULL)
    {
      return false;
    }

  current = lv_label_get_text(label);
  if (current == NULL || strcmp(current, text) != 0)
    {
      lv_label_set_text(label, text);
      return true;
    }

  return false;
}

static void configure_fast_button(lv_obj_t *button)
{
  /* The default LVGL button transition animates color/filter/geometry for
   * roughly 80 ms.  On a software-rendered 1024x600 framebuffer that turns
   * one tap into several expensive frames.  Local style properties override
   * any theme transition while preserving immediate pressed feedback.
   */

  if (!g_fast_transition_initialized)
    {
      lv_style_transition_dsc_init(&g_fast_transition,
                                   g_fast_transition_props, NULL,
                                   0, 0, NULL);
      g_fast_transition_initialized = true;
    }

  lv_obj_set_style_transition(button, &g_fast_transition, 0);
  lv_obj_set_style_transition(button, &g_fast_transition,
                              LV_STATE_PRESSED);
  lv_obj_set_style_anim_duration(button, 0, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_set_style_transform_width(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(button, 0, LV_STATE_PRESSED);
}

static void style_panel(lv_obj_t *panel)
{
  lv_obj_set_style_radius(panel, UI_RADIUS, 0);
  lv_obj_set_style_shadow_width(panel, 0, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_BORDER), 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_SURFACE), 0);
}

static void style_toggle(lv_obj_t *toggle, bool checked)
{
  lv_obj_set_style_radius(toggle, UI_CONTROL_RADIUS, 0);
  lv_obj_set_style_shadow_width(toggle, 0, 0);
  lv_obj_set_style_border_width(toggle, 1, 0);
  lv_obj_set_style_border_color(
    toggle, lv_color_hex(checked ? COLOR_GREEN : COLOR_BORDER), 0);
  lv_obj_set_style_bg_color(
    toggle, lv_color_hex(checked ? COLOR_GREEN : COLOR_SURFACE_2), 0);
  lv_obj_set_style_bg_color(toggle, lv_color_hex(COLOR_BLUE),
                            LV_STATE_PRESSED);
}

static void update_time_ui(bool force)
{
  static const char *weekdays[] =
  {
    "星期日", "星期一", "星期二", "星期三",
    "星期四", "星期五", "星期六"
  };
  struct tm local_time;
  char clock_text[16];
  char date_text[48];
  uint32_t now_ms = lv_tick_get();
  time_t now;
  int ret;

  if (!force && lv_tick_elaps(g_last_time_update) <
      TIME_UPDATE_INTERVAL_MS)
    {
      return;
    }

  g_last_time_update = now_ms;
  if (g_network_state == NETWORK_ONLINE && !g_ntp_started &&
      (g_next_ntp_attempt == 0 ||
       (int32_t)(now_ms - g_next_ntp_attempt) >= 0))
    {
      ret = ntpc_start();
      if (ret >= 0 || ret == -EALREADY)
        {
          g_ntp_started = true;
          syslog(LOG_INFO, "[HOME][TIME] ntp client started pid=%d\n",
                 ret);
        }
      else
        {
          g_next_ntp_attempt = now_ms + NTP_RETRY_INTERVAL_MS;
          syslog(LOG_WARNING, "[HOME][TIME] ntp start failed ret=%d\n",
                 ret);
        }
    }

  now = time(NULL);
  if (now < TIME_VALID_EPOCH)
    {
      if (g_clock_label != NULL)
        {
          set_label_text_if_changed(g_clock_label, "--:--");
        }
      if (g_date_label != NULL)
        {
          set_label_text_if_changed(g_date_label, "等待网络校时");
        }
      return;
    }

  if (!g_time_synced)
    {
      g_time_synced = true;
      syslog(LOG_INFO, "[HOME][TIME] synchronized epoch=%ld timezone=UTC+8\n",
             (long)now);
    }

  now += 8 * 60 * 60;
  gmtime_r(&now, &local_time);
  snprintf(clock_text, sizeof(clock_text), "%02d:%02d",
           local_time.tm_hour, local_time.tm_min);
  snprintf(date_text, sizeof(date_text), "%d月%d日  %s",
           local_time.tm_mon + 1, local_time.tm_mday,
           weekdays[local_time.tm_wday]);
  if (g_clock_label != NULL)
    {
      set_label_text_if_changed(g_clock_label, clock_text);
    }
  if (g_date_label != NULL)
    {
      set_label_text_if_changed(g_date_label, date_text);
    }
}

static const char *network_state_name(enum network_state_e state)
{
  switch (state)
    {
      case NETWORK_INITIALIZING:
        return "initializing";
      case NETWORK_CHECKING:
        return "checking";
      case NETWORK_NO_INTERNET:
        return "no-internet";
      case NETWORK_ONLINE:
        return "online";
      case NETWORK_DISCONNECTED:
        return "cable-disconnected";
      default:
        return "unknown";
    }
}

static void network_set_state(enum network_state_e state)
{
  if (g_network_state != state)
    {
      g_network_state = state;
      syslog(LOG_INFO, "[HOME][NET] state=%s\n",
             network_state_name(state));
    }
}

enum network_link_state_e
{
  NETWORK_LINK_INITIALIZING = 0,
  NETWORK_LINK_DOWN,
  NETWORK_LINK_UP
};

static enum network_link_state_e network_get_link_state(uint8_t *ifflags)
{
  uint8_t flags = 0;

  if (netlib_getifstatus(NETWORK_INTERFACE, &flags) < 0)
    {
      *ifflags = 0;
      return NETWORK_LINK_INITIALIZING;
    }

  *ifflags = flags;
  if ((flags & IFF_RUNNING) != 0)
    {
      return NETWORK_LINK_UP;
    }

  return (flags & IFF_UP) != 0 ? NETWORK_LINK_DOWN :
                                 NETWORK_LINK_INITIALIZING;
}

static bool network_has_carrier(void)
{
  uint8_t flags;

  return network_get_link_state(&flags) == NETWORK_LINK_UP;
}

static bool network_get_ipv4_address(struct in_addr *address)
{
  return netlib_get_ipv4addr(NETWORK_INTERFACE, address) >= 0 &&
         address->s_addr != INADDR_ANY;
}

struct network_ping_packet_s
{
  struct icmp_hdr_s header;
  uint8_t data[NETWORK_PING_DATA_SIZE];
};

static uint16_t network_ping_checksum(const void *buffer, size_t length)
{
  const uint16_t *word = buffer;
  uint32_t sum = 0;

  while (length > 1)
    {
      sum += *word++;
      length -= 2;
    }

  if (length != 0)
    {
      sum += *(const uint8_t *)word;
    }

  while ((sum >> 16) != 0)
    {
      sum = (sum & 0xffff) + (sum >> 16);
    }

  return (uint16_t)~sum;
}

struct network_dns_context_s
{
  struct sockaddr_in server;
  bool found;
};

static int network_dns_server_callback(void *arg, struct sockaddr *address,
                                       socklen_t address_length)
{
  struct network_dns_context_s *context = arg;

  (void)address_length;
  if (address->sa_family == AF_INET)
    {
      memcpy(&context->server, address, sizeof(context->server));
      context->found = true;
      return 1;
    }

  return 0;
}

static bool network_get_dns_server(struct sockaddr_in *server)
{
  struct network_dns_context_s context;

  memset(&context, 0, sizeof(context));
  dns_foreach_nameserver(network_dns_server_callback, &context);
  if (context.found)
    {
      *server = context.server;
    }
  else
    {
      memset(server, 0, sizeof(*server));
      server->sin_family = AF_INET;
      if (netlib_get_dripv4addr(NETWORK_INTERFACE,
                                &server->sin_addr) < 0 ||
          server->sin_addr.s_addr == INADDR_ANY)
        {
          return false;
        }
    }

  if (server->sin_port == 0)
    {
      server->sin_port = htons(DNS_DEFAULT_PORT);
    }

  return true;
}

static uint16_t network_dns_read_u16(const uint8_t *data)
{
  uint16_t value;

  memcpy(&value, data, sizeof(value));
  return ntohs(value);
}

static void network_dns_write_u16(uint8_t *data, uint16_t value)
{
  value = htons(value);
  memcpy(data, &value, sizeof(value));
}

static bool network_dns_skip_name(const uint8_t *message, size_t length,
                                  size_t *offset)
{
  while (*offset < length)
    {
      uint8_t label_length = message[(*offset)++];

      if (label_length == 0)
        {
          return true;
        }

      if ((label_length & 0xc0) == 0xc0)
        {
          if (*offset >= length)
            {
              return false;
            }

          (*offset)++;
          return true;
        }

      if (label_length > 63 || *offset + label_length > length)
        {
          return false;
        }

      *offset += label_length;
    }

  return false;
}

static bool network_resolve_host(const char *hostname,
                                 struct in_addr *resolved_address)
{
  struct sockaddr_in dns_server;
  uint8_t query[NETWORK_DNS_BUFFER_SIZE];
  uint8_t response[NETWORK_DNS_BUFFER_SIZE];
  struct dns_header_s *header = (struct dns_header_s *)query;
  const char *label = hostname;
  uint16_t query_id = ++g_network_dns_id;
  size_t query_length = sizeof(struct dns_header_s);
  unsigned int poll_count;
  int sockfd;
  int ret;

  if (!network_get_dns_server(&dns_server))
    {
      syslog(LOG_WARNING, "[HOME][NET] host=%s dns-server=missing\n",
             hostname);
      return false;
    }

  memset(query, 0, sizeof(query));
  header->id = htons(query_id);
  header->flags1 = DNS_FLAG1_RD;
  header->numquestions = htons(1);

  while (*label != '\0')
    {
      const char *dot = strchr(label, '.');
      size_t label_length = dot == NULL ? strlen(label) :
                                          (size_t)(dot - label);

      if (label_length == 0 || label_length > 63 ||
          query_length + label_length + 6 > sizeof(query))
        {
          return false;
        }

      query[query_length++] = (uint8_t)label_length;
      memcpy(&query[query_length], label, label_length);
      query_length += label_length;
      if (dot == NULL)
        {
          break;
        }

      label = dot + 1;
    }

  query[query_length++] = 0;
  network_dns_write_u16(&query[query_length], DNS_RECTYPE_A);
  query_length += 2;
  network_dns_write_u16(&query[query_length], DNS_CLASS_IN);
  query_length += 2;

  sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockfd < 0)
    {
      syslog(LOG_WARNING, "[HOME][NET] host=%s dns-socket=%d\n",
             hostname, errno);
      return false;
    }

  if (fcntl(sockfd, F_SETFL, O_NONBLOCK) < 0)
    {
      syslog(LOG_WARNING, "[HOME][NET] host=%s dns-nonblock=%d\n",
             hostname, errno);
      close(sockfd);
      return false;
    }

  ret = sendto(sockfd, query, query_length, MSG_DONTWAIT,
               (const struct sockaddr *)&dns_server,
               sizeof(dns_server));
  if (ret != (int)query_length)
    {
      syslog(LOG_WARNING, "[HOME][NET] host=%s dns-send=%d errno=%d\n",
             hostname, ret, errno);
      close(sockfd);
      return false;
    }

  for (poll_count = 0; poll_count < NETWORK_PING_POLLS; poll_count++)
    {
      ssize_t received;

      received = recvfrom(sockfd, response, sizeof(response),
                          MSG_DONTWAIT, NULL, NULL);
      if (received >= (ssize_t)sizeof(struct dns_header_s))
        {
          struct dns_header_s *response_header =
            (struct dns_header_s *)response;
          size_t offset = sizeof(struct dns_header_s);
          unsigned int index;

          if (ntohs(response_header->id) != query_id ||
              (response_header->flags1 & DNS_FLAG1_RESPONSE) == 0 ||
              (response_header->flags2 & DNS_FLAG2_ERR_MASK) != 0)
            {
              continue;
            }

          for (index = 0;
               index < ntohs(response_header->numquestions); index++)
            {
              if (!network_dns_skip_name(response, received, &offset) ||
                  offset + 4 > (size_t)received)
                {
                  break;
                }

              offset += 4;
            }

          for (index = 0; index < ntohs(response_header->numanswers);
               index++)
            {
              uint16_t record_type;
              uint16_t record_class;
              uint16_t record_length;

              if (!network_dns_skip_name(response, received, &offset) ||
                  offset + 10 > (size_t)received)
                {
                  break;
                }

              record_type = network_dns_read_u16(&response[offset]);
              record_class = network_dns_read_u16(&response[offset + 2]);
              record_length = network_dns_read_u16(&response[offset + 8]);
              offset += 10;
              if (offset + record_length > (size_t)received)
                {
                  break;
                }

              if (record_type == DNS_RECTYPE_A &&
                  record_class == DNS_CLASS_IN && record_length == 4)
                {
                  memcpy(resolved_address, &response[offset], 4);
                  close(sockfd);
                  return true;
                }

              offset += record_length;
            }
        }
      else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
               errno != EINTR)
        {
          syslog(LOG_WARNING,
                 "[HOME][NET] host=%s dns-recv=%d\n", hostname, errno);
          break;
        }

      usleep(NETWORK_PING_POLL_US);
    }

  syslog(LOG_WARNING, "[HOME][NET] host=%s dns-timeout\n", hostname);
  close(sockfd);
  return false;
}

static int network_ping_host(const char *hostname)
{
  struct network_ping_packet_s packet;
  struct sockaddr_in destination;
  uint8_t receive_buffer[64];
  uint16_t ping_id;
  unsigned int poll_count;
  int sockfd;
  int ret;

  memset(&destination, 0, sizeof(destination));
  destination.sin_family = AF_INET;
  if (!network_resolve_host(hostname, &destination.sin_addr))
    {
      return 0;
    }

  sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sockfd < 0)
    {
      syslog(LOG_WARNING, "[HOME][NET] host=%s socket=%d\n",
             hostname, errno);
      return 0;
    }

  if (fcntl(sockfd, F_SETFL, O_NONBLOCK) < 0)
    {
      syslog(LOG_WARNING, "[HOME][NET] host=%s nonblock=%d\n",
             hostname, errno);
      close(sockfd);
      return 0;
    }

  memset(&packet, 0, sizeof(packet));
  ping_id = ++g_network_ping_id;
  packet.header.type = ICMP_ECHO_REQUEST;
  packet.header.id = htons(ping_id);
  packet.header.seqno = htons(1);
  memset(packet.data, 0x5a, sizeof(packet.data));
  packet.header.icmpchksum = network_ping_checksum(&packet,
                                                   sizeof(packet));

  ret = sendto(sockfd, &packet, sizeof(packet), MSG_DONTWAIT,
               (const struct sockaddr *)&destination,
               sizeof(destination));
  if (ret != (int)sizeof(packet))
    {
      syslog(LOG_WARNING, "[HOME][NET] host=%s send=%d errno=%d\n",
             hostname, ret, errno);
      close(sockfd);
      return 0;
    }

  for (poll_count = 0; poll_count < NETWORK_PING_POLLS; poll_count++)
    {
      ssize_t received;
      size_t offset = 0;
      struct icmp_hdr_s *reply;

      received = recvfrom(sockfd, receive_buffer, sizeof(receive_buffer),
                          MSG_DONTWAIT, NULL, NULL);
      if (received > 0)
        {
          if ((receive_buffer[0] >> 4) == 4)
            {
              offset = (receive_buffer[0] & 0x0f) * 4;
            }

          if ((size_t)received >= offset + sizeof(struct icmp_hdr_s))
            {
              reply = (struct icmp_hdr_s *)(receive_buffer + offset);
              if (reply->type == ICMP_ECHO_REPLY &&
                  ntohs(reply->id) == ping_id &&
                  ntohs(reply->seqno) == 1)
                {
                  close(sockfd);
                  return 1;
                }
            }
        }
      else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
          syslog(LOG_WARNING, "[HOME][NET] host=%s recv=%d\n",
                 hostname, errno);
          break;
        }

      usleep(NETWORK_PING_POLL_US);
    }

  close(sockfd);
  return 0;
}

static void *network_worker(void *arg)
{
  unsigned int elapsed = NETWORK_PROBE_INTERVAL;
  unsigned int initial_down_seconds = 0;
  bool was_connected = false;
  bool link_seen = false;
  enum network_link_state_e last_link = NETWORK_LINK_INITIALIZING;
  uint8_t last_flags = UINT8_MAX;

  (void)arg;

  for (;;)
    {
      uint8_t flags;
      enum network_link_state_e link = network_get_link_state(&flags);
      bool connected = link == NETWORK_LINK_UP;
      bool refresh = g_network_refresh_requested;
      struct in_addr address;

      if (link != last_link || flags != last_flags)
        {
          const char *link_name = link == NETWORK_LINK_UP ? "up" :
                                  link == NETWORK_LINK_DOWN ? "down" :
                                  "initializing";

          syslog(LOG_INFO, "[HOME][NET] carrier=%s flags=%02x\n",
                 link_name, flags);
          last_link = link;
          last_flags = flags;
        }

      g_network_refresh_requested = false;
      if (link == NETWORK_LINK_INITIALIZING)
        {
          network_set_state(NETWORK_INITIALIZING);
          was_connected = false;
          elapsed = NETWORK_PROBE_INTERVAL;
          initial_down_seconds = 0;
        }
      else if (!connected)
        {
          if (!link_seen && initial_down_seconds < NETWORK_LINK_GRACE_SEC)
            {
              network_set_state(NETWORK_INITIALIZING);
              initial_down_seconds++;
            }
          else
            {
              network_set_state(NETWORK_DISCONNECTED);
            }
          was_connected = false;
          elapsed = NETWORK_PROBE_INTERVAL;
        }
      else if (!network_get_ipv4_address(&address))
        {
          /* DHCP runs independently. Keep checking without reporting a
           * false Internet failure while an address is being acquired.
           */

          network_set_state(NETWORK_CHECKING);
          link_seen = true;
          initial_down_seconds = 0;
          was_connected = false;
        }
      else if (!was_connected || refresh ||
               elapsed >= NETWORK_PROBE_INTERVAL)
        {
          int replies;
          char address_text[INET_ADDRSTRLEN];

          /* Keep the last definitive result visible during an automatic
           * background probe.  Only initial and user-requested probes show
           * the transient checking state.
           */

          if (!was_connected || refresh)
            {
              network_set_state(NETWORK_CHECKING);
            }
          link_seen = true;
          initial_down_seconds = 0;
          inet_ntop(AF_INET, &address, address_text, sizeof(address_text));
          replies = network_ping_host("mi.com");
          if (replies <= 0 && network_has_carrier())
            {
              replies = network_ping_host("xiaomi.cn");
            }

          if (!network_has_carrier())
            {
              network_set_state(NETWORK_DISCONNECTED);
            }
          else
            {
              network_set_state(replies > 0 ? NETWORK_ONLINE :
                                              NETWORK_NO_INTERNET);
              syslog(LOG_INFO,
                     "[HOME][NET] health ipv4=%s result=%s\n",
                     address_text, replies > 0 ? "online" : "unreachable");
            }

          was_connected = true;
          elapsed = 0;
        }
      else
        {
          elapsed++;
        }

      sleep(1);
    }

  return NULL;
}

static int start_network_monitor(void)
{
  pthread_attr_t attr;
  struct sched_param param;
  pthread_t thread;
  int ret;

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      return ret;
    }

  pthread_attr_setstacksize(&attr, NETWORK_THREAD_STACK);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  memset(&param, 0, sizeof(param));
  param.sched_priority = NETWORK_THREAD_PRIORITY;
  pthread_attr_setschedparam(&attr, &param);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  ret = pthread_create(&thread, &attr, network_worker, NULL);
  pthread_attr_destroy(&attr);
  return ret;
}

static void apply_network_state(enum network_state_e state)
{
  const char *top_text;
  const char *settings_text;
  const char *probe_text;
  lv_color_t color;

  if (state == NETWORK_ONLINE)
    {
      top_text = LV_SYMBOL_OK "  已连接互联网";
      settings_text = "已连接互联网";
      probe_text = "mi.com / xiaomi.cn 可达";
      color = lv_color_hex(COLOR_GREEN);
    }
  else if (state == NETWORK_NO_INTERNET)
    {
      top_text = LV_SYMBOL_WARNING "  无互联网连接";
      settings_text = "无互联网连接";
      probe_text = "mi.com / xiaomi.cn 不可达";
      color = lv_color_hex(COLOR_ORANGE);
    }
  else if (state == NETWORK_CHECKING)
    {
      top_text = LV_SYMBOL_REFRESH "  正在检测网络";
      settings_text = "网线已连接";
      probe_text = "正在检测互联网";
      color = lv_color_hex(COLOR_BLUE);
    }
  else if (state == NETWORK_INITIALIZING)
    {
      top_text = LV_SYMBOL_REFRESH "  正在初始化网络";
      settings_text = "正在初始化有线网络";
      probe_text = "等待网络接口启动";
      color = lv_color_hex(COLOR_BLUE);
    }
  else
    {
      top_text = LV_SYMBOL_WARNING "  有线网络未连接";
      settings_text = "有线网络未连接";
      probe_text = "等待网线连接";
      color = lv_color_hex(COLOR_ORANGE);
    }

  lv_label_set_text(g_network_label, top_text);
  lv_obj_set_style_text_color(g_network_label, color, 0);

  if (g_settings_network_label != NULL)
    {
      lv_label_set_text(g_settings_network_label, settings_text);
      lv_obj_set_style_text_color(g_settings_network_label, color, 0);
    }

  if (g_settings_probe_label != NULL)
    {
      lv_label_set_text(g_settings_probe_label, probe_text);
      lv_obj_set_style_text_color(g_settings_probe_label, color, 0);
    }
}

static void set_chinese_font(lv_obj_t *obj)
{
  lv_obj_set_style_text_font(obj, home_panel_font_get(), 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            int x, int y, lv_color_t color,
                            const lv_font_t *font)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_font(label, font, 0);
  return label;
}

static void scene_clicked(lv_event_t *event)
{
  struct home_panel_scene_binding_s *binding =
    lv_event_get_user_data(event);
  char message[64];
  int ret;

  if (binding == NULL || binding->scene == NULL)
    {
      return;
    }

  ret = home_panel_mijia_request_scene(binding->scene->id,
                                       binding->scene->name);
  snprintf(message, sizeof(message), ret == 0 ? "正在执行：%s" :
                                                "场景执行失败：%s",
           binding->scene->name);
  lv_label_set_text(g_status_label, message);
  lv_obj_set_style_text_color(g_status_label,
                              lv_color_hex(ret == 0 ? COLOR_BLUE :
                                                        COLOR_ORANGE), 0);
}

static void action_clicked(lv_event_t *event)
{
  const char *action = lv_event_get_user_data(event);
  char message[72];

  snprintf(message, sizeof(message), "%s：操作已生效", action);
  if (g_status_label != NULL)
    {
      lv_label_set_text(g_status_label, message);
      lv_obj_set_style_text_color(g_status_label,
                                  lv_color_hex(COLOR_GREEN), 0);
    }
}

static const char *control_display_name(const char *name)
{
  if (strcmp(name, "on") == 0)
    {
      return "电源";
    }
  if (strcmp(name, "brightness") == 0)
    {
      return "亮度";
    }
  if (strcmp(name, "volume") == 0)
    {
      return "音量";
    }
  if (strcmp(name, "mute") == 0)
    {
      return "静音";
    }
  if (strcmp(name, "microphone-mute") == 0)
    {
      return "麦克风静音";
    }
  if (strcmp(name, "target-temperature") == 0)
    {
      return "目标温度";
    }
  if (strcmp(name, "color-temperature") == 0)
    {
      return "色温";
    }
  if (strcmp(name, "physical-controls-locked") == 0)
    {
      return "童锁";
    }
  if (strcmp(name, "indicator-light-on") == 0)
    {
      return "指示灯";
    }
  if (strcmp(name, "alarm") == 0)
    {
      return "提示音";
    }
  if (strcmp(name, "sleep-mode") == 0)
    {
      return "睡眠模式";
    }
  if (strcmp(name, "no-disturb") == 0)
    {
      return "勿扰模式";
    }
  if (strcmp(name, "countdown-time") == 0)
    {
      return "定时关闭";
    }
  if (strcmp(name, "charging-protection-on") == 0)
    {
      return "充电保护";
    }

  return name;
}

static void format_control_value(const struct home_panel_control_s *property,
                                 int value, char *text, size_t capacity)
{
  if (strcmp(property->name, "brightness") == 0)
    {
      int span = property->maximum - property->minimum;
      int percent = span > 0 ?
        (value - property->minimum) * 100 / span : value;
      snprintf(text, capacity, "%d%%", percent);
    }
  else if (strcmp(property->name, "volume") == 0)
    {
      snprintf(text, capacity, "%d%%", value);
    }
  else if (strcmp(property->name, "target-temperature") == 0)
    {
      snprintf(text, capacity, "%d°C", value);
    }
  else if (strcmp(property->name, "color-temperature") == 0)
    {
      snprintf(text, capacity, "%d K", value);
    }
  else
    {
      snprintf(text, capacity, "%d", value);
    }
}

static void set_command_status(int ret)
{
  if (g_status_label == NULL)
    {
      return;
    }

  lv_label_set_text(g_status_label,
                    ret == 0 ? "指令已发送，等待设备确认" :
                    ret == -EBUSY ? "请等待上一个设备操作完成" :
                                    "设备操作发送失败");
  lv_obj_set_style_text_color(g_status_label,
                              lv_color_hex(ret == 0 ? COLOR_BLUE :
                                                      COLOR_ORANGE), 0);
}

static void device_toggled(lv_event_t *event)
{
  lv_obj_t *button = lv_event_get_target(event);
  struct home_panel_device_binding_s *binding =
    lv_event_get_user_data(event);
  bool requested;
  int ret;

  if (binding == NULL || binding->device == NULL ||
      !binding->device->power_writable || !binding->device->online)
    {
      return;
    }

  requested = lv_obj_has_state(button, LV_STATE_CHECKED);
  ret = home_panel_mijia_request_bool_property(binding->device->did,
                                                binding->device->name,
                                                "on",
                                                binding->device->power_siid,
                                                binding->device->power_piid,
                                                requested);

  if (binding->device->power)
    {
      lv_obj_add_state(button, LV_STATE_CHECKED);
    }
  else
    {
      lv_obj_remove_state(button, LV_STATE_CHECKED);
    }

  if (ret == 0)
    {
      lv_label_set_text(binding->state_label,
                        requested ? "正在开启" : "正在关闭");
      lv_obj_set_style_text_color(binding->state_label,
                                  lv_color_hex(COLOR_BLUE), 0);
      lv_obj_add_state(button, LV_STATE_DISABLED);
      set_command_status(ret);
    }
  else
    {
      set_command_status(ret);
    }
}

static void detail_boolean_changed(lv_event_t *event)
{
  lv_obj_t *button = lv_event_get_target(event);
  struct home_panel_control_binding_s *binding =
    lv_event_get_user_data(event);
  bool requested;
  int ret;

  if (binding == NULL || binding->device == NULL ||
      binding->property == NULL || !binding->device->online)
    {
      return;
    }

  requested = lv_obj_has_state(button, LV_STATE_CHECKED);
  ret = home_panel_mijia_request_bool_property(
    binding->device->did, binding->device->name,
    binding->property->name, binding->property->siid,
    binding->property->piid, requested);

  if (binding->property->boolean_value)
    {
      lv_obj_add_state(button, LV_STATE_CHECKED);
    }
  else
    {
      lv_obj_remove_state(button, LV_STATE_CHECKED);
    }

  if (ret == 0)
    {
      lv_obj_add_state(button, LV_STATE_DISABLED);
    }
  set_command_status(ret);
}

static void detail_number_changed(lv_event_t *event)
{
  struct home_panel_control_binding_s *binding =
    lv_event_get_user_data(event);
  char value[24];

  if (binding == NULL || binding->property == NULL ||
      binding->value_label == NULL)
    {
      return;
    }

  format_control_value(binding->property,
                       lv_slider_get_value(lv_event_get_target(event)),
                       value, sizeof(value));
  set_label_text_if_changed(binding->value_label, value);
}

static void detail_number_released(lv_event_t *event)
{
  struct home_panel_control_binding_s *binding =
    lv_event_get_user_data(event);
  int value;
  int ret;

  if (binding == NULL || binding->device == NULL ||
      binding->property == NULL || !binding->device->online)
    {
      return;
    }

  value = lv_slider_get_value(lv_event_get_target(event));
  if (binding->property->step > 1)
    {
      int offset = value - binding->property->minimum;

      value = binding->property->minimum +
              ((offset + binding->property->step / 2) /
               binding->property->step) * binding->property->step;
      if (value > binding->property->maximum)
        {
          value = binding->property->maximum;
        }
      lv_slider_set_value(lv_event_get_target(event), value, LV_ANIM_OFF);
    }
  ret = home_panel_mijia_request_number_property(
    binding->device->did, binding->device->name,
    binding->property->name, binding->property->siid,
    binding->property->piid, value);
  set_command_status(ret);
}

static void login_close(lv_event_t *event)
{
  lv_obj_t *shade = lv_event_get_user_data(event);

  g_login_shade = NULL;
  g_login_qr = NULL;
  g_login_message = NULL;
  g_login_action_label = NULL;
  lv_obj_delete_async(shade);
}

static bool login_update_qr(
  const struct home_panel_mijia_snapshot_s *snapshot)
{
  lv_image_header_t header;
  lv_area_t area;
  uint8_t *data;
  size_t size;
  unsigned int dark_pixels = 0;
  size_t offset;
  int ret;

  if (snapshot->qr_size == 0)
    {
      return false;
    }

  if (g_login_qr_data != NULL &&
      g_login_qr_revision == snapshot->qr_revision)
    {
      lv_image_set_src(g_login_qr, &g_login_qr_image);
      if (lv_image_get_src(g_login_qr) != &g_login_qr_image)
        {
          syslog(LOG_ERR, "[HOME][UI] qr source rejected on reuse\n");
          return false;
        }

      lv_obj_remove_flag(g_login_qr, LV_OBJ_FLAG_HIDDEN);
      lv_obj_invalidate(g_login_qr);
      return true;
    }

  data = malloc(snapshot->qr_size);
  if (data == NULL)
    {
      return false;
    }

  ret = home_panel_mijia_copy_qr(snapshot->qr_revision, data,
                                 snapshot->qr_size, &size);
  if (ret < 0)
    {
      free(data);
      return false;
    }

  if (g_login_qr_data != NULL)
    {
      lv_image_cache_drop(&g_login_qr_image);
      lv_image_set_src(g_login_qr, NULL);
      free(g_login_qr_data);
    }

  memset(&g_login_qr_image, 0, sizeof(g_login_qr_image));
  g_login_qr_data = data;
  g_login_qr_revision = snapshot->qr_revision;
  g_login_qr_image.header.magic = LV_IMAGE_HEADER_MAGIC;
  g_login_qr_image.header.cf = LV_COLOR_FORMAT_RGB565;
  g_login_qr_image.header.w = 240;
  g_login_qr_image.header.h = 240;
  g_login_qr_image.header.stride = 480;
  g_login_qr_image.data_size = size;
  g_login_qr_image.data = data;

  memset(&header, 0, sizeof(header));
  if (lv_image_decoder_get_info(&g_login_qr_image, &header) !=
      LV_RESULT_OK || header.cf != LV_COLOR_FORMAT_RGB565 ||
      header.w != 240 || header.h != 240 || header.stride != 480)
    {
      syslog(LOG_ERR,
             "[HOME][UI] qr decoder rejected descriptor cf=%u %ux%u stride=%u\n",
             (unsigned int)header.cf, (unsigned int)header.w,
             (unsigned int)header.h, (unsigned int)header.stride);
      free(g_login_qr_data);
      g_login_qr_data = NULL;
      g_login_qr_revision = 0;
      memset(&g_login_qr_image, 0, sizeof(g_login_qr_image));
      return false;
    }

  for (offset = 0; offset + 1 < size; offset += 2)
    {
      if (data[offset] == 0 && data[offset + 1] == 0)
        {
          dark_pixels++;
        }
    }

  lv_image_set_src(g_login_qr, &g_login_qr_image);
  if (lv_image_get_src(g_login_qr) != &g_login_qr_image)
    {
      syslog(LOG_ERR, "[HOME][UI] qr source rejected after decode\n");
      free(g_login_qr_data);
      g_login_qr_data = NULL;
      g_login_qr_revision = 0;
      memset(&g_login_qr_image, 0, sizeof(g_login_qr_image));
      return false;
    }

  lv_obj_remove_flag(g_login_qr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_login_qr);
  lv_obj_invalidate(g_login_qr);
  lv_obj_update_layout(g_login_qr);
  lv_obj_get_coords(g_login_qr, &area);
  syslog(LOG_INFO,
         "[HOME][UI] qr ready RGB565 240x240 bytes=%u dark=%u "
         "area=%d,%d-%d,%d hidden=%u revision=%u\n",
         (unsigned int)size, dark_pixels,
         (int)area.x1, (int)area.y1, (int)area.x2, (int)area.y2,
         lv_obj_has_flag(g_login_qr, LV_OBJ_FLAG_HIDDEN) ? 1 : 0,
         (unsigned int)snapshot->qr_revision);
  return true;
}

static void login_action(lv_event_t *event)
{
  struct home_panel_mijia_snapshot_s snapshot;

  home_panel_mijia_get_snapshot(&snapshot);
  if (snapshot.state == HOME_PANEL_MIJIA_AUTHENTICATED)
    {
      login_close(event);
      return;
    }

  if (g_network_state != NETWORK_ONLINE)
    {
      lv_label_set_text(g_login_message,
                        "请先连接可访问互联网的有线网络");
      return;
    }

  if (home_panel_mijia_request_login() < 0)
    {
      lv_label_set_text(g_login_message, "米家登录服务尚未就绪");
    }
}

static void show_login(lv_event_t *event)
{
  lv_obj_t *screen = lv_screen_active();
  lv_obj_t *shade;
  lv_obj_t *dialog;
  lv_obj_t *close;
  lv_obj_t *action;
  lv_obj_t *label;
  struct home_panel_mijia_snapshot_s snapshot;

  (void)event;

  if (g_login_shade != NULL)
    {
      return;
    }

  shade = lv_obj_create(screen);
  g_login_shade = shade;
  lv_obj_remove_style_all(shade);
  lv_obj_set_size(shade, PANEL_WIDTH, PANEL_HEIGHT);
  lv_obj_set_pos(shade, 0, 0);
  lv_obj_set_style_bg_color(shade, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(shade, LV_OPA_70, 0);

  dialog = lv_obj_create(shade);
  lv_obj_set_size(dialog, 680, 410);
  lv_obj_center(dialog);
  lv_obj_set_style_radius(dialog, UI_RADIUS, 0);
  lv_obj_set_style_border_width(dialog, 1, 0);
  lv_obj_set_style_border_color(dialog, lv_color_hex(COLOR_BORDER), 0);
  lv_obj_set_style_bg_color(dialog, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_pad_all(dialog, 24, 0);
  lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

  label = make_label(dialog, "米家账号登录", 282, 8,
                     lv_color_hex(COLOR_TEXT), home_panel_font_get());
  lv_obj_set_style_text_font(label, home_panel_font_get(), 0);

  g_login_qr = lv_image_create(dialog);
  lv_obj_set_size(g_login_qr, 256, 256);
  lv_image_set_inner_align(g_login_qr, LV_IMAGE_ALIGN_CENTER);
  lv_obj_set_pos(g_login_qr, 18, 62);
  lv_obj_set_style_border_color(g_login_qr, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_border_width(g_login_qr, 8, 0);
  lv_obj_add_flag(g_login_qr, LV_OBJ_FLAG_HIDDEN);

  g_login_message = make_label(dialog, "正在准备米家登录",
                               282, 78, lv_color_hex(COLOR_MUTED),
                               home_panel_font_get());
  lv_obj_set_width(g_login_message, 340);
  lv_label_set_long_mode(g_login_message, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(g_login_message, 12, 0);

  label = make_label(dialog,
                     "请使用米家 App 扫码。账号认证数据只保存在\n"
                     "本地服务端的加密保险箱中。",
                     282, 164, lv_color_hex(COLOR_MUTED),
                     home_panel_font_get());
  lv_obj_set_style_text_line_space(label, 10, 0);

  close = lv_button_create(dialog);
  configure_fast_button(close);
  lv_obj_set_size(close, 44, 44);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_radius(close, UI_RADIUS, 0);
  lv_obj_set_style_bg_color(close, lv_color_hex(COLOR_SURFACE_2), 0);
  lv_obj_set_style_bg_color(close, lv_color_hex(0x343b44),
                            LV_STATE_PRESSED);
  lv_obj_add_event_cb(close, login_close, LV_EVENT_CLICKED, shade);
  label = lv_label_create(close);
  lv_label_set_text(label, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_center(label);

  action = lv_button_create(dialog);
  configure_fast_button(action);
  lv_obj_set_size(action, 144, 50);
  lv_obj_align(action, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_radius(action, UI_RADIUS, 0);
  lv_obj_set_style_bg_color(action, lv_color_hex(COLOR_BLUE), 0);
  lv_obj_set_style_bg_color(action, lv_color_hex(0x3475d6),
                            LV_STATE_PRESSED);
  lv_obj_add_event_cb(action, login_action, LV_EVENT_CLICKED, shade);
  g_login_action_label = lv_label_create(action);
  lv_label_set_text(g_login_action_label, "重新生成");
  set_chinese_font(g_login_action_label);
  lv_obj_center(g_login_action_label);

  home_panel_mijia_get_snapshot(&snapshot);
  if (snapshot.state == HOME_PANEL_MIJIA_AUTHENTICATED)
    {
      apply_mijia_snapshot(&snapshot);
    }
  else if (g_network_state == NETWORK_ONLINE)
    {
      home_panel_mijia_request_login();
    }
  else if (g_network_state != NETWORK_ONLINE)
    {
      lv_label_set_text(g_login_message,
                        "请先连接可访问互联网的有线网络");
    }
}

static void apply_mijia_snapshot(
  const struct home_panel_mijia_snapshot_s *snapshot)
{
  char text[160];

  if (g_login_top_label != NULL)
    {
      set_label_text_if_changed(
        g_login_top_label,
        snapshot->state == HOME_PANEL_MIJIA_AUTHENTICATED ?
        "米家已登录" : "登录米家");
    }

  if (g_settings_account_label != NULL)
    {
      if (snapshot->state == HOME_PANEL_MIJIA_AUTHENTICATED)
        {
          snprintf(text, sizeof(text), "%s · %u/%u 台在线",
                   snapshot->home_name[0] != '\0' ?
                   snapshot->home_name : "米家账号",
                   snapshot->online_count, snapshot->device_count);
          set_label_text_if_changed(g_settings_account_label, text);
          lv_obj_set_style_text_color(g_settings_account_label,
                                      lv_color_hex(COLOR_GREEN), 0);
        }
      else
        {
          set_label_text_if_changed(g_settings_account_label, "未登录");
          lv_obj_set_style_text_color(g_settings_account_label,
                                      lv_color_hex(COLOR_MUTED), 0);
        }
    }

  if (g_home_summary_label != NULL && !g_family_model_valid)
    {
      if (snapshot->state == HOME_PANEL_MIJIA_IDLE)
        {
          set_label_text_if_changed(g_home_summary_label,
                                    "登录米家后同步家庭设备");
        }
      else if (snapshot->state == HOME_PANEL_MIJIA_AUTHENTICATED)
        {
          set_label_text_if_changed(g_home_summary_label,
                                    "正在同步米家设备");
        }
      else
        {
          set_label_text_if_changed(g_home_summary_label,
                                    snapshot->message);
        }
    }

  if (g_login_shade == NULL)
    {
      return;
    }

  lv_label_set_text(g_login_message, snapshot->message);
  if (snapshot->state == HOME_PANEL_MIJIA_WAITING)
    {
      if (!login_update_qr(snapshot))
        {
          lv_obj_add_flag(g_login_qr, LV_OBJ_FLAG_HIDDEN);
          lv_label_set_text(g_login_message, "登录二维码加载失败");
        }

      lv_label_set_text(g_login_action_label, "重新生成");
    }
  else if (snapshot->state == HOME_PANEL_MIJIA_AUTHENTICATED)
    {
      lv_obj_add_flag(g_login_qr, LV_OBJ_FLAG_HIDDEN);
      snprintf(text, sizeof(text),
               "已登录 %s\n共 %u 台设备，%u 台在线",
               snapshot->home_name[0] != '\0' ?
               snapshot->home_name : "米家账号",
               snapshot->device_count, snapshot->online_count);
      lv_label_set_text(g_login_message, text);
      lv_obj_set_style_text_color(g_login_message,
                                  lv_color_hex(COLOR_GREEN), 0);
      lv_label_set_text(g_login_action_label, "完成");
    }
  else
    {
      lv_obj_add_flag(g_login_qr, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(g_login_action_label,
                        snapshot->state == HOME_PANEL_MIJIA_STARTING ?
                        "连接中" : "重新生成");
    }
}

static lv_obj_t *make_nav_button(lv_obj_t *parent, const char *symbol,
                                 const char *text, int y,
                                 unsigned int page)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_t *indicator;
  lv_obj_t *icon;
  lv_obj_t *label;

  configure_fast_button(button);
  lv_obj_set_pos(button, 12, y);
  lv_obj_set_size(button, 130, 52);
  lv_obj_set_style_radius(button, UI_RADIUS, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_NAV), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_SURFACE_2),
                            LV_STATE_PRESSED);
  lv_obj_add_event_cb(button, nav_clicked, LV_EVENT_PRESSED,
                      (void *)(uintptr_t)page);

  indicator = lv_obj_create(button);
  lv_obj_remove_style_all(indicator);
  lv_obj_set_pos(indicator, 0, 10);
  lv_obj_set_size(indicator, 3, 32);
  lv_obj_set_style_radius(indicator, 2, 0);
  lv_obj_set_style_bg_color(indicator, lv_color_hex(COLOR_BLUE), 0);
  lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
  lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);

  icon = make_label(button, symbol, 16, 15,
                    lv_color_hex(COLOR_MUTED),
                    &lv_font_montserrat_16);
  label = make_label(button, text, 48, 13,
                     lv_color_hex(COLOR_MUTED),
                     home_panel_font_get());
  g_nav_icons[page] = icon;
  g_nav_labels[page] = label;
  g_nav_indicators[page] = indicator;
  return button;
}

static void set_nav_selected(unsigned int page, bool selected)
{
  if (page >= HOME_PAGE_COUNT || g_nav_buttons[page] == NULL)
    {
      return;
    }

  lv_obj_set_style_bg_color(
    g_nav_buttons[page],
    lv_color_hex(selected ? COLOR_NAV_ACTIVE : COLOR_NAV), 0);
  lv_obj_set_style_text_color(
    g_nav_icons[page],
    lv_color_hex(selected ? COLOR_BLUE : COLOR_MUTED), 0);
  lv_obj_set_style_text_color(
    g_nav_labels[page],
    lv_color_hex(selected ? COLOR_TEXT : COLOR_MUTED), 0);
  if (selected)
    {
      lv_obj_remove_flag(g_nav_indicators[page], LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_obj_add_flag(g_nav_indicators[page], LV_OBJ_FLAG_HIDDEN);
    }
}

static void nav_clicked(lv_event_t *event)
{
  unsigned int page = (uintptr_t)lv_event_get_user_data(event);

  show_page(page);
}

static lv_obj_t *make_scene_button(lv_obj_t *parent, const char *symbol,
                                   const struct home_panel_scene_s *scene,
                                   unsigned int binding_index, int x,
                                   lv_color_t accent)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_t *indicator;
  lv_obj_t *icon;
  lv_obj_t *label;

  configure_fast_button(button);
  lv_obj_set_pos(button, x, 92);
  lv_obj_set_size(button, 180, 72);
  lv_obj_set_style_radius(button, UI_RADIUS, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_SURFACE_2),
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(COLOR_BORDER), 0);
  g_scene_bindings[g_current_page][binding_index].scene = scene;
  lv_obj_add_event_cb(button, scene_clicked, LV_EVENT_CLICKED,
                      &g_scene_bindings[g_current_page][binding_index]);

  indicator = lv_obj_create(button);
  lv_obj_remove_style_all(indicator);
  lv_obj_set_pos(indicator, 0, 12);
  lv_obj_set_size(indicator, 3, 48);
  lv_obj_set_style_radius(indicator, 2, 0);
  lv_obj_set_style_bg_color(indicator, accent, 0);
  lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);

  icon = make_label(button, symbol, 18, 22, accent,
                    &lv_font_montserrat_16);
  label = make_label(button, scene->name, 54, 20,
                     lv_color_hex(COLOR_TEXT),
                     home_panel_font_get());
  (void)icon;
  return label;
}

static const struct home_panel_control_s *find_device_control(
  const struct home_panel_device_s *device, const char *name)
{
  unsigned int index;

  for (index = 0; index < device->control_count; index++)
    {
      if (strcmp(device->controls[index].name, name) == 0)
        {
          return &device->controls[index];
        }
    }

  return NULL;
}

static void format_device_value(const struct home_panel_device_s *device,
                                char *value, size_t capacity)
{
  const struct home_panel_control_s *volume;

  if (device->has_brightness)
    {
      int span = device->brightness_max - device->brightness_min;
      int percent = span > 0 ?
        (device->brightness - device->brightness_min) * 100 / span : 0;

      snprintf(value, capacity, "%d%%", percent);
    }
  else if (device->has_temperature)
    {
      snprintf(value, capacity, "%d°C", device->temperature);
    }
  else if (device->has_humidity)
    {
      snprintf(value, capacity, "湿度 %d%%", device->humidity);
    }
  else if (device->has_battery)
    {
      snprintf(value, capacity, "%d%%", device->battery);
    }
  else
    {
      volume = find_device_control(device, "volume");
      if (volume != NULL && volume->has_value)
        {
          snprintf(value, capacity, "音量 %d%%", volume->value);
        }
      else
        {
          snprintf(value, capacity, "%s",
                   device->online ? "在线" : "离线");
        }
    }
}

static const char *device_state_text(
  const struct home_panel_device_s *device)
{
  if (!device->online)
    {
      return "离线";
    }

  if (device->has_power)
    {
      return device->power ? "已开启" : "已关闭";
    }

  return "状态已同步";
}

static void make_device_card(lv_obj_t *parent, int x, int y,
                             int width, int height, const char *symbol,
                             const struct home_panel_device_s *device,
                             lv_color_t accent)
{
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_t *toggle;
  lv_obj_t *state;
  lv_obj_t *label;
  lv_obj_t *value_label;
  struct home_panel_device_binding_s *binding;
  unsigned int binding_index;
  const lv_font_t *value_font;
  char value[32];
  const char *state_text;
  bool checked = device->has_power && device->power;

  format_device_value(device, value, sizeof(value));
  value_font = device->has_brightness || device->has_temperature ||
               device->has_battery ? &home_panel_digits_28 :
                                     home_panel_font_get();

  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, width, height);
  style_panel(card);
  lv_obj_set_style_pad_all(card, 18, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  label = lv_obj_create(card);
  lv_obj_remove_style_all(label);
  lv_obj_set_pos(label, 0, 0);
  lv_obj_set_size(label, 38, 38);
  lv_obj_set_style_radius(label, UI_RADIUS, 0);
  lv_obj_set_style_bg_color(label, lv_color_hex(COLOR_SURFACE_2), 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
  make_label(label, symbol, 11, 10, accent, &lv_font_montserrat_16);
  make_label(card, device->room[0] != '\0' ? device->room : "未分房间",
             50, 8, lv_color_hex(COLOR_MUTED),
             home_panel_font_get());
  label = make_label(card, device->name, 0, 62,
                     lv_color_hex(COLOR_TEXT), home_panel_font_get());
  lv_obj_set_width(label, width - 36);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  value_label = make_label(card, value, 0, 100, accent, value_font);

  state_text = device_state_text(device);
  state = make_label(card, state_text, 0, 169,
                     lv_color_hex(checked ? COLOR_GREEN : COLOR_MUTED),
                     home_panel_font_get());

  if (!device->power_writable)
    {
      return;
    }

  binding_index = g_device_binding_counts[g_current_page];
  if (binding_index >= HOME_PANEL_MAX_DEVICES + HOME_CARD_COUNT)
    {
      return;
    }
  binding = &g_device_bindings[g_current_page][binding_index];
  g_device_binding_counts[g_current_page]++;

  toggle = lv_button_create(card);
  configure_fast_button(toggle);
  lv_obj_set_size(toggle, 52, 32);
  lv_obj_align(toggle, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  style_toggle(toggle, checked);
  lv_obj_add_flag(toggle, LV_OBJ_FLAG_CHECKABLE);
  if (checked)
    {
      lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
  if (!device->online)
    {
      lv_obj_add_state(toggle, LV_STATE_DISABLED);
    }

  label = lv_label_create(toggle);
  lv_label_set_text(label, LV_SYMBOL_POWER);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_center(label);
  binding->device = device;
  binding->button = toggle;
  binding->value_label = value_label;
  binding->state_label = state;
  lv_obj_add_event_cb(toggle, device_toggled, LV_EVENT_VALUE_CHANGED,
                      binding);
}

static lv_obj_t *make_action_button(lv_obj_t *parent, const char *text,
                                    int x, int y, int width)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_t *label;

  configure_fast_button(button);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, 52);
  lv_obj_set_style_radius(button, UI_RADIUS, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(0x7bb4ff), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_BLUE), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x478ee8),
                            LV_STATE_PRESSED);
  lv_obj_add_event_cb(button, action_clicked, LV_EVENT_CLICKED, (void *)text);

  label = lv_label_create(button);
  lv_label_set_text(label, text);
  set_chinese_font(label);
  lv_obj_center(label);
  return button;
}

static void style_secondary_action(lv_obj_t *button)
{
  lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_SURFACE_2), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_BORDER),
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_color(button, lv_color_hex(COLOR_BORDER), 0);
}

static lv_obj_t *make_info_row(lv_obj_t *parent, const char *name,
                               const char *value, int y, lv_color_t color)
{
  make_label(parent, name, 32, y, lv_color_hex(COLOR_MUTED),
             home_panel_font_get());
  return make_label(parent, value, 238, y, color,
                    home_panel_font_get());
}

static void network_refresh_clicked(lv_event_t *event)
{
  (void)event;

  g_network_refresh_requested = true;
  if (g_status_label != NULL)
    {
      lv_label_set_text(g_status_label, "已请求重新检测网络");
      lv_obj_set_style_text_color(g_status_label,
                                  lv_color_hex(COLOR_BLUE), 0);
    }
}

static int device_card_score(const struct home_panel_device_s *device)
{
  if (!device->online || !device->power_writable)
    {
      return -1;
    }

  if (strcmp(device->type, "light") == 0)
    {
      return 100;
    }
  if (strcmp(device->type, "switch") == 0)
    {
      return 80;
    }
  if (strcmp(device->type, "outlet") == 0)
    {
      return 60;
    }
  return 20;
}

static const char *device_symbol(const struct home_panel_device_s *device)
{
  if (strcmp(device->type, "light") == 0)
    {
      return LV_SYMBOL_BULLET;
    }
  if (strcmp(device->type, "outlet") == 0)
    {
      return LV_SYMBOL_CHARGE;
    }
  return LV_SYMBOL_POWER;
}

static unsigned int select_device_cards(unsigned int selected[3])
{
  bool used[HOME_PANEL_MAX_DEVICES] = {false};
  unsigned int count = 0;

  while (count < 3)
    {
      int best_score = -1;
      unsigned int best = 0;
      unsigned int index;

      for (index = 0; index < g_family_model.device_count; index++)
        {
          int score;

          if (used[index])
            {
              continue;
            }
          score = device_card_score(&g_family_model.devices[index]);
          if (score > best_score)
            {
              best_score = score;
              best = index;
            }
        }

      if (best_score < 0)
        {
          break;
        }
      used[best] = true;
      selected[count++] = best;
    }

  return count;
}

static void create_home_page(void)
{
  const struct home_panel_room_s *environment = NULL;
  unsigned int selected[3];
  unsigned int selected_count;
  unsigned int index;
  char subtitle[96];
  char status[64];

  make_label(g_content, "晚上好，欢迎回家", 30, 22,
             lv_color_hex(COLOR_TEXT), home_panel_font_get());

  for (index = 0; index < g_family_model.room_count; index++)
    {
      if (g_family_model.rooms[index].has_temperature)
        {
          environment = &g_family_model.rooms[index];
          if (strcmp(environment->name, "客厅") == 0)
            {
              break;
            }
        }
    }
  if (environment != NULL)
    {
      snprintf(subtitle, sizeof(subtitle), "%s %d°C  ·  湿度 %d%%",
               environment->name, environment->temperature,
               environment->humidity);
    }
  else
    {
      snprintf(subtitle, sizeof(subtitle), "%s",
               g_family_model_valid ? "环境传感器暂无数据" :
                                      "正在同步米家设备");
    }
  g_home_summary_label = make_label(g_content, subtitle, 30, 54,
                                    lv_color_hex(COLOR_MUTED),
                                    home_panel_font_get());

  for (index = 0; index < g_family_model.scene_count && index < 3; index++)
    {
      static const int positions[3] = {30, 226, 422};
      static const uint32_t colors[3] =
      {
        COLOR_GREEN, COLOR_BLUE, COLOR_ORANGE
      };
      make_scene_button(g_content, LV_SYMBOL_PLAY,
                        &g_family_model.scenes[index], index,
                        positions[index], lv_color_hex(colors[index]));
    }

  make_label(g_content, "常用设备", 30, 198, lv_color_hex(COLOR_TEXT),
             home_panel_font_get());
  snprintf(status, sizeof(status), "%u/%u 台在线",
           g_family_model.online_count, g_family_model.device_count);
  g_status_label = make_label(g_content, status, 666, 198,
                              lv_color_hex(COLOR_MUTED),
                              home_panel_font_get());

  selected_count = select_device_cards(selected);
  for (index = 0; index < selected_count; index++)
    {
      static const int positions[3] = {30, 288, 546};
      static const uint32_t colors[3] =
      {
        COLOR_ORANGE, COLOR_BLUE, COLOR_GREEN
      };
      const struct home_panel_device_s *device =
        &g_family_model.devices[selected[index]];
      make_device_card(g_content, positions[index], 238, 240, 218,
                       device_symbol(device), device,
                       lv_color_hex(colors[index]));
    }
}

static bool device_in_room(const struct home_panel_device_s *device,
                           const struct home_panel_room_s *room)
{
  return device->room[0] != '\0' && room->name[0] != '\0' &&
         strcmp(device->room, room->name) == 0;
}

static void update_room_button_styles(void)
{
  if (g_room_highlighted < g_family_model.room_count &&
      g_room_highlighted != g_selected_room &&
      g_room_buttons[g_room_highlighted] != NULL)
    {
      lv_obj_set_style_bg_color(
        g_room_buttons[g_room_highlighted], lv_color_hex(COLOR_NAV), 0);
    }

  if (g_selected_room < g_family_model.room_count &&
      g_room_buttons[g_selected_room] != NULL &&
      g_room_highlighted != g_selected_room)
    {
      lv_obj_set_style_bg_color(g_room_buttons[g_selected_room],
                                lv_color_hex(COLOR_NAV_ACTIVE), 0);
    }

  g_room_highlighted = g_selected_room;
}

static lv_obj_t *create_room_device_container(void)
{
  lv_obj_t *host = lv_obj_create(g_content);

  lv_obj_set_pos(host, 210, 92);
  lv_obj_set_size(host, 642, 418);
  lv_obj_set_style_radius(host, 0, 0);
  lv_obj_set_style_border_width(host, 0, 0);
  lv_obj_set_style_bg_color(host, lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(host, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(host, 10, 0);
  lv_obj_set_scroll_dir(host, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(host, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(host, LV_OBJ_FLAG_SCROLL_MOMENTUM |
                           LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_set_scroll_snap_y(host, LV_SCROLL_SNAP_NONE);
  return host;
}

static const char *device_type_text(const struct home_panel_device_s *device)
{
  if (strcmp(device->type, "light") == 0)
    {
      return "灯光设备";
    }
  if (strcmp(device->type, "speaker") == 0)
    {
      return "智能音响";
    }
  if (strcmp(device->type, "environment-sensor") == 0)
    {
      return "温湿度传感器";
    }
  if (strcmp(device->type, "contact-sensor") == 0)
    {
      return "门窗传感器";
    }
  if (strcmp(device->type, "outlet") == 0)
    {
      return "智能插座";
    }
  if (strcmp(device->type, "heater") == 0)
    {
      return "取暖设备";
    }
  return "米家设备";
}

static void room_device_clicked(lv_event_t *event)
{
  uintptr_t device = (uintptr_t)lv_event_get_user_data(event);

  if (device > 0)
    {
      show_room_device_detail((unsigned int)device - 1);
    }
}

static void make_room_device_summary(lv_obj_t *parent, int x, int y,
                                     const struct home_panel_device_s *device,
                                     unsigned int device_index,
                                     lv_color_t accent)
{
  struct home_panel_device_binding_s *binding = NULL;
  lv_obj_t *card;
  lv_obj_t *label;
  lv_obj_t *value_label;
  lv_obj_t *state_label;
  unsigned int binding_index;
  char value[32];

  card = lv_button_create(parent);
  configure_fast_button(card);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, ROOM_CARD_WIDTH, 108);
  lv_obj_set_style_radius(card, UI_RADIUS, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_SURFACE_2),
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
  lv_obj_add_event_cb(card, room_device_clicked, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)(device_index + 1));

  make_label(card, device_symbol(device), 14, 12, accent,
             &lv_font_montserrat_16);
  label = make_label(card, device->name, 48, 10,
                     lv_color_hex(COLOR_TEXT), home_panel_font_get());
  lv_obj_set_width(label, 196);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  make_label(card, LV_SYMBOL_RIGHT, 258, 10,
             lv_color_hex(COLOR_MUTED), &lv_font_montserrat_16);

  format_device_value(device, value, sizeof(value));
  value_label = make_label(card, value, 16, 58, accent,
                           home_panel_font_get());
  state_label = make_label(card, device_state_text(device), 190, 58,
                           lv_color_hex(device->online ? COLOR_GREEN :
                                                        COLOR_MUTED),
                           home_panel_font_get());
  lv_obj_set_width(state_label, 72);
  lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_RIGHT, 0);

  binding_index = g_device_binding_counts[g_current_page];
  if (binding_index < HOME_PANEL_MAX_DEVICES + HOME_CARD_COUNT)
    {
      binding = &g_device_bindings[g_current_page][binding_index];
      g_device_binding_counts[g_current_page]++;
      binding->device = device;
      binding->button = NULL;
      binding->value_label = value_label;
      binding->state_label = state_label;
    }
}

static lv_obj_t *make_detail_metric(lv_obj_t *parent, int x, int y,
                                    const char *name, const char *value,
                                    lv_color_t accent)
{
  lv_obj_t *card = lv_obj_create(parent);

  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, 184, 76);
  style_panel(card);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  make_label(card, name, 12, 8, lv_color_hex(COLOR_MUTED),
             home_panel_font_get());
  return make_label(card, value, 12, 38, accent, home_panel_font_get());
}

static void make_detail_control(lv_obj_t *parent, int y,
                                const struct home_panel_device_s *device,
                                const struct home_panel_control_s *property)
{
  struct home_panel_control_binding_s *binding;
  lv_obj_t *row;
  lv_obj_t *control;
  lv_obj_t *value_label;
  lv_obj_t *label;
  char value[24];

  if (g_control_binding_count >= HOME_PANEL_MAX_CONTROLS)
    {
      return;
    }

  binding = &g_control_bindings[g_control_binding_count++];
  binding->device = device;
  binding->property = property;

  row = lv_obj_create(parent);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_size(row, 586,
                  property->type == HOME_PANEL_CONTROL_NUMBER ? 92 : 64);
  style_panel(row);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  label = make_label(row, control_display_name(property->name), 14, 10,
                     lv_color_hex(COLOR_TEXT), home_panel_font_get());
  lv_obj_set_width(label, 260);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

  if (property->type == HOME_PANEL_CONTROL_BOOLEAN)
    {
      value_label = make_label(row,
                               property->has_value ?
                                 (property->boolean_value ? "已开启" :
                                                            "已关闭") :
                                 "状态未知",
                               330, 10, lv_color_hex(COLOR_MUTED),
                               home_panel_font_get());
      control = lv_button_create(row);
      configure_fast_button(control);
      lv_obj_set_size(control, 52, 32);
      lv_obj_set_pos(control, 510, 6);
      style_toggle(control, property->boolean_value);
      lv_obj_add_flag(control, LV_OBJ_FLAG_CHECKABLE);
      if (property->boolean_value)
        {
          lv_obj_add_state(control, LV_STATE_CHECKED);
        }
      label = lv_label_create(control);
      lv_label_set_text(label, LV_SYMBOL_POWER);
      lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
      lv_obj_center(label);
      lv_obj_add_event_cb(control, detail_boolean_changed,
                          LV_EVENT_VALUE_CHANGED, binding);
    }
  else
    {
      format_control_value(property, property->value,
                           value, sizeof(value));
      value_label = make_label(row, property->has_value ? value : "--",
                               492, 10, lv_color_hex(COLOR_BLUE),
                               home_panel_font_get());
      lv_obj_set_width(value_label, 70);
      lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
      control = lv_slider_create(row);
      lv_obj_set_pos(control, 16, 56);
      lv_obj_set_size(control, 546, 16);
      lv_slider_set_range(control, property->minimum, property->maximum);
      lv_slider_set_value(control, property->value, LV_ANIM_OFF);
      lv_obj_set_style_bg_color(control, lv_color_hex(COLOR_SURFACE_2),
                                LV_PART_MAIN);
      lv_obj_set_style_bg_color(control, lv_color_hex(COLOR_BLUE),
                                LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(control, lv_color_hex(COLOR_TEXT),
                                LV_PART_KNOB);
      lv_obj_set_style_radius(control, UI_CONTROL_RADIUS, LV_PART_MAIN);
      lv_obj_set_style_radius(control, UI_CONTROL_RADIUS,
                              LV_PART_INDICATOR);
      lv_obj_set_style_radius(control, UI_CONTROL_RADIUS, LV_PART_KNOB);
      lv_obj_add_event_cb(control, detail_number_changed,
                          LV_EVENT_VALUE_CHANGED, binding);
      lv_obj_add_event_cb(control, detail_number_released,
                          LV_EVENT_RELEASED, binding);
    }

  if (!device->online)
    {
      lv_obj_add_state(control, LV_STATE_DISABLED);
    }
  binding->control = control;
  binding->value_label = value_label;
}

static void reset_detail_bindings(void)
{
  g_selected_device = HOME_PANEL_MAX_DEVICES;
  g_control_binding_count = 0;
  memset(g_control_bindings, 0, sizeof(g_control_bindings));
  g_detail_subtitle_label = NULL;
  g_detail_temperature_label = NULL;
  g_detail_humidity_label = NULL;
  g_detail_battery_label = NULL;
}

static void close_room_device_detail(lv_event_t *event)
{
  unsigned int room = g_selected_room;

  (void)event;
  if (g_room_detail_host != NULL)
    {
      lv_obj_delete(g_room_detail_host);
      g_room_detail_host = NULL;
    }
  reset_detail_bindings();
  g_selected_room = HOME_PANEL_MAX_ROOMS;
  render_room_devices(room);
}

static void show_room_device_detail(unsigned int device_index)
{
  const struct home_panel_device_s *device;
  lv_obj_t *back;
  lv_obj_t *label;
  unsigned int metric = 0;
  unsigned int index;
  int y = 76;
  char value[32];
  char subtitle[96];

  if (device_index >= g_family_model.device_count ||
      g_room_device_host == NULL)
    {
      return;
    }

  if (g_room_detail_host != NULL)
    {
      lv_obj_delete(g_room_detail_host);
    }
  lv_obj_add_flag(g_room_device_host, LV_OBJ_FLAG_HIDDEN);
  g_room_detail_host = create_room_device_container();
  reset_detail_bindings();
  g_selected_device = device_index;
  device = &g_family_model.devices[device_index];

  back = lv_button_create(g_room_detail_host);
  configure_fast_button(back);
  lv_obj_set_pos(back, 0, 0);
  lv_obj_set_size(back, 42, 42);
  lv_obj_set_style_radius(back, UI_RADIUS, 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_set_style_border_width(back, 1, 0);
  lv_obj_set_style_border_color(back, lv_color_hex(COLOR_BORDER), 0);
  lv_obj_set_style_bg_color(back, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_add_event_cb(back, close_room_device_detail, LV_EVENT_CLICKED, NULL);
  label = lv_label_create(back);
  lv_label_set_text(label, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_center(label);

  label = make_label(g_room_detail_host, device->name, 56, 0,
                     lv_color_hex(COLOR_TEXT), home_panel_font_get());
  lv_obj_set_width(label, 420);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  snprintf(subtitle, sizeof(subtitle), "%s · %s",
           device_type_text(device), device->online ? "在线" : "离线");
  g_detail_subtitle_label = make_label(
    g_room_detail_host, subtitle, 56, 32,
    lv_color_hex(device->online ? COLOR_GREEN : COLOR_MUTED),
    home_panel_font_get());

  if (device->has_temperature)
    {
      snprintf(value, sizeof(value), "%d°C", device->temperature);
      g_detail_temperature_label = make_detail_metric(
        g_room_detail_host, (int)metric++ * 196, y,
        "温度", value, lv_color_hex(COLOR_ORANGE));
    }
  if (device->has_humidity)
    {
      snprintf(value, sizeof(value), "%d%%", device->humidity);
      g_detail_humidity_label = make_detail_metric(
        g_room_detail_host, (int)metric++ * 196, y,
        "湿度", value, lv_color_hex(COLOR_BLUE));
    }
  if (device->has_battery && metric < 3)
    {
      snprintf(value, sizeof(value), "%d%%", device->battery);
      g_detail_battery_label = make_detail_metric(
        g_room_detail_host, (int)metric++ * 196, y,
        "电量", value, lv_color_hex(COLOR_GREEN));
    }
  if (metric > 0)
    {
      y += 90;
    }

  for (index = 0; index < device->control_count; index++)
    {
      make_detail_control(g_room_detail_host, y, device,
                          &device->controls[index]);
      y += device->controls[index].type == HOME_PANEL_CONTROL_NUMBER ?
           104 : 76;
    }

  if (metric == 0 && device->control_count == 0)
    {
      label = make_label(g_room_detail_host, "该设备暂无可用控制项", 0, y,
                         lv_color_hex(COLOR_MUTED), home_panel_font_get());
      lv_obj_set_width(label, 586);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

  set_label_text_if_changed(g_room_title_label, device->name);
  set_label_text_if_changed(g_room_summary_label, subtitle);
  g_status_label = g_room_summary_label;
  syslog(LOG_INFO, "[HOME][UI] device=%s type=%s controls=%u\n",
         device->name, device->type, device->control_count);
}

static lv_obj_t *create_room_device_view(unsigned int room_index)
{
  static const uint32_t colors[] =
  {
    COLOR_ORANGE, COLOR_BLUE, COLOR_GREEN
  };
  const struct home_panel_room_s *room;
  lv_obj_t *host;
  unsigned int device_index;
  unsigned int visible = 0;

  if (room_index >= g_family_model.room_count)
    {
      return NULL;
    }

  host = create_room_device_container();
  if (host == NULL)
    {
      return NULL;
    }

  room = &g_family_model.rooms[room_index];
  for (device_index = 0;
       device_index < g_family_model.device_count;
       device_index++)
    {
      const struct home_panel_device_s *device =
        &g_family_model.devices[device_index];
      unsigned int column;
      unsigned int row;

      if (!device_in_room(device, room))
        {
          continue;
        }

      column = visible % 2;
      row = visible / 2;
      make_room_device_summary(
        host, (int)column * (ROOM_CARD_WIDTH + ROOM_CARD_GAP),
        (int)row * 120, device, device_index,
        lv_color_hex(colors[visible % 3]));
      visible++;
    }

  if (visible == 0)
    {
      lv_obj_t *empty = make_label(host,
                                   "该房间暂无设备", 0, 24,
                                   lv_color_hex(COLOR_MUTED),
                                   home_panel_font_get());
      lv_obj_set_width(empty, 580);
      lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    }

  g_room_visible_counts[room_index] = visible;
  lv_obj_add_flag(host, LV_OBJ_FLAG_HIDDEN);
  return host;
}

static void render_room_devices(unsigned int room_index)
{
  const struct home_panel_room_s *room;
  bool reused;
  char summary[96];

  if (g_family_model.room_count == 0)
    {
      return;
    }

  if (room_index >= g_family_model.room_count)
    {
      room_index = 0;
    }

  if (g_room_detail_host != NULL)
    {
      lv_obj_delete(g_room_detail_host);
      g_room_detail_host = NULL;
      reset_detail_bindings();
    }

  if (room_index == g_selected_room &&
      g_room_device_host != NULL &&
      g_room_device_host == g_room_device_hosts[room_index])
    {
      return;
    }

  if (g_room_device_host != NULL)
    {
      lv_obj_add_flag(g_room_device_host, LV_OBJ_FLAG_HIDDEN);
    }

  g_selected_room = room_index;
  room = &g_family_model.rooms[room_index];
  g_room_device_host = g_room_device_hosts[room_index];
  reused = g_room_device_host != NULL;
  if (g_room_device_host == NULL)
    {
      g_room_device_host = create_room_device_view(room_index);
      g_room_device_hosts[room_index] = g_room_device_host;
    }

  if (g_room_device_host == NULL)
    {
      return;
    }

  lv_obj_remove_flag(g_room_device_host, LV_OBJ_FLAG_HIDDEN);
  set_label_text_if_changed(g_room_title_label, room->name);
  if (room->has_temperature && room->has_humidity)
    {
      snprintf(summary, sizeof(summary), "%u 台设备 · %d°C · 湿度 %d%%",
               room->device_count, room->temperature, room->humidity);
    }
  else
    {
      snprintf(summary, sizeof(summary), "%u 台设备",
               room->device_count);
    }
  set_label_text_if_changed(g_room_summary_label, summary);
  update_room_button_styles();
  syslog(LOG_INFO, "[HOME][UI] room=%s devices=%u cached=%u\n",
         room->name, g_room_visible_counts[room_index], reused ? 1 : 0);
}

static void room_clicked(lv_event_t *event)
{
  uintptr_t room = (uintptr_t)lv_event_get_user_data(event);
  uint32_t started;
  uint32_t elapsed;

  if (room == 0)
    {
      return;
    }

  started = lv_tick_get();
  render_room_devices((unsigned int)room - 1);
  elapsed = lv_tick_elaps(started);
  if (elapsed >= UI_SLOW_LOG_MS)
    {
      syslog(LOG_WARNING,
             "[HOME][PERF] room-build index=%u elapsed=%ums\n",
             (unsigned int)room - 1, (unsigned int)elapsed);
    }
}

static void create_rooms_page(void)
{
  lv_obj_t *sidebar;
  lv_obj_t *button;
  lv_obj_t *label;
  unsigned int index;
  char count[24];

  memset(g_room_buttons, 0, sizeof(g_room_buttons));
  memset(g_room_device_hosts, 0, sizeof(g_room_device_hosts));
  memset(g_room_visible_counts, 0, sizeof(g_room_visible_counts));
  g_room_highlighted = HOME_PANEL_MAX_ROOMS;
  g_room_title_label = NULL;
  g_room_summary_label = NULL;
  g_room_device_host = NULL;
  g_room_detail_host = NULL;
  reset_detail_bindings();

  sidebar = lv_obj_create(g_content);
  lv_obj_remove_style_all(sidebar);
  lv_obj_set_pos(sidebar, 0, 0);
  lv_obj_set_size(sidebar, ROOM_NAV_WIDTH,
                  PANEL_HEIGHT - TOPBAR_HEIGHT);
  lv_obj_set_style_bg_color(sidebar, lv_color_hex(COLOR_NAV), 0);
  lv_obj_set_style_bg_opa(sidebar, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_left(sidebar, 10, 0);
  lv_obj_set_style_pad_right(sidebar, 10, 0);
  lv_obj_set_style_pad_top(sidebar, 12, 0);
  lv_obj_set_style_pad_bottom(sidebar, 12, 0);
  lv_obj_set_scroll_dir(sidebar, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(sidebar, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(sidebar, LV_OBJ_FLAG_SCROLL_MOMENTUM |
                              LV_OBJ_FLAG_SCROLL_ELASTIC);

  make_label(sidebar, "房间", 10, 4, lv_color_hex(COLOR_TEXT),
             home_panel_font_get());
  for (index = 0; index < g_family_model.room_count; index++)
    {
      const struct home_panel_room_s *room = &g_family_model.rooms[index];

      button = lv_button_create(sidebar);
      configure_fast_button(button);
      g_room_buttons[index] = button;
      lv_obj_set_pos(button, 0, 48 + (int)index * 58);
      lv_obj_set_size(button, ROOM_NAV_WIDTH - 20, 50);
      lv_obj_set_style_radius(button, UI_RADIUS, 0);
      lv_obj_set_style_shadow_width(button, 0, 0);
      lv_obj_set_style_border_width(button, 0, 0);
      lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_NAV), 0);
      lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_SURFACE_2),
                                LV_STATE_PRESSED);
      lv_obj_add_event_cb(button, room_clicked, LV_EVENT_PRESSED,
                          (void *)(uintptr_t)(index + 1));

      label = make_label(button, room->name, 8, 4,
                         lv_color_hex(COLOR_MUTED),
                         home_panel_font_get());
      lv_obj_set_width(label, 112);
      lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
      snprintf(count, sizeof(count), "%u", room->device_count);
      label = make_label(button, count, 132, 4,
                         lv_color_hex(COLOR_MUTED),
                         home_panel_font_get());
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    }

  g_room_title_label = make_label(g_content, "房间", 216, 20,
                                  lv_color_hex(COLOR_TEXT),
                                  home_panel_font_get());
  g_room_summary_label = make_label(g_content,
                                    "按空间查看和控制设备", 216, 52,
                                    lv_color_hex(COLOR_MUTED),
                                    home_panel_font_get());

  if (g_family_model.room_count > 0)
    {
      if (g_selected_room >= g_family_model.room_count)
        {
          g_selected_room = 0;
        }
      render_room_devices(g_selected_room);
    }
  else
    {
      lv_label_set_text(g_room_summary_label, "正在同步米家房间");
      g_room_device_host = create_room_device_container();
      label = make_label(g_room_device_host, "暂无房间数据", 0, 24,
                         lv_color_hex(COLOR_MUTED),
                         home_panel_font_get());
      lv_obj_set_width(label, 580);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

  g_status_label = g_room_summary_label;
}

static void create_scenes_page(void)
{
  unsigned int index;

  make_label(g_content, "场景", 30, 22, lv_color_hex(COLOR_TEXT),
             home_panel_font_get());
  make_label(g_content, "一次控制多个家庭设备", 30, 54,
             lv_color_hex(COLOR_MUTED), home_panel_font_get());
  for (index = 0; index < g_family_model.scene_count && index < 3; index++)
    {
      static const int positions[3] = {30, 226, 422};
      static const uint32_t colors[3] =
      {
        COLOR_GREEN, COLOR_BLUE, COLOR_ORANGE
      };
      make_scene_button(g_content, LV_SYMBOL_PLAY,
                        &g_family_model.scenes[index], index,
                        positions[index], lv_color_hex(colors[index]));
    }
  g_status_label = make_label(g_content,
                              g_family_model.scene_count > 0 ?
                              "点击场景即可执行" : "暂无可执行场景",
                              30, 208, lv_color_hex(COLOR_MUTED),
                              home_panel_font_get());
}

struct proactive_persist_work_s
{
  size_t length;
  uint8_t data[PROACTIVE_PROFILE_SIZE];
};

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

  ret = home_proactive_export_profile(work->data, sizeof(work->data),
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

static bool proactive_persist_retry_ready(void)
{
  irqstate_t flags;
  bool ready;

  flags = up_irq_save();
  ready = g_proactive_persist_pending && !g_proactive_persist_busy &&
          (int32_t)(lv_tick_get() - g_proactive_persist_retry_at) >= 0;
  up_irq_restore(flags);
  return ready;
}

static void load_proactive_profile(void)
{
  uint8_t data[PROACTIVE_PROFILE_SIZE];
  size_t length = 0;
  int ret;

  home_proactive_initialize();
  ret = board_agent_persist_read(data, sizeof(data), &length);
  if (ret == 0)
    {
      ret = home_proactive_import_profile(data, length);
    }

  syslog(LOG_INFO, "[HOME][AGENT] profile %s ret=%d bytes=%u\n",
         ret == 0 ? "restored" : "cold-start", ret,
         (unsigned int)length);
}

static bool text_contains(const char *text, const char *needle)
{
  return text != NULL && needle != NULL && strstr(text, needle) != NULL;
}

static void update_proactive_context(void)
{
  struct home_proactive_context_s context;
  bool target_is_living_room = false;
  time_t now = time(NULL);
  struct tm local_time;
  unsigned int index;

  memset(&context, 0, sizeof(context));
  context.network_online = g_network_state == NETWORK_ONLINE;
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

  for (index = 0; index < g_family_model.device_count; index++)
    {
      const struct home_panel_device_s *device =
        &g_family_model.devices[index];
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

struct proactive_action_target_s
{
  const struct home_panel_device_s *device;
  const struct home_panel_control_s *control;
  int current_value;
  bool available;
  bool already_satisfied;
};

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

  for (index = 0; index < g_family_model.device_count; index++)
    {
      const struct home_panel_device_s *device =
        &g_family_model.devices[index];
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
  bool automation)
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
    }

  return ret;
}

static lv_obj_t *make_proactive_metric(lv_obj_t *parent, const char *name,
                                       int x, lv_obj_t **value_label)
{
  lv_obj_t *panel = lv_obj_create(parent);

  lv_obj_set_pos(panel, x, 92);
  lv_obj_set_size(panel, 182, 82);
  style_panel(panel);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  make_label(panel, name, 14, 10, lv_color_hex(COLOR_MUTED),
             home_panel_font_get());
  *value_label = make_label(panel, "--", 14, 40,
                            lv_color_hex(COLOR_TEXT),
                            home_panel_font_get());
  return panel;
}

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

static void update_proactive_widgets(void)
{
  struct home_proactive_snapshot_s snapshot;
  struct home_panel_agent_snapshot_s agent;
  struct proactive_action_target_s target;
  char text[384];
  bool actionable;
  bool agent_matches;
  bool cloud_blocks;
  bool generic;
  bool target_available;
  bool visible;

  home_proactive_get_snapshot(&snapshot);
  home_panel_mijia_get_agent_snapshot(&agent);
  if (g_proactive_mode_label == NULL)
    {
      return;
    }

  g_displayed_proactive_revision = snapshot.revision;
  g_displayed_agent_revision = agent.revision;
  generic = snapshot.candidate_kind != HOME_PROACTIVE_CANDIDATE_SLEEP;
  target_available = generic ?
    proactive_resolve_action(&snapshot, &target) :
    snapshot.target_available;
  agent_matches = proactive_agent_matches(&agent, &snapshot);
  cloud_blocks = agent_matches &&
                 (agent.state == HOME_PANEL_AGENT_PENDING ||
                  (agent.state == HOME_PANEL_AGENT_CLOUD_READY &&
                   agent.decision != HOME_PANEL_AGENT_PROPOSE));
  actionable = snapshot.suggestion_available && target_available &&
               !cloud_blocks;
  set_label_text_if_changed(
    g_proactive_mode_label,
    snapshot.mode == HOME_PROACTIVE_REAL ?
      (agent_matches && agent.state == HOME_PANEL_AGENT_CLOUD_READY ?
        "真实运行｜云端深度分析" :
        (snapshot.profile_learned || snapshot.routine_count > 0) ?
          "真实运行｜本地持续学习" : "真实运行｜规则冷启动") :
    snapshot.mode == HOME_PROACTIVE_REPLAY ?
      "演示模式｜模拟历史数据" : "演示模式｜第 8 天");
  lv_obj_set_style_text_color(
    g_proactive_mode_label,
    lv_color_hex(snapshot.mode == HOME_PROACTIVE_REAL ? COLOR_GREEN :
                                                        COLOR_ORANGE), 0);

  snprintf(text, sizeof(text), "%u 天", snapshot.history_days);
  set_label_text_if_changed(g_proactive_history_label, text);
  snprintf(text, sizeof(text), "%02u:%02u",
           snapshot.preferred_hour, snapshot.preferred_minute);
  set_label_text_if_changed(g_proactive_time_label, text);
  snprintf(text, sizeof(text), "%u%%",
           agent_matches && agent.state != HOME_PANEL_AGENT_PENDING ?
             agent.adjusted_confidence : snapshot.confidence);
  set_label_text_if_changed(g_proactive_confidence_label, text);

  if (snapshot.mode == HOME_PROACTIVE_REPLAY)
    {
      snprintf(text, sizeof(text), "正在回放：第 %u / 7 天",
               snapshot.replay_day);
    }
  else if (snapshot.mode == HOME_PROACTIVE_DEMO_READY)
    {
      snprintf(text, sizeof(text),
               "已学习 %u 次反馈：接受 %u，忽略 %u",
               snapshot.feedback_count, snapshot.accepted_count,
               snapshot.ignored_count);
    }
  else
    {
      snprintf(text, sizeof(text),
               "真实画像：%u 条习惯，%u 次观察，%u 条自动化",
               snapshot.routine_count, snapshot.routine_observations,
               snapshot.automation_count);
    }
  set_label_text_if_changed(g_proactive_progress_label, text);

  visible = snapshot.suggestion_available;
  set_label_text_if_changed(
    g_proactive_suggestion_title,
    !visible ? "暂无主动建议" :
    agent_matches && agent.state == HOME_PANEL_AGENT_PENDING ?
      "主动建议｜云端分析中" :
    agent_matches && agent.state == HOME_PANEL_AGENT_CLOUD_READY &&
      agent.decision == HOME_PANEL_AGENT_SUPPRESS ?
      "云端建议暂缓" :
    agent_matches && agent.state == HOME_PANEL_AGENT_CLOUD_READY &&
      agent.decision == HOME_PANEL_AGENT_DEFER ?
      "云端建议继续观察" :
    snapshot.candidate_kind == HOME_PROACTIVE_CANDIDATE_EVENT_ROUTINE ?
      "设备联动建议" :
    snapshot.candidate_kind == HOME_PROACTIVE_CANDIDATE_TIME_ROUTINE ?
      "时间习惯建议" : "睡眠准备");
  if (visible)
    {
      if (agent_matches && agent.summary[0] != '\0')
        {
          snprintf(text, sizeof(text), "%s%s%s",
                   agent.summary,
                   agent.reasons[0] != '\0' ? "；" : "",
                   agent.reasons);
        }
      else if (generic)
        {
          snprintf(text, sizeof(text),
                   "本地已观察到相似行为 %u 次；预测触发时间 %02u:%02u；"
                   "当前置信度 %u%%。%s",
                   snapshot.candidate_observations,
                   snapshot.preferred_hour, snapshot.preferred_minute,
                   snapshot.confidence,
                   snapshot.candidate_kind ==
                     HOME_PROACTIVE_CANDIDATE_EVENT_ROUTINE ?
                     "本次由真实设备状态变化触发。" :
                     "本次由长期时间规律触发。");
        }
      else
        {
          snprintf(text, sizeof(text),
                   "触发依据：当前处于常用睡眠时段；%u 盏灯仍开启；"
                   "相似情境接受率 %u%%。",
                   snapshot.lights_on,
                   snapshot.feedback_count == 0 ? 0 :
                   snapshot.accepted_count * 100 /
                   snapshot.feedback_count);
        }
      set_label_text_if_changed(g_proactive_reason_label, text);
      if (cloud_blocks)
        {
          snprintf(text, sizeof(text),
                   agent.state == HOME_PANEL_AGENT_PENDING ?
                     "等待云端决策；超时后自动切换本地算法" :
                     "本次不下发设备操作，板端继续观察状态");
        }
      else if (generic && target_available)
        {
          if (target.already_satisfied)
            {
              snprintf(text, sizeof(text), "%s 已处于建议状态",
                       target.device->name);
            }
          else if (snapshot.action_is_boolean)
            {
              snprintf(text, sizeof(text), "建议将 %s 设置为%s",
                       target.device->name,
                       snapshot.action_value != 0 ? "开启" : "关闭");
            }
          else
            {
              snprintf(text, sizeof(text), "建议将 %s 的 %s 调整为 %d",
                       target.device->name, target.control->name,
                       snapshot.action_value);
            }
        }
      else if (generic)
        {
          snprintf(text, sizeof(text),
                   "目标设备当前离线、已删除或属性已不可写，本次不会执行");
        }
      else
        {
          snprintf(text, sizeof(text), "建议关闭 %s%s",
                   snapshot.target_available ? snapshot.target_name :
                                               "当前灯光",
                   snapshot.air_conditioner_on ? "，保留空调运行" : "");
        }
      set_label_text_if_changed(g_proactive_action_label, text);
    }
  else
    {
      set_label_text_if_changed(
        g_proactive_reason_label,
        snapshot.mode == HOME_PROACTIVE_REPLAY ?
          "正在压缩回放七天事件，算法与真实运行使用同一条事件链。" :
        !snapshot.clock_valid ?
          "等待网络校时完成；时间无效时不会产生真实主动建议。" :
          "系统会结合时间、设备状态和你的反馈生成建议。");
      set_label_text_if_changed(g_proactive_action_label,
                                "不会未经确认控制家庭设备");
    }

  if (g_proactive_accept_button != NULL)
    {
      if (actionable)
        {
          lv_obj_remove_flag(g_proactive_accept_button,
                             LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_proactive_accept_button, LV_OBJ_FLAG_HIDDEN);
        }
      if (visible)
        {
          lv_obj_remove_flag(g_proactive_ignore_button,
                             LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(g_proactive_less_button, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_proactive_ignore_button, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(g_proactive_less_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
  if (g_proactive_automation_button != NULL)
    {
      if (actionable && generic && !snapshot.automation_enabled)
        {
          lv_obj_remove_flag(g_proactive_automation_button,
                             LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_proactive_automation_button,
                          LV_OBJ_FLAG_HIDDEN);
        }
    }
  if (g_proactive_return_button != NULL)
    {
      if (snapshot.mode == HOME_PROACTIVE_REAL)
        {
          lv_obj_add_flag(g_proactive_return_button, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_remove_flag(g_proactive_return_button,
                             LV_OBJ_FLAG_HIDDEN);
        }
    }
  if (g_proactive_reset_button != NULL)
    {
      if (snapshot.mode == HOME_PROACTIVE_REAL)
        {
          lv_obj_remove_flag(g_proactive_reset_button,
                             LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_proactive_reset_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void proactive_replay_clicked(lv_event_t *event)
{
  (void)event;
  home_proactive_start_replay(lv_tick_get());
  set_label_text_if_changed(g_proactive_feedback_label,
                            "演示数据不会写入真实用户画像");
  update_proactive_widgets();
}

static void proactive_reset_clicked(lv_event_t *event)
{
  struct home_proactive_snapshot_s snapshot;

  (void)event;
  home_proactive_get_snapshot(&snapshot);
  if (snapshot.mode != HOME_PROACTIVE_REAL)
    {
      set_label_text_if_changed(g_proactive_feedback_label,
                                "请先返回真实模式再重置画像");
      return;
    }
  home_proactive_reset();
  update_proactive_context();
  schedule_proactive_persist();
  set_label_text_if_changed(g_proactive_feedback_label,
                            "真实用户画像已重置");
  update_proactive_widgets();
}

static void proactive_return_clicked(lv_event_t *event)
{
  (void)event;
  home_proactive_stop_replay();
  update_proactive_context();
  set_label_text_if_changed(g_proactive_feedback_label,
                            "已恢复回放前的真实画像");
  update_proactive_widgets();
}

static void proactive_feedback_clicked(lv_event_t *event)
{
  enum home_proactive_feedback_e feedback =
    (enum home_proactive_feedback_e)(uintptr_t)lv_event_get_user_data(event);
  struct home_panel_agent_snapshot_s agent;
  struct home_proactive_snapshot_s before;
  struct proactive_action_target_s target;
  bool generic;
  int ret = 0;

  home_proactive_get_snapshot(&before);
  home_panel_mijia_get_agent_snapshot(&agent);
  generic = before.candidate_kind != HOME_PROACTIVE_CANDIDATE_SLEEP;
  if (feedback == HOME_PROACTIVE_ENABLE_AUTOMATION && !generic)
    {
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
          set_label_text_if_changed(
            g_proactive_feedback_label,
            agent.state == HOME_PANEL_AGENT_PENDING ?
              "云端仍在分析，请稍候；失败后会自动切换本地算法" :
              "云端建议本次暂缓，未执行设备操作");
          return;
        }
      if (generic)
        {
          if (!proactive_resolve_action(&before, &target))
            {
              set_label_text_if_changed(
                g_proactive_feedback_label,
                "目标设备离线或属性已不可写，本次未执行");
              return;
            }
          ret = proactive_request_action(&before, &target, false);
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
        }
      if (ret < 0)
        {
          set_label_text_if_changed(g_proactive_feedback_label,
                                    ret == -EBUSY ?
                                    "请等待上一条设备指令完成" :
                                    "设备指令发送失败");
          return;
        }
    }

  home_proactive_feedback(feedback);
  if (before.mode == HOME_PROACTIVE_REAL)
    {
      schedule_proactive_persist();
    }
  set_label_text_if_changed(
    g_proactive_feedback_label,
    feedback == HOME_PROACTIVE_ENABLE_AUTOMATION ?
      "已执行本次建议，并建立本地断网自动化" :
    feedback == HOME_PROACTIVE_ACCEPT ?
      (ret == 1 ? "设备已处于建议状态，已记录本次选择" :
                  "指令已发送，等待设备状态确认") :
    feedback == HOME_PROACTIVE_IGNORE_TODAY ? "今天已忽略，已记录反馈" :
                                             "已降低提醒频率");
  update_proactive_widgets();
}

static void process_proactive_automation(void)
{
  struct home_proactive_snapshot_s snapshot;
  struct proactive_action_target_s target;
  uint32_t now_ms = lv_tick_get();
  int ret;

  if (g_proactive_command.active &&
      (int32_t)(now_ms - g_proactive_command.deadline_ms) > 0)
    {
      if (g_proactive_command.automation)
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

  ret = proactive_request_action(&snapshot, &target, true);
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

static void request_proactive_cloud_analysis(
  const struct home_proactive_snapshot_s *snapshot,
  enum home_panel_mijia_state_e mijia_state)
{
  struct home_panel_agent_request_s request;
  struct home_panel_agent_snapshot_s current;
  struct proactive_action_target_s target;
  bool request_current = false;
  bool target_available;
  int ret;

  if (snapshot != NULL &&
      g_requested_proactive_key == snapshot->decision_key)
    {
      home_panel_mijia_get_agent_snapshot(&current);
      request_current = proactive_agent_matches(&current, snapshot);
    }

  target_available =
    snapshot != NULL &&
    snapshot->candidate_kind != HOME_PROACTIVE_CANDIDATE_SLEEP ?
      proactive_resolve_action(snapshot, &target) :
      snapshot != NULL && snapshot->target_available;

  if (snapshot == NULL || !snapshot->suggestion_available ||
      snapshot->automation_enabled ||
      !snapshot->network_online || !target_available ||
      mijia_state != HOME_PANEL_MIJIA_AUTHENTICATED ||
      request_current)
    {
      return;
    }

  memset(&request, 0, sizeof(request));
  request.context_revision = snapshot->decision_key;
  request.routine_id = snapshot->routine_id;
  request.source = (unsigned int)snapshot->source;
  request.candidate_kind = (unsigned int)snapshot->candidate_kind;
  request.minute_of_day = snapshot->current_minute_of_day;
  request.local_confidence = snapshot->confidence;
  request.routine_observations = snapshot->candidate_observations;
  request.routine_accepted = snapshot->candidate_accepted;
  request.routine_rejected = snapshot->candidate_rejected;
  request.history_days = snapshot->history_days;
  request.feedback_count = snapshot->feedback_count;
  request.accepted_count = snapshot->accepted_count;
  request.ignored_count = snapshot->ignored_count;
  request.lights_on = snapshot->lights_on;
  request.local_eligible = snapshot->suggestion_available &&
                           target_available;
  request.automation_enabled = snapshot->automation_enabled;
  request.air_conditioner_on = snapshot->air_conditioner_on;
  request.network_online = snapshot->network_online;
  request.demo = snapshot->mode != HOME_PROACTIVE_REAL;
  ret = home_panel_mijia_request_agent_analysis(&request);
  if (ret == 0)
    {
      g_requested_proactive_key = snapshot->decision_key;
      syslog(LOG_INFO,
             "[HOME][AGENT] cloud analysis requested key=%u source=%u\n",
             (unsigned int)snapshot->decision_key,
             (unsigned int)snapshot->source);
    }
}

static void request_proactive_cloud_learning(
  const struct home_proactive_snapshot_s *snapshot,
  enum home_panel_mijia_state_e mijia_state)
{
  struct home_panel_agent_learning_request_s request;
  int ret;

  if (snapshot == NULL ||
      snapshot->mode != HOME_PROACTIVE_REAL ||
      snapshot->learning_revision == 0 ||
      snapshot->learning_routine_id == 0 ||
      snapshot->learning_observations == 0 ||
      !snapshot->network_online ||
      mijia_state != HOME_PANEL_MIJIA_AUTHENTICATED)
    {
      return;
    }

  memset(&request, 0, sizeof(request));
  request.profile_revision = snapshot->learning_revision;
  request.routine_id = snapshot->learning_routine_id;
  request.routine_kind = (unsigned int)snapshot->learning_kind;
  request.update_kind =
    (unsigned int)snapshot->learning_update_kind;
  request.observation_count = snapshot->learning_observations;
  request.accepted_count = snapshot->learning_accepted;
  request.rejected_count = snapshot->learning_rejected;
  request.confidence = snapshot->learning_confidence;
  request.mean_minute_of_day = snapshot->learning_mean_minute;
  request.mean_deviation_minutes =
    snapshot->learning_deviation_minutes;
  request.mean_delay_seconds = snapshot->learning_delay_seconds;
  request.automation_enabled =
    snapshot->learning_automation_enabled;
  ret = home_panel_mijia_request_agent_learning(&request);
  if (ret == -EALREADY)
    {
      if (home_proactive_ack_learning(snapshot->learning_revision,
                                      snapshot->learning_routine_id))
        {
          syslog(LOG_INFO,
                 "[HOME][AGENT] learning acknowledged revision=%u "
                 "routine=%u\n",
                 (unsigned int)snapshot->learning_revision,
                 (unsigned int)snapshot->learning_routine_id);
        }
    }
}

static void create_proactive_page(void)
{
  lv_obj_t *panel;
  lv_obj_t *button;

  make_label(g_content, "主动智能", 30, 22, lv_color_hex(COLOR_TEXT),
             home_panel_font_get());
  make_label(g_content, "板端学习、可解释建议与安全确认", 30, 54,
             lv_color_hex(COLOR_MUTED), home_panel_font_get());
  g_proactive_mode_label = make_label(g_content, "真实运行", 610, 28,
                                      lv_color_hex(COLOR_GREEN),
                                      home_panel_font_get());

  make_proactive_metric(g_content, "有效历史", 30,
                        &g_proactive_history_label);
  make_proactive_metric(g_content, "习惯参考时间", 226,
                        &g_proactive_time_label);
  make_proactive_metric(g_content, "建议置信度", 422,
                        &g_proactive_confidence_label);
  g_proactive_progress_label = make_label(g_content, "真实画像：0 次反馈",
                                          30, 184,
                                          lv_color_hex(COLOR_MUTED),
                                          home_panel_font_get());

  button = make_action_button(g_content, "回放 7 天", 628, 92, 192);
  lv_obj_remove_event_cb(button, action_clicked);
  lv_obj_add_event_cb(button, proactive_replay_clicked,
                      LV_EVENT_CLICKED, NULL);
  g_proactive_return_button = make_action_button(
    g_content, "返回真实", 628, 154, 92);
  style_secondary_action(g_proactive_return_button);
  lv_obj_remove_event_cb(g_proactive_return_button, action_clicked);
  lv_obj_add_event_cb(g_proactive_return_button, proactive_return_clicked,
                      LV_EVENT_CLICKED, NULL);
  g_proactive_reset_button = make_action_button(
    g_content, "重置画像", 728, 154, 92);
  style_secondary_action(g_proactive_reset_button);
  lv_obj_remove_event_cb(g_proactive_reset_button, action_clicked);
  lv_obj_add_event_cb(g_proactive_reset_button, proactive_reset_clicked,
                      LV_EVENT_CLICKED, NULL);

  panel = lv_obj_create(g_content);
  lv_obj_set_pos(panel, 30, 220);
  lv_obj_set_size(panel, 790, 206);
  style_panel(panel);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  g_proactive_suggestion_title = make_label(
    panel, "暂无主动建议", 20, 14, lv_color_hex(COLOR_TEXT),
    home_panel_font_get());
  g_proactive_reason_label = make_label(
    panel, "系统会结合时间、设备状态和你的反馈生成建议。",
    20, 50, lv_color_hex(COLOR_MUTED), home_panel_font_get());
  lv_obj_set_width(g_proactive_reason_label, 742);
  lv_label_set_long_mode(g_proactive_reason_label, LV_LABEL_LONG_WRAP);
  g_proactive_action_label = make_label(
    panel, "不会未经确认控制家庭设备", 20, 112,
    lv_color_hex(COLOR_BLUE), home_panel_font_get());

  g_proactive_accept_button = make_action_button(
    g_content, "仅本次执行", 30, 444, 136);
  lv_obj_remove_event_cb(g_proactive_accept_button, action_clicked);
  lv_obj_add_event_cb(g_proactive_accept_button, proactive_feedback_clicked,
                      LV_EVENT_CLICKED,
                      (void *)(uintptr_t)HOME_PROACTIVE_ACCEPT);
  g_proactive_automation_button = make_action_button(
    g_content, "设为自动", 176, 444, 136);
  lv_obj_remove_event_cb(g_proactive_automation_button, action_clicked);
  lv_obj_add_event_cb(g_proactive_automation_button,
                      proactive_feedback_clicked, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)
                        HOME_PROACTIVE_ENABLE_AUTOMATION);
  g_proactive_ignore_button = make_action_button(
    g_content, "仅今天忽略", 322, 444, 136);
  style_secondary_action(g_proactive_ignore_button);
  lv_obj_remove_event_cb(g_proactive_ignore_button, action_clicked);
  lv_obj_add_event_cb(g_proactive_ignore_button, proactive_feedback_clicked,
                      LV_EVENT_CLICKED,
                      (void *)(uintptr_t)HOME_PROACTIVE_IGNORE_TODAY);
  g_proactive_less_button = make_action_button(
    g_content, "以后少提醒", 468, 444, 136);
  style_secondary_action(g_proactive_less_button);
  lv_obj_remove_event_cb(g_proactive_less_button, action_clicked);
  lv_obj_add_event_cb(g_proactive_less_button, proactive_feedback_clicked,
                      LV_EVENT_CLICKED,
                      (void *)(uintptr_t)HOME_PROACTIVE_LESS_OFTEN);
  g_proactive_feedback_label = make_label(
    g_content, "自动化仅由本机确认后启用", 620, 452,
    lv_color_hex(COLOR_MUTED), home_panel_font_get());
  lv_obj_set_width(g_proactive_feedback_label, 200);
  lv_label_set_long_mode(g_proactive_feedback_label, LV_LABEL_LONG_WRAP);
  update_proactive_widgets();
  g_status_label = g_proactive_feedback_label;
}

static void create_settings_page(void)
{
  lv_obj_t *panel;

  make_label(g_content, "设置", 30, 22, lv_color_hex(COLOR_TEXT),
             home_panel_font_get());
  make_label(g_content, "网络、账号与系统状态", 30, 54,
             lv_color_hex(COLOR_MUTED), home_panel_font_get());

  panel = lv_obj_create(g_content);
  lv_obj_set_pos(panel, 30, 104);
  lv_obj_set_size(panel, 790, 318);
  style_panel(panel);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  g_settings_network_label = make_info_row(panel, "有线网络",
                                            "有线网络未连接", 24,
                                            lv_color_hex(COLOR_ORANGE));
  g_settings_probe_label = make_info_row(panel, "互联网检测",
                                          "等待网线连接", 82,
                                          lv_color_hex(COLOR_ORANGE));
  make_info_row(panel, "地址获取", "DHCP 自动", 140,
                lv_color_hex(COLOR_TEXT));
  g_settings_account_label = make_info_row(panel, "米家账号", "未登录",
                                            198,
                                            lv_color_hex(COLOR_MUTED));
  make_info_row(panel, "状态通道",
                g_family_model.mqtt_connected ? "MQTT 实时推送" :
                (g_family_model.event_source[0] != '\0' ?
                 "云端定向回读" : "等待米家同步"),
                246, g_family_model.mqtt_connected ?
                     lv_color_hex(COLOR_GREEN) : lv_color_hex(COLOR_BLUE));
  g_status_label = make_label(g_content, "系统每分钟自动检测网络",
                              30, 458, lv_color_hex(COLOR_MUTED),
                              home_panel_font_get());
  panel = make_action_button(g_content, "重新检测网络", 628, 450, 192);
  lv_obj_remove_event_cb(panel, action_clicked);
  lv_obj_add_event_cb(panel, network_refresh_clicked, LV_EVENT_CLICKED, NULL);
  apply_network_state(g_network_state);
}

static void show_page(unsigned int page)
{
  struct home_panel_mijia_snapshot_s snapshot;
  bool creating;
  unsigned int previous_page;
  uint32_t started = lv_tick_get();

  if (page >= HOME_PAGE_COUNT)
    {
      page = 0;
    }

  if (g_pages[page] != NULL &&
      g_page_model_revisions[page] != g_family_ui_revision)
    {
      delete_page(page);
    }

  previous_page = g_current_page;
  g_current_page = page;
  creating = g_pages[page] == NULL;

  if (previous_page != page)
    {
      set_nav_selected(previous_page, false);
      set_nav_selected(page, true);
      if (g_pages[previous_page] != NULL)
        {
          lv_obj_add_flag(g_pages[previous_page], LV_OBJ_FLAG_HIDDEN);
        }
    }
  else
    {
      set_nav_selected(page, true);
    }

  if (g_pages[page] != NULL)
    {
      lv_obj_remove_flag(g_pages[page], LV_OBJ_FLAG_HIDDEN);
    }

  if (!creating)
    {
      g_content = g_pages[page];
      g_status_label = g_page_status_labels[page];
      g_settings_network_label = g_page_settings_network_labels[page];
      g_settings_probe_label = g_page_settings_probe_labels[page];
      g_settings_account_label = g_page_settings_account_labels[page];
      g_home_summary_label = g_page_home_summary_labels[page];
      if (page == PROACTIVE_PAGE)
        {
          update_proactive_widgets();
        }
      home_panel_mijia_get_snapshot(&snapshot);
      apply_mijia_snapshot(&snapshot);
      syslog(LOG_INFO, "[HOME][UI] page-switch page=%u model=%u\n",
             page, (unsigned int)g_family_model.revision);
      if (lv_tick_elaps(started) >= UI_SLOW_LOG_MS)
        {
          syslog(LOG_WARNING,
                 "[HOME][PERF] page-switch page=%u elapsed=%ums\n",
                 page, (unsigned int)lv_tick_elaps(started));
        }
      return;
    }

  g_pages[page] = lv_obj_create(g_page_host);
  lv_obj_remove_style_all(g_pages[page]);
  lv_obj_set_pos(g_pages[page], 0, 0);
  lv_obj_set_size(g_pages[page], PANEL_WIDTH - NAV_WIDTH,
                  PANEL_HEIGHT - TOPBAR_HEIGHT);
  lv_obj_set_style_bg_color(g_pages[page], lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(g_pages[page], LV_OPA_COVER, 0);
  lv_obj_clear_flag(g_pages[page], LV_OBJ_FLAG_SCROLLABLE);
  g_content = g_pages[page];
  g_device_binding_counts[page] = 0;
  memset(g_device_bindings[page], 0, sizeof(g_device_bindings[page]));
  memset(g_scene_bindings[page], 0, sizeof(g_scene_bindings[page]));
  g_status_label = NULL;
  g_settings_network_label = NULL;
  g_settings_probe_label = NULL;
  g_settings_account_label = NULL;
  g_home_summary_label = NULL;
  syslog(LOG_INFO, "[HOME][UI] page-render begin page=%u model=%u\n",
         page, (unsigned int)g_family_model.revision);

  if (page == 0)
    {
      create_home_page();
    }
  else if (page == 1)
    {
      create_rooms_page();
    }
  else if (page == 2)
    {
      create_scenes_page();
    }
  else if (page == PROACTIVE_PAGE)
    {
      create_proactive_page();
    }
  else
    {
      create_settings_page();
    }

  g_page_status_labels[page] = g_status_label;
  g_page_settings_network_labels[page] = g_settings_network_label;
  g_page_settings_probe_labels[page] = g_settings_probe_label;
  g_page_settings_account_labels[page] = g_settings_account_label;
  g_page_home_summary_labels[page] = g_home_summary_label;
  g_page_model_revisions[page] = g_family_ui_revision;
  home_panel_mijia_get_snapshot(&snapshot);
  apply_mijia_snapshot(&snapshot);
  syslog(LOG_INFO, "[HOME][UI] page-render end page=%u model=%u\n",
         page, (unsigned int)g_family_model.revision);
  if (lv_tick_elaps(started) >= UI_SLOW_LOG_MS)
    {
      syslog(LOG_WARNING,
             "[HOME][PERF] page-render page=%u elapsed=%ums\n",
             page, (unsigned int)lv_tick_elaps(started));
    }
}

static void delete_page(unsigned int page)
{
  if (page >= HOME_PAGE_COUNT)
    {
      return;
    }

  if (g_pages[page] != NULL)
    {
      lv_obj_delete(g_pages[page]);
      g_pages[page] = NULL;
    }

  g_page_status_labels[page] = NULL;
  g_page_settings_network_labels[page] = NULL;
  g_page_settings_probe_labels[page] = NULL;
  g_page_settings_account_labels[page] = NULL;
  g_page_home_summary_labels[page] = NULL;
  g_page_model_revisions[page] = UINT32_MAX;
  g_device_binding_counts[page] = 0;
  memset(g_device_bindings[page], 0, sizeof(g_device_bindings[page]));
  memset(g_scene_bindings[page], 0, sizeof(g_scene_bindings[page]));

  if (page == 1)
    {
      memset(g_room_buttons, 0, sizeof(g_room_buttons));
      memset(g_room_device_hosts, 0, sizeof(g_room_device_hosts));
      memset(g_room_visible_counts, 0, sizeof(g_room_visible_counts));
      g_room_highlighted = HOME_PANEL_MAX_ROOMS;
      g_room_title_label = NULL;
      g_room_summary_label = NULL;
      g_room_device_host = NULL;
      g_room_detail_host = NULL;
      reset_detail_bindings();
    }

  if (page == PROACTIVE_PAGE)
    {
      g_proactive_mode_label = NULL;
      g_proactive_history_label = NULL;
      g_proactive_time_label = NULL;
      g_proactive_confidence_label = NULL;
      g_proactive_progress_label = NULL;
      g_proactive_suggestion_title = NULL;
      g_proactive_reason_label = NULL;
      g_proactive_action_label = NULL;
      g_proactive_feedback_label = NULL;
      g_proactive_accept_button = NULL;
      g_proactive_automation_button = NULL;
      g_proactive_ignore_button = NULL;
      g_proactive_less_button = NULL;
      g_proactive_return_button = NULL;
      g_proactive_reset_button = NULL;
    }

  if (g_current_page == page)
    {
      g_content = NULL;
      g_status_label = NULL;
      g_settings_network_label = NULL;
      g_settings_probe_label = NULL;
      g_settings_account_label = NULL;
      g_home_summary_label = NULL;
    }
}

static bool device_structure_changed(
  const struct home_panel_device_s *before,
  const struct home_panel_device_s *after)
{
  unsigned int index;

  if (strcmp(before->did, after->did) != 0 ||
      strcmp(before->name, after->name) != 0 ||
      strcmp(before->room, after->room) != 0 ||
      strcmp(before->model, after->model) != 0 ||
      strcmp(before->type, after->type) != 0 ||
      before->has_power != after->has_power ||
      before->power_writable != after->power_writable ||
      before->power_siid != after->power_siid ||
      before->power_piid != after->power_piid ||
      before->has_brightness != after->has_brightness ||
      before->has_temperature != after->has_temperature ||
      before->has_humidity != after->has_humidity ||
      before->has_battery != after->has_battery ||
      before->control_count != after->control_count)
    {
      return true;
    }

  for (index = 0; index < after->control_count; index++)
    {
      const struct home_panel_control_s *left = &before->controls[index];
      const struct home_panel_control_s *right = &after->controls[index];

      if (strcmp(left->name, right->name) != 0 ||
          left->siid != right->siid || left->piid != right->piid ||
          left->type != right->type ||
          left->has_range != right->has_range ||
          left->minimum != right->minimum ||
          left->maximum != right->maximum || left->step != right->step)
        {
          return true;
        }
    }

  return false;
}

static bool family_structure_changed(
  const struct home_panel_family_model_s *before,
  const struct home_panel_family_model_s *after)
{
  unsigned int index;

  if (before->device_count != after->device_count ||
      before->room_count != after->room_count ||
      before->scene_count != after->scene_count ||
      before->mqtt_connected != after->mqtt_connected ||
      strcmp(before->event_source, after->event_source) != 0)
    {
      return true;
    }

  for (index = 0; index < after->device_count; index++)
    {
      if (device_structure_changed(&before->devices[index],
                                   &after->devices[index]))
        {
          return true;
        }
    }

  for (index = 0; index < after->room_count; index++)
    {
      if (strcmp(before->rooms[index].name, after->rooms[index].name) != 0 ||
          before->rooms[index].device_count !=
          after->rooms[index].device_count)
        {
          return true;
        }
    }

  for (index = 0; index < after->scene_count; index++)
    {
      if (strcmp(before->scenes[index].id, after->scenes[index].id) != 0 ||
          strcmp(before->scenes[index].name,
                 after->scenes[index].name) != 0)
        {
          return true;
        }
    }

  return false;
}

static void update_device_binding(
  struct home_panel_device_binding_s *binding)
{
  const struct home_panel_device_s *device = binding->device;
  bool checked;
  bool current_checked;
  bool disabled;
  bool state_text_changed;
  char value[32];

  if (device == NULL || binding->value_label == NULL ||
      binding->state_label == NULL)
    {
      return;
    }

  checked = device->has_power && device->power;
  format_device_value(device, value, sizeof(value));
  set_label_text_if_changed(binding->value_label, value);
  state_text_changed = set_label_text_if_changed(
    binding->state_label, device_state_text(device));

  if (binding->button == NULL)
    {
      if (state_text_changed)
        {
          lv_obj_set_style_text_color(
            binding->state_label,
            lv_color_hex(device->online ? COLOR_GREEN : COLOR_MUTED), 0);
        }
      return;
    }

  current_checked = lv_obj_has_state(binding->button, LV_STATE_CHECKED);
  if (state_text_changed || checked != current_checked)
    {
      lv_obj_set_style_text_color(
        binding->state_label,
        lv_color_hex(checked ? COLOR_GREEN : COLOR_MUTED), 0);
    }

  if (checked != current_checked)
    {
      style_toggle(binding->button, checked);
      if (checked)
        {
          lv_obj_add_state(binding->button, LV_STATE_CHECKED);
        }
      else
        {
          lv_obj_remove_state(binding->button, LV_STATE_CHECKED);
        }
    }

  disabled = lv_obj_has_state(binding->button, LV_STATE_DISABLED);
  if (device->online && disabled)
    {
      lv_obj_remove_state(binding->button, LV_STATE_DISABLED);
    }
  else if (!device->online && !disabled)
    {
      lv_obj_add_state(binding->button, LV_STATE_DISABLED);
    }
}

static void update_control_binding(
  struct home_panel_control_binding_s *binding)
{
  const struct home_panel_control_s *property = binding->property;
  bool disabled;
  char value[24];

  if (binding->device == NULL || property == NULL ||
      binding->control == NULL || binding->value_label == NULL)
    {
      return;
    }

  if (property->type == HOME_PANEL_CONTROL_BOOLEAN)
    {
      bool checked = lv_obj_has_state(binding->control, LV_STATE_CHECKED);

      set_label_text_if_changed(binding->value_label,
                                property->has_value ?
                                  (property->boolean_value ? "已开启" :
                                                             "已关闭") :
                                  "状态未知");
      if (checked != property->boolean_value)
        {
          style_toggle(binding->control, property->boolean_value);
          if (property->boolean_value)
            {
              lv_obj_add_state(binding->control, LV_STATE_CHECKED);
            }
          else
            {
              lv_obj_remove_state(binding->control, LV_STATE_CHECKED);
            }
        }
    }
  else
    {
      if (!lv_obj_has_state(binding->control, LV_STATE_PRESSED))
        {
          lv_slider_set_value(binding->control, property->value,
                              LV_ANIM_OFF);
          format_control_value(property, property->value,
                               value, sizeof(value));
          set_label_text_if_changed(binding->value_label,
                                    property->has_value ? value : "--");
        }
    }

  disabled = lv_obj_has_state(binding->control, LV_STATE_DISABLED);
  if (binding->device->online && disabled)
    {
      lv_obj_remove_state(binding->control, LV_STATE_DISABLED);
    }
  else if (!binding->device->online && !disabled)
    {
      lv_obj_add_state(binding->control, LV_STATE_DISABLED);
    }
}

static void update_model_widgets(void)
{
  const struct home_panel_room_s *environment = NULL;
  const struct home_panel_device_s *detail = NULL;
  unsigned int page;
  unsigned int index;
  char text[96];

  for (page = 0; page < HOME_PAGE_COUNT; page++)
    {
      for (index = 0; index < g_device_binding_counts[page]; index++)
        {
          update_device_binding(&g_device_bindings[page][index]);
        }
    }

  for (index = 0; index < g_control_binding_count; index++)
    {
      update_control_binding(&g_control_bindings[index]);
    }

  if (g_room_detail_host != NULL &&
      g_selected_device < g_family_model.device_count)
    {
      detail = &g_family_model.devices[g_selected_device];
      snprintf(text, sizeof(text), "%s · %s",
               device_type_text(detail), detail->online ? "在线" : "离线");
      if (set_label_text_if_changed(g_detail_subtitle_label, text))
        {
          lv_obj_set_style_text_color(
            g_detail_subtitle_label,
            lv_color_hex(detail->online ? COLOR_GREEN : COLOR_MUTED), 0);
        }
      if (g_detail_temperature_label != NULL)
        {
          snprintf(text, sizeof(text), "%d°C", detail->temperature);
          set_label_text_if_changed(g_detail_temperature_label, text);
        }
      if (g_detail_humidity_label != NULL)
        {
          snprintf(text, sizeof(text), "%d%%", detail->humidity);
          set_label_text_if_changed(g_detail_humidity_label, text);
        }
      if (g_detail_battery_label != NULL)
        {
          snprintf(text, sizeof(text), "%d%%", detail->battery);
          set_label_text_if_changed(g_detail_battery_label, text);
        }
    }

  for (index = 0; index < g_family_model.room_count; index++)
    {
      if (g_family_model.rooms[index].has_temperature)
        {
          environment = &g_family_model.rooms[index];
          if (strcmp(environment->name, "客厅") == 0)
            {
              break;
            }
        }
    }

  if (g_page_home_summary_labels[0] != NULL)
    {
      if (environment != NULL)
        {
          snprintf(text, sizeof(text), "%s %d°C  ·  湿度 %d%%",
                   environment->name, environment->temperature,
                   environment->humidity);
        }
      else
        {
          snprintf(text, sizeof(text), "环境传感器暂无数据");
        }
      set_label_text_if_changed(g_page_home_summary_labels[0], text);
    }

  if (g_page_status_labels[0] != NULL)
    {
      snprintf(text, sizeof(text), "%u/%u 台在线",
               g_family_model.online_count, g_family_model.device_count);
      set_label_text_if_changed(g_page_status_labels[0], text);
    }

  if (g_room_detail_host == NULL && g_room_summary_label != NULL &&
      g_selected_room < g_family_model.room_count)
    {
      const struct home_panel_room_s *room =
        &g_family_model.rooms[g_selected_room];

      if (room->has_temperature && room->has_humidity)
        {
          snprintf(text, sizeof(text), "%u 台设备 · %d°C · 湿度 %d%%",
                   room->device_count, room->temperature, room->humidity);
        }
      else
        {
          snprintf(text, sizeof(text), "%u 台设备", room->device_count);
        }
      set_label_text_if_changed(g_room_summary_label, text);
    }
}

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
  static const char *const blocked_types[] =
  {
    "camera", "lock", "cateye", "sensor", "alarm", "smoke", "gas"
  };
  unsigned int index;

  if (device == NULL)
    {
      return false;
    }

  for (index = 0;
       index < sizeof(blocked_types) / sizeof(blocked_types[0]); index++)
    {
      if (strstr(device->type, blocked_types[index]) != NULL ||
          strstr(device->model, blocked_types[index]) != NULL)
        {
          return false;
        }
    }
  return true;
}

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
  if (command_match)
    {
      if (g_proactive_command.automation)
        {
          home_proactive_automation_result(true);
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

static void proactive_observe_model_changes(
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

static int refresh_family_model(
  const struct home_panel_mijia_family_snapshot_s *snapshot,
  const struct home_panel_family_model_s *model)
{
  bool changed;
  bool structure_changed;

  if (snapshot == NULL || model == NULL || snapshot->revision == 0 ||
      snapshot->json_size == 0 || model->revision != snapshot->revision)
    {
      return -EAGAIN;
    }

  changed = !g_family_model_valid ||
            memcmp((const char *)&g_family_model +
                   sizeof(g_family_model.revision),
                   (const char *)model + sizeof(model->revision),
                   sizeof(*model) - sizeof(model->revision)) != 0;
  if (changed)
    {
      structure_changed = !g_family_model_valid ||
        family_structure_changed(&g_family_model, model);
      if (g_family_model_valid)
        {
          proactive_observe_model_changes(&g_family_model, model);
        }
      memcpy(&g_family_model, model, sizeof(g_family_model));
      g_family_model_valid = true;
      if (structure_changed)
        {
          g_family_ui_revision++;
          if (g_family_ui_revision == 0)
            {
              g_family_ui_revision = 1;
            }
        }
      else
        {
          update_model_widgets();
        }
      syslog(LOG_INFO,
             "[HOME][MODEL] revision=%u devices=%u online=%u rooms=%u "
             "scenes=%u event=%s mqtt=%u update=%s\n",
             (unsigned int)model->revision, model->device_count,
             model->online_count, model->room_count, model->scene_count,
             model->event_source, model->mqtt_connected ? 1 : 0,
             structure_changed ? "rebuild" : "in-place");
      return structure_changed ? 0 : 2;
    }

  g_family_model.revision = model->revision;
  syslog(LOG_INFO,
         "[HOME][MODEL] revision=%u display-unchanged\n",
         (unsigned int)model->revision);
  return 1;
}

static void create_home_screen(void)
{
  lv_obj_t *screen = lv_screen_active();
  lv_obj_t *topbar;
  lv_obj_t *nav;
  lv_obj_t *login;

  lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  topbar = lv_obj_create(screen);
  lv_obj_remove_style_all(topbar);
  lv_obj_set_pos(topbar, 0, 0);
  lv_obj_set_size(topbar, PANEL_WIDTH, TOPBAR_HEIGHT);
  lv_obj_set_style_bg_color(topbar, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_bg_opa(topbar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(topbar, 1, 0);
  lv_obj_set_style_border_side(topbar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(topbar, lv_color_hex(COLOR_BORDER), 0);

  g_clock_label = make_label(topbar, "--:--", 24, 17,
                             lv_color_hex(COLOR_TEXT),
                             &home_panel_digits_28);
  g_date_label = make_label(topbar, "等待网络校时", 132, 26,
                            lv_color_hex(COLOR_MUTED),
                            home_panel_font_get());
  g_network_label = make_label(topbar,
                               LV_SYMBOL_WARNING "  有线网络未连接",
                               650, 26, lv_color_hex(COLOR_ORANGE),
                               home_panel_font_get());

  login = lv_button_create(topbar);
  configure_fast_button(login);
  lv_obj_set_size(login, 118, 44);
  lv_obj_set_pos(login, 886, 14);
  lv_obj_set_style_radius(login, UI_RADIUS, 0);
  lv_obj_set_style_shadow_width(login, 0, 0);
  lv_obj_set_style_border_width(login, 1, 0);
  lv_obj_set_style_border_color(login, lv_color_hex(0x7bb4ff), 0);
  lv_obj_set_style_bg_color(login, lv_color_hex(COLOR_BLUE), 0);
  lv_obj_set_style_bg_color(login, lv_color_hex(0x478ee8),
                            LV_STATE_PRESSED);
  lv_obj_add_event_cb(login, show_login, LV_EVENT_CLICKED, NULL);
  g_login_top_label = lv_label_create(login);
  lv_label_set_text(g_login_top_label, "登录米家");
  set_chinese_font(g_login_top_label);
  lv_obj_center(g_login_top_label);

  nav = lv_obj_create(screen);
  lv_obj_remove_style_all(nav);
  lv_obj_set_pos(nav, 0, TOPBAR_HEIGHT);
  lv_obj_set_size(nav, NAV_WIDTH, PANEL_HEIGHT - TOPBAR_HEIGHT);
  lv_obj_set_style_bg_color(nav, lv_color_hex(COLOR_NAV), 0);
  lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);

  g_nav_buttons[0] = make_nav_button(nav, LV_SYMBOL_HOME, "家庭", 20, 0);
  g_nav_buttons[1] = make_nav_button(nav, LV_SYMBOL_LIST, "房间", 84, 1);
  g_nav_buttons[2] = make_nav_button(nav, LV_SYMBOL_PLAY, "场景", 148, 2);
  g_nav_buttons[3] = make_nav_button(nav, LV_SYMBOL_REFRESH, "智能", 212, 3);
  g_nav_buttons[4] = make_nav_button(nav, LV_SYMBOL_SETTINGS, "设置", 432, 4);

  g_page_host = lv_obj_create(screen);
  lv_obj_remove_style_all(g_page_host);
  lv_obj_set_pos(g_page_host, NAV_WIDTH, TOPBAR_HEIGHT);
  lv_obj_set_size(g_page_host, PANEL_WIDTH - NAV_WIDTH,
                  PANEL_HEIGHT - TOPBAR_HEIGHT);
  lv_obj_set_style_bg_color(g_page_host, lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(g_page_host, LV_OPA_COVER, 0);
  lv_obj_clear_flag(g_page_host, LV_OBJ_FLAG_SCROLLABLE);
  show_page(0);
  update_time_ui(true);
}

static void log_fpu_state(void)
{
#ifdef CONFIG_ARCH_FPU
  uint32_t mstatus;
  uint32_t mexstatus;
  uint32_t fcsr;

  __asm__ __volatile__("csrr %0, mstatus" : "=r"(mstatus));
  __asm__ __volatile__("csrr %0, 0x7e1" : "=r"(mexstatus));
  __asm__ __volatile__("csrr %0, fcsr" : "=r"(fcsr));
  syslog(LOG_INFO,
         "[HOME][FPU] mstatus=%08lx fs=%lu fcsr=%08lx mexstatus=%08lx "
         "spush=%lu spswap=%lu\n",
         (unsigned long)mstatus, (unsigned long)((mstatus >> 13) & 3u),
         (unsigned long)fcsr, (unsigned long)mexstatus,
         (unsigned long)((mexstatus >> 16) & 1u),
         (unsigned long)((mexstatus >> 17) & 1u));
#endif
}

int main(int argc, char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  enum network_state_e displayed_state = (enum network_state_e)-1;
  struct home_panel_mijia_snapshot_s mijia_snapshot;
  struct home_panel_mijia_family_snapshot_s family_snapshot;
  struct home_panel_mijia_command_snapshot_s command_snapshot;
  struct home_panel_agent_snapshot_s agent_snapshot;
  uint32_t displayed_mijia_revision = UINT32_MAX;
  uint32_t displayed_family_revision = 0;
  uint32_t displayed_command_revision = 0;
  uint32_t last_slow_refresh_log = 0;
  uint32_t last_mijia_poll = 0;
  uint32_t last_proactive_tick = 0;
  uint32_t last_proactive_ui_poll = 0;
  uint32_t last_proactive_cloud_poll = 0;
  uint32_t last_proactive_context = 0;
  struct sched_param ui_param;
  int ui_policy;
  int ret;

  (void)argc;
  (void)argv;
  memset(&mijia_snapshot, 0, sizeof(mijia_snapshot));

  if (lv_is_initialized())
    {
      fprintf(stderr, "home_panel: LVGL is already initialized\n");
      return 1;
    }

  if (pthread_getschedparam(pthread_self(), &ui_policy, &ui_param) == 0)
    {
      ui_param.sched_priority = UI_THREAD_PRIORITY;
      ret = pthread_setschedparam(pthread_self(), ui_policy, &ui_param);
      if (ret != 0)
        {
          syslog(LOG_WARNING,
                 "[HOME][PERF] UI priority unchanged ret=%d\n", ret);
        }
    }

  log_fpu_state();
  lv_init();
  ret = home_panel_font_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "[HOME][FONT] using built-in subset fallback ret=%d\n", ret);
    }
  load_proactive_profile();
  update_proactive_context();

  lv_nuttx_dsc_init(&info);
#ifdef CONFIG_INPUT_TOUCHSCREEN
  info.input_path = CONFIG_D13X_HOME_PANEL_INPUT_DEVPATH;
#endif
  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      fprintf(stderr, "home_panel: failed to initialize /dev/fb0\n");
      lv_deinit();
      return 1;
    }

  if (result.indev == NULL)
    {
      fprintf(stderr, "home_panel: failed to initialize %s\n",
              CONFIG_D13X_HOME_PANEL_INPUT_DEVPATH);
      lv_nuttx_deinit(&result);
      lv_deinit();
      return 1;
    }

  lv_indev_set_display(result.indev, result.disp);

  create_home_screen();
  ret = start_network_monitor();
  if (ret != 0)
    {
      fprintf(stderr, "home_panel: network monitor failed: %d\n", ret);
    }

  ret = home_panel_mijia_initialize();
  if (ret != 0)
    {
      fprintf(stderr, "home_panel: Mijia client failed: %d\n", ret);
    }

  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(result.disp);

  for (;;)
    {
      uint32_t delay;

      if (displayed_state != g_network_state)
        {
          displayed_state = g_network_state;
          apply_network_state(displayed_state);
          syslog(LOG_INFO, "[HOME][UI] network=%s\n",
                 network_state_name(displayed_state));
        }

      update_time_ui(false);
      if (last_proactive_tick == 0 ||
          lv_tick_elaps(last_proactive_tick) >= PROACTIVE_TICK_MS)
        {
          last_proactive_tick = lv_tick_get();
          home_proactive_tick(last_proactive_tick);
          process_proactive_automation();
        }
      if (proactive_persist_retry_ready())
        {
          schedule_proactive_persist();
        }
      if (last_proactive_context == 0 ||
          lv_tick_elaps(last_proactive_context) >= 1000)
        {
          last_proactive_context = lv_tick_get();
          update_proactive_context();
        }
      if (g_proactive_mode_label != NULL &&
          (last_proactive_ui_poll == 0 ||
           lv_tick_elaps(last_proactive_ui_poll) >= PROACTIVE_UI_POLL_MS))
        {
          struct home_proactive_snapshot_s proactive_snapshot;

          last_proactive_ui_poll = lv_tick_get();
          home_proactive_get_snapshot(&proactive_snapshot);
          home_panel_mijia_get_agent_snapshot(&agent_snapshot);
          if (g_displayed_proactive_revision !=
                proactive_snapshot.revision ||
              g_displayed_agent_revision != agent_snapshot.revision)
            {
              update_proactive_widgets();
            }
        }

      if (last_mijia_poll == 0 ||
          lv_tick_elaps(last_mijia_poll) >= MIJIA_UI_POLL_MS)
        {
          last_mijia_poll = lv_tick_get();
          home_panel_mijia_get_snapshot(&mijia_snapshot);
          if (displayed_mijia_revision != mijia_snapshot.revision)
            {
              displayed_mijia_revision = mijia_snapshot.revision;
              apply_mijia_snapshot(&mijia_snapshot);
              syslog(LOG_INFO, "[HOME][UI] mijia=%s\n",
                     home_panel_mijia_state_name(mijia_snapshot.state));
            }

          if (!g_login_autostart_attempted &&
              g_network_state == NETWORK_ONLINE &&
              mijia_snapshot.state == HOME_PANEL_MIJIA_IDLE)
            {
              g_login_autostart_attempted = true;
              syslog(LOG_INFO,
                     "[HOME][UI] no persisted Mijia session; opening login\n");
              show_login(NULL);
            }

          ret = home_panel_mijia_get_family_update(
            displayed_family_revision, &family_snapshot,
            &g_family_update_model);
          if (ret == 0)
            {
              ret = refresh_family_model(&family_snapshot,
                                         &g_family_update_model);
              if (ret >= 0)
                {
                  displayed_family_revision = family_snapshot.revision;
                  if (ret == 0)
                    {
                      g_model_refresh_pending = true;
                    }
                }
            }

          home_panel_mijia_get_command_snapshot(&command_snapshot);
          if (displayed_command_revision != command_snapshot.revision)
            {
              displayed_command_revision = command_snapshot.revision;
              if (g_status_label != NULL && command_snapshot.revision != 0)
                {
                  lv_label_set_text(g_status_label,
                                    command_snapshot.message);
                  lv_obj_set_style_text_color(
                    g_status_label,
                    lv_color_hex(command_snapshot.state ==
                                 HOME_PANEL_MIJIA_COMMAND_CONFIRMED ?
                                 COLOR_GREEN :
                                 command_snapshot.state ==
                                 HOME_PANEL_MIJIA_COMMAND_ERROR ?
                                 COLOR_ORANGE : COLOR_BLUE), 0);
                }
              syslog(LOG_INFO, "[HOME][UI] command=%u code=%d\n",
                     (unsigned int)command_snapshot.state,
                     command_snapshot.code);
            }
        }

      if (last_proactive_cloud_poll == 0 ||
          lv_tick_elaps(last_proactive_cloud_poll) >=
            PROACTIVE_CLOUD_POLL_MS)
        {
          struct home_proactive_snapshot_s proactive_snapshot;

          last_proactive_cloud_poll = lv_tick_get();
          home_proactive_get_snapshot(&proactive_snapshot);
          request_proactive_cloud_learning(&proactive_snapshot,
                                           mijia_snapshot.state);
          request_proactive_cloud_analysis(&proactive_snapshot,
                                           mijia_snapshot.state);
        }

      if (g_model_refresh_pending &&
          lv_indev_get_state(result.indev) == LV_INDEV_STATE_RELEASED)
        {
          g_model_refresh_pending = false;
          show_page(g_current_page);
        }

      {
        uint32_t refresh_started = lv_tick_get();
        uint32_t refresh_elapsed;

        delay = lv_timer_handler();
        refresh_elapsed = lv_tick_elaps(refresh_started);
        if (refresh_elapsed >= UI_SLOW_LOG_MS &&
            (last_slow_refresh_log == 0 ||
             lv_tick_elaps(last_slow_refresh_log) >= 1000))
          {
            last_slow_refresh_log = lv_tick_get();
            syslog(LOG_WARNING,
                   "[HOME][PERF] lv-refresh page=%u elapsed=%ums\n",
                   g_current_page, (unsigned int)refresh_elapsed);
          }
      }
      if (delay == LV_NO_TIMER_READY || delay > UI_LOOP_MAX_DELAY_MS)
        {
          delay = UI_LOOP_MAX_DELAY_MS;
        }

      usleep((delay > 0 ? delay : 1) * 1000);
    }

  return 0;
}
