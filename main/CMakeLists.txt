# idf_component_register(SRCS "main.c"
#                        INCLUDE_DIRS ".")

set(SOURCES
    main.c
    
    # HAL - 核心驱动
    hal/font_alipuhui20.c
    #hal/lcd_hal.c
    hal/it7259_hal.c
    hal/lvgl_hal.c
    #hal/touch_hal.c
    hal/wifi_hal.c
    #hal/keys_hal.c
    
    # Service - 核心服务
    services/power_service.c
    services/event_loop_service.c
    services/wifi_service.c
    services/sntp_service.c
    services/ui_service.c
    
    # App 层
)

set(INCLUDE_DIRS
    .
    hal
    services
    app
    config
)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS ${INCLUDE_DIRS})