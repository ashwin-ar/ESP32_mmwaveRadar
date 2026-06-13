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
#include <unistd.h>
#include <sys/param.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_err.h"

// extern void example_lvgl_demo_ui(lv_obj_t *scr);

// void app_main(void)
// {
//     bsp_display_start();

//     ESP_LOGI("example", "Display LVGL animation");
//     bsp_display_lock(0);
//     lv_obj_t *scr = lv_disp_get_scr_act(NULL);
// #if (CONFIG_ESP_LCD_TOUCH_MAX_POINTS > 1 && CONFIG_LV_USE_GESTURE_RECOGNITION)
//     lv_indev_t *indev = bsp_display_get_input_dev();
//     lv_indev_set_rotation_rad_threshold(indev, 0.15f);
// #endif
//     example_lvgl_demo_ui(scr);

//     bsp_display_unlock();
// }


static const char *TAG = "example";

// LVGL library is not thread-safe, this example will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Refresh Rate = 18000000/(1+40+20+800)/(1+10+5+480) = 42Hz
#define EXAMPLE_LCD_H_RES              480
#define EXAMPLE_LCD_V_RES              480

// @note: pin def is not required

#if CONFIG_EXAMPLE_USE_DOUBLE_FB
#define EXAMPLE_LCD_NUM_FB             2
#else
#define EXAMPLE_LCD_NUM_FB             1
#endif // CONFIG_EXAMPLE_USE_DOUBLE_FB

#define EXAMPLE_DATA_BUS_WIDTH         16
#define EXAMPLE_PIXEL_SIZE             2
#define EXAMPLE_LV_COLOR_FORMAT        LV_COLOR_FORMAT_RGB565

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your Application ///////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define EXAMPLE_LVGL_DRAW_BUF_LINES    50 // number of display lines in each draw buffer
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (5 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ

extern void example_lvgl_demo_ui(lv_display_t *disp);

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // pass the draw buffer to the driver
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of task watch dog timeout
        time_till_next_ms = MAX(time_till_next_ms, EXAMPLE_LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, EXAMPLE_LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing Board Support Package Display...");

    /* 1. This single call sets up the 3-wire SPI, configures the high-speed RGB panel, 
          allocates the PSRAM buffers, and registers the display to LVGL automatically. */
    lv_display_t *display = bsp_display_start();
    
    /* 2. Turn on the backlight panel */
    bsp_display_backlight_on();

    ESP_LOGI(TAG, "Display fully initialized by BSP. Injecting custom UI...");

    /* 3. Lock the display before modifying visual objects.
          The BSP implements safe background threads to continuously flush 
          the display panel; locking prevents rendering overlaps. */
    bsp_display_lock(0);

    // Call your custom UI layout function
    example_lvgl_demo_ui(display);
    
    // Release the lock so the rendering thread can paint the panel
    bsp_display_unlock();

    ESP_LOGI(TAG, "UI setup complete. Entering background idle loop.");
    
    /* 4. Keep app_main alive. 
          The BSP already spins up its own background tasks to increment 
          the LVGL tick timer and run lv_timer_handler(). */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
