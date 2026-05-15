# idf_component_register(SRCS "main.c"
#                        INCLUDE_DIRS ".")

set(SOURCES
    main.c
    # component

    
    # HAL - 核心驱动
    # hal/tca9535_hal.c  # IO扩展
   
    # hal/it7259_hal.c  # LCD触摸
    # hal/lvgl_hal.c  # LCD
    hal/wifi_hal.c  # wifi
    # hal/keys_hal.c  # 按键
    # hal/sd_hal.c #SD卡
    # hal/power_io_hal.c #电源使能
    
    # Service - 核心服务
    # services/power_service.c  # 电源服务
    # services/event_loop_service.c # 事件转发
    services/wifi_service.c # wifi服务
    # services/sntp_service.c # 校时服务
    # services/ui_service.c   # 界面
    # services/led_service.c  # 灯效
    # services/audio_service.c  # 音频
    
    # App 层
    # app/desktop_app.c
    # app/radio_app.c

    # 附加文件
    canon.pcm
    icon/font_alipuhui20.c # 字体
    icon/icon_clock.c
    icon/icon_deepseek.c
    icon/icon_radio.c
    icon/icon_weather.c
    icon/icon_setting.c
    icon/icon_booting.c
    icon/icon_folder.c
    icon/icon_light.c
    icon/icon_music.c
    icon/icon_record.c 
    icon/icon_usb.c    
    icon/icon_tomato.c  
    icon/icon_next_40.c  
    icon/icon_play_40.c
    icon/icon_prev_40.c
    icon/icon_reflash_28.c
    icon/icon_stop_40.c
    icon/icon_wifion_15.c
    icon/icon_wifioff_15.c
)

set(INCLUDE_DIRS
    .
    hal
    services
    app
    config
)


idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS ${INCLUDE_DIRS}
                       EMBED_FILES "canon.pcm"
)