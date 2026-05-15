#pragma once

esp_err_t sd_hal_read_file(const char *path);
esp_err_t sd_hal_write_file(const char *path, char *data);
esp_err_t sd_hal_list_content(const char *path);
esp_err_t sd_hal_init(void);