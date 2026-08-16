# D13x Hengshan-Pi home control panel

if(CONFIG_D13X_HOME_PANEL)
  nuttx_add_application(
    NAME home_panel
    SRCS home_panel_main.c home_panel_mijia_client.c
         home_panel_mijia_model.c home_panel_proactive.c
         home_panel_proactive_controller.c
         ui/home_ui_theme.c ui/home_ui_components.c
         ui/page_home.c ui/page_rooms.c ui/page_proactive.c
         ui/page_settings.c ui/page_scenes.c
         home_panel_font.c home_panel_misans_18.c
         home_panel_digits_28.c
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/ui
    PRIORITY ${CONFIG_D13X_HOME_PANEL_PRIORITY}
    STACKSIZE ${CONFIG_D13X_HOME_PANEL_STACKSIZE})
endif()
