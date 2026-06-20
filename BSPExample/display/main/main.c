/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief BSP Display Example
 * @details Show an image on the screen with a simple startup animation (LVGL)
 * @example https://espressif.github.io/esp-launchpad/?flashConfigURL=https://espressif.github.io/esp-bsp/config.toml&app=display-
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"

extern void lv_example_chart_scatter(void);
extern void chart_add_custom_point(int32_t, int32_t, lv_color_t);

void app_main(void)
{
    bsp_display_start();

    ESP_LOGI("example", "Display LVGL animation");
    bsp_display_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
#if (CONFIG_ESP_LCD_TOUCH_MAX_POINTS > 1 && CONFIG_LV_USE_GESTURE_RECOGNITION)
    lv_indev_t *indev = bsp_display_get_input_dev();
    lv_indev_set_rotation_rad_threshold(indev, 0.15f);
#endif
    lv_example_chart_scatter();
    chart_add_custom_point(25, 45, lv_palette_main(LV_PALETTE_GREEN));
    chart_add_custom_point(-70, -10, lv_color_hex(0xFF5733)); // Custom Hex Orange
    chart_add_custom_point(50, -85, lv_color_black());

    bsp_display_unlock();
    //@note: removed BLK, as there is no backight here.  
}
