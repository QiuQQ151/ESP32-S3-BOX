#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#define BSP_SD_CLK          (47)
#define BSP_SD_CMD          (48)
#define BSP_SD_D0           (21)

static const char *TAG = "sd_hal";

#define MOUNT_POINT              "/sdcard"
#define EXAMPLE_MAX_CHAR_SIZE    64

// 写文件内容 path是路径 data是内容
esp_err_t sd_hal_write_file(const char *path, char *data)
{
    ESP_LOGI(TAG, "Opening file %s", path);
    FILE *f = fopen(path, "w");   // 以只写方式打开路径中文件
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing"); 
        return ESP_FAIL;
    }
    fprintf(f, data); // 写入内容
    fclose(f);  // 关闭文件
    ESP_LOGI(TAG, "File written");

    return ESP_OK;
}

// 读文件内容 path是路径
esp_err_t sd_hal_read_file(const char *path)
{
    ESP_LOGI(TAG, "Reading file %s", path);
    FILE *f = fopen(path, "r");  // 以只读方式打开文件
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }
    char line[EXAMPLE_MAX_CHAR_SIZE];  // 定义一个字符串数组
    fgets(line, sizeof(line), f); // 获取文件中的内容到字符串数组
    fclose(f); // 关闭文件

    // strip newline
    char *pos = strchr(line, '\n'); // 查找字符串中的“\n”并返回其位置
    if (pos) {
        *pos = '\0'; // 把\n替换成\0
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line); // 把数组内容输出到终端

    return ESP_OK;
}

// 列出目录下的所有文件
esp_err_t sd_hal_list_content(const char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Contents of '%s':", path);
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        const char *type_str = "UNKNOWN";
        if (entry->d_type == DT_REG) {
            type_str = "FILE";
        } else if (entry->d_type == DT_DIR) {
            type_str = "DIR ";
        }
        ESP_LOGI(TAG, "  [%s] %s (len=%d)", type_str, entry->d_name, (int)strlen(entry->d_name));
        count++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "Total entries: %d", count);
    return ESP_OK;
}

// 初始化
esp_err_t sd_hal_init(void)
{
    esp_err_t ret;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,   // 如果挂载不成功是否需要格式化SD卡
        .max_files = 10, // 允许打开的最大文件数
        .allocation_unit_size = 16 * 1024  // 分配单元大小
    };
    
    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");
    ESP_LOGI(TAG, "Using SDMMC peripheral");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT(); // SDMMC主机接口配置
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT(); // SDMMC插槽配置
    slot_config.width = 1;  // 设置为1线SD模式
    slot_config.clk = BSP_SD_CLK; 
    slot_config.cmd = BSP_SD_CMD;
    slot_config.d0 = BSP_SD_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP; // 打开内部上拉电阻

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card); // 挂载SD卡

    if (ret != ESP_OK) {  // 如果没有挂载成功
        if (ret == ESP_FAIL) { // 如果挂载失败
            ESP_LOGE(TAG, "Failed to mount filesystem. ");
        } else { // 如果是其它错误 打印错误名称
            ESP_LOGE(TAG, "Failed to initialize the card (%s). ", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Filesystem mounted"); // 提示挂载成功
    sdmmc_card_print_info(stdout, card); // 终端打印SD卡的一些信息

    return ESP_OK;
}