# idf_component_register(SRCS "main.c"
#                        INCLUDE_DIRS ".")
set(SOURCES
    main.c
    
    # HAL - 核心驱动
    hal/wifi_hal.c
    
    # Service - 核心服务
    services/event_loop_service.c
    services/wifi_service.c
    services/sntp_service.c
    
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