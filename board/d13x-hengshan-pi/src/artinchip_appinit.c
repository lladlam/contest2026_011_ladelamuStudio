/****************************************************************************
 * vendor/artinchip/boards/d13x-hengshan-pi/src/artinchip_appinit.c
 *
 * D13x Hengshan Pi 应用初始化
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <nuttx/board.h>

#include <errno.h>
#include <sched.h>
#include <syslog.h>

#ifdef CONFIG_D13X_HOME_PANEL
int home_panel_main(int argc, FAR char *argv[]);
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   应用层初始化
 *
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
#ifdef CONFIG_D13X_HOME_PANEL
  int pid;

  pid = task_create("home_panel", CONFIG_D13X_HOME_PANEL_PRIORITY,
                    CONFIG_D13X_HOME_PANEL_STACKSIZE,
                    home_panel_main, NULL);
  if (pid < 0)
    {
      syslog(LOG_ERR, "[HOME][BOOT] autostart failed: %d\n", errno);
      return -errno;
    }

  syslog(LOG_INFO, "[HOME][BOOT] autostart pid=%d\n", pid);
#endif

  return 0;
}
