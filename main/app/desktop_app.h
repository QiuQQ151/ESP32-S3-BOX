// desktop_app.h
#ifndef DESKTOP_APP_H
#define DESKTOP_APP_H

#include "ui_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册桌面应用到 UI 服务
 * 
 * 该函数在 UI 服务初始化时调用，用于将桌面应用加入到应用列表中。
 */
void desktop_app_register(void);

#ifdef __cplusplus
}
#endif

#endif // DESKTOP_APP_H