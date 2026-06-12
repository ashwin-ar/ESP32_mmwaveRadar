/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_gc9503.h"

static const char *TAG = "example";

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Refresh Rate = 18000000/(1+40+20+800)/(1+10+5+480) = 42Hz
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     (16 * 1000 * 1000)
#define EXAMPLE_LCD_H_RES              480
#define EXAMPLE_LCD_V_RES              480
#define EXAMPLE_LCD_HSYNC              4
#define EXAMPLE_LCD_HBP                8
#define EXAMPLE_LCD_HFP                8
#define EXAMPLE_LCD_VSYNC              4
#define EXAMPLE_LCD_VBP                8
#define EXAMPLE_LCD_VFP                8

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_BK_LIGHT       -1
#define EXAMPLE_PIN_NUM_DISP_EN        -1

#define EXAMPLE_PIN_NUM_HSYNC          CONFIG_EXAMPLE_LCD_HSYNC_GPIO
#define EXAMPLE_PIN_NUM_VSYNC          CONFIG_EXAMPLE_LCD_VSYNC_GPIO
#define EXAMPLE_PIN_NUM_DE             CONFIG_EXAMPLE_LCD_DE_GPIO
#define EXAMPLE_PIN_NUM_PCLK           CONFIG_EXAMPLE_LCD_PCLK_GPIO

#define EXAMPLE_PIN_NUM_DATA0          CONFIG_EXAMPLE_LCD_DATA0_GPIO
#define EXAMPLE_PIN_NUM_DATA1          CONFIG_EXAMPLE_LCD_DATA1_GPIO
#define EXAMPLE_PIN_NUM_DATA2          CONFIG_EXAMPLE_LCD_DATA2_GPIO
#define EXAMPLE_PIN_NUM_DATA3          CONFIG_EXAMPLE_LCD_DATA3_GPIO
#define EXAMPLE_PIN_NUM_DATA4          CONFIG_EXAMPLE_LCD_DATA4_GPIO
#define EXAMPLE_PIN_NUM_DATA5          CONFIG_EXAMPLE_LCD_DATA5_GPIO
#define EXAMPLE_PIN_NUM_DATA6          CONFIG_EXAMPLE_LCD_DATA6_GPIO
#define EXAMPLE_PIN_NUM_DATA7          CONFIG_EXAMPLE_LCD_DATA7_GPIO
#define EXAMPLE_PIN_NUM_DATA8          CONFIG_EXAMPLE_LCD_DATA8_GPIO
#define EXAMPLE_PIN_NUM_DATA9          CONFIG_EXAMPLE_LCD_DATA9_GPIO
#define EXAMPLE_PIN_NUM_DATA10         CONFIG_EXAMPLE_LCD_DATA10_GPIO
#define EXAMPLE_PIN_NUM_DATA11         CONFIG_EXAMPLE_LCD_DATA11_GPIO
#define EXAMPLE_PIN_NUM_DATA12         CONFIG_EXAMPLE_LCD_DATA12_GPIO
#define EXAMPLE_PIN_NUM_DATA13         CONFIG_EXAMPLE_LCD_DATA13_GPIO
#define EXAMPLE_PIN_NUM_DATA14         CONFIG_EXAMPLE_LCD_DATA14_GPIO
#define EXAMPLE_PIN_NUM_DATA15         CONFIG_EXAMPLE_LCD_DATA15_GPIO
#if CONFIG_EXAMPLE_LCD_DATA_LINES > 16
#define EXAMPLE_PIN_NUM_DATA16         CONFIG_EXAMPLE_LCD_DATA16_GPIO
#define EXAMPLE_PIN_NUM_DATA17         CONFIG_EXAMPLE_LCD_DATA17_GPIO
#define EXAMPLE_PIN_NUM_DATA18         CONFIG_EXAMPLE_LCD_DATA18_GPIO
#define EXAMPLE_PIN_NUM_DATA19         CONFIG_EXAMPLE_LCD_DATA19_GPIO
#define EXAMPLE_PIN_NUM_DATA20         CONFIG_EXAMPLE_LCD_DATA20_GPIO
#define EXAMPLE_PIN_NUM_DATA21         CONFIG_EXAMPLE_LCD_DATA21_GPIO
#define EXAMPLE_PIN_NUM_DATA22         CONFIG_EXAMPLE_LCD_DATA22_GPIO
#define EXAMPLE_PIN_NUM_DATA23         CONFIG_EXAMPLE_LCD_DATA23_GPIO
#endif

#if CONFIG_EXAMPLE_USE_DOUBLE_FB
#define EXAMPLE_LCD_NUM_FB             2
#else
#define EXAMPLE_LCD_NUM_FB             1
#endif // CONFIG_EXAMPLE_USE_DOUBLE_FB

#if CONFIG_EXAMPLE_LCD_DATA_LINES_16
#define EXAMPLE_DATA_BUS_WIDTH         16
#define EXAMPLE_PIXEL_SIZE             2
#define EXAMPLE_LV_COLOR_FORMAT        LV_COLOR_FORMAT_RGB565
#elif CONFIG_EXAMPLE_LCD_DATA_LINES_24
#define EXAMPLE_DATA_BUS_WIDTH         24
#define EXAMPLE_PIXEL_SIZE             3
#define EXAMPLE_LV_COLOR_FORMAT        LV_COLOR_FORMAT_RGB888
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your Application ///////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define EXAMPLE_LVGL_DRAW_BUF_LINES    50 // number of display lines in each draw buffer
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (5 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ

// LVGL library is not thread-safe, this example will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;

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

static void example_bsp_init_lcd_backlight(void)
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif
}

static void example_bsp_set_lcd_backlight(uint32_t level)
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, level);
#endif
}

void app_main(void)
{

    ESP_LOGI(TAG, "Install 3-wire SPI panel IO");
    spi_line_config_t line_config = {
        .cs_io_type = IO_TYPE_GPIO,                 // Set to `IO_TYPE_EXPANDER` if using IO expander
        .cs_gpio_num = 4,
        .scl_io_type = IO_TYPE_GPIO,
        .scl_gpio_num = 48,
        .sda_io_type = IO_TYPE_GPIO,
        .sda_gpio_num = 47,
        .io_expander = NULL,                        // Set to device handle if using IO expander

    };
    esp_lcd_panel_io_3wire_spi_config_t io_config = GC9503_PANEL_IO_3WIRE_SPI_CONFIG(line_config);
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_3wire_spi(&io_config, &io_handle));



    ESP_LOGI(TAG, "Turn off LCD backlight");
    example_bsp_init_lcd_backlight();
    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);

    ESP_LOGI(TAG, "Install RGB LCD panel driver");
//     esp_lcd_panel_handle_t panel_handle = NULL;
//     esp_lcd_rgb_panel_config_t panel_config = {
//         .data_width = EXAMPLE_DATA_BUS_WIDTH,
//         .dma_burst_size = 64,
//         .num_fbs = EXAMPLE_LCD_NUM_FB,
// #if CONFIG_EXAMPLE_USE_BOUNCE_BUFFER
//         .bounce_buffer_size_px = 20 * EXAMPLE_LCD_H_RES,
// #endif
//         .clk_src = LCD_CLK_SRC_DEFAULT,
//         .disp_gpio_num = EXAMPLE_PIN_NUM_DISP_EN,
//         .pclk_gpio_num = EXAMPLE_PIN_NUM_PCLK,
//         .vsync_gpio_num = EXAMPLE_PIN_NUM_VSYNC,
//         .hsync_gpio_num = EXAMPLE_PIN_NUM_HSYNC,
//         .de_gpio_num = EXAMPLE_PIN_NUM_DE,
//         .data_gpio_nums = {
//             EXAMPLE_PIN_NUM_DATA0,
//             EXAMPLE_PIN_NUM_DATA1,
//             EXAMPLE_PIN_NUM_DATA2,
//             EXAMPLE_PIN_NUM_DATA3,
//             EXAMPLE_PIN_NUM_DATA4,
//             EXAMPLE_PIN_NUM_DATA5,
//             EXAMPLE_PIN_NUM_DATA6,
//             EXAMPLE_PIN_NUM_DATA7,
//             EXAMPLE_PIN_NUM_DATA8,
//             EXAMPLE_PIN_NUM_DATA9,
//             EXAMPLE_PIN_NUM_DATA10,
//             EXAMPLE_PIN_NUM_DATA11,
//             EXAMPLE_PIN_NUM_DATA12,
//             EXAMPLE_PIN_NUM_DATA13,
//             EXAMPLE_PIN_NUM_DATA14,
//             EXAMPLE_PIN_NUM_DATA15,
// #if CONFIG_EXAMPLE_LCD_DATA_LINES > 16
//             EXAMPLE_PIN_NUM_DATA16,
//             EXAMPLE_PIN_NUM_DATA17,
//             EXAMPLE_PIN_NUM_DATA18,
//             EXAMPLE_PIN_NUM_DATA19,
//             EXAMPLE_PIN_NUM_DATA20,
//             EXAMPLE_PIN_NUM_DATA21,
//             EXAMPLE_PIN_NUM_DATA22,
//             EXAMPLE_PIN_NUM_DATA23
// #endif
//         },
//         .timings = {
//             .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
//             .h_res = EXAMPLE_LCD_H_RES,
//             .v_res = EXAMPLE_LCD_V_RES,
//             .hsync_back_porch = EXAMPLE_LCD_HBP,
//             .hsync_front_porch = EXAMPLE_LCD_HFP,
//             .hsync_pulse_width = EXAMPLE_LCD_HSYNC,
//             .vsync_back_porch = EXAMPLE_LCD_VBP,
//             .vsync_front_porch = EXAMPLE_LCD_VFP,
//             .vsync_pulse_width = EXAMPLE_LCD_VSYNC,
//             .flags = {
//                 .pclk_active_neg = true,
//             },
//         },
//         .flags.fb_in_psram = true, // allocate frame buffer in PSRAM
//     };
    ESP_LOGI(TAG, "Install GC9503 panel driver");
    esp_lcd_rgb_panel_config_t rgb_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dma_burst_size = 64,
        .data_width = 16,
        .de_gpio_num = EXAMPLE_PIN_NUM_DE,
        .pclk_gpio_num = EXAMPLE_PIN_NUM_PCLK,
        .vsync_gpio_num = EXAMPLE_PIN_NUM_VSYNC,
        .hsync_gpio_num = EXAMPLE_PIN_NUM_HSYNC,
        .disp_gpio_num = EXAMPLE_PIN_NUM_DISP_EN,
        .data_gpio_nums = {
            EXAMPLE_PIN_NUM_DATA0,
            EXAMPLE_PIN_NUM_DATA1,
            EXAMPLE_PIN_NUM_DATA2,
            EXAMPLE_PIN_NUM_DATA3,
            EXAMPLE_PIN_NUM_DATA4,
            EXAMPLE_PIN_NUM_DATA5,
            EXAMPLE_PIN_NUM_DATA6,
            EXAMPLE_PIN_NUM_DATA7,
            EXAMPLE_PIN_NUM_DATA8,
            EXAMPLE_PIN_NUM_DATA9,
            EXAMPLE_PIN_NUM_DATA10,
            EXAMPLE_PIN_NUM_DATA11,
            EXAMPLE_PIN_NUM_DATA12,
            EXAMPLE_PIN_NUM_DATA13,
            EXAMPLE_PIN_NUM_DATA14,
            EXAMPLE_PIN_NUM_DATA15,
        },
        .timings = GC9503_480_480_PANEL_60HZ_RGB_TIMING(),
        .flags.fb_in_psram = 1,
    };

    // gc9503_vendor_config_t vendor_config = {
    //     .rgb_config = &rgb_config,
    //     // .init_cmds = lcd_init_cmds,      // Uncomment these line if use custom initialization commands
    //     // .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(gc9503_lcd_init_cmd_t),
    //     .flags = {
    //         .mirror_by_cmd = 1,             // Only work when `auto_del_panel_io` is set to 0
    //         .auto_del_panel_io = 0,         /**
    //                                          * Set to 1 if panel IO is no longer needed after LCD initialization.
    //                                          * If the panel IO pins are sharing other pins of the RGB interface to save GPIOs,
    //                                          * Please set it to 1 to release the pins.
    //                                          */
    //     },
    // };
    // const esp_lcd_panel_dev_config_t panel_config = {
    //     .reset_gpio_num = -1,           // Set to -1 if not use
    //     .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,     // Implemented by LCD command `B1h`
    //     .vendor_config = &vendor_config,
    // };
    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_LOGI(TAG, "H_RES=%d", rgb_config.timings.h_res);
    ESP_LOGI(TAG, "V_RES=%d", rgb_config.timings.v_res);
    ESP_LOGI(TAG, "PCLK=%ld", rgb_config.timings.pclk_hz);
    ESP_LOGI(TAG, "PCLK_NEG=%d", rgb_config.timings.flags.pclk_active_neg);
    ESP_LOGI(TAG, "DISP=%d", EXAMPLE_PIN_NUM_DISP_EN);
    ESP_LOGI(TAG, "D0=%d", rgb_config.data_gpio_nums[0]);
    ESP_LOGI(TAG, "D1=%d", rgb_config.data_gpio_nums[1]);
    ESP_LOGI(TAG, "D2=%d", rgb_config.data_gpio_nums[2]);
    ESP_LOGI(TAG, "D3=%d", rgb_config.data_gpio_nums[3]);
    ESP_LOGI(TAG, "D4=%d", rgb_config.data_gpio_nums[4]);
    ESP_LOGI(TAG, "D5=%d", rgb_config.data_gpio_nums[5]);
    ESP_LOGI(TAG, "D6=%d", rgb_config.data_gpio_nums[6]);
    ESP_LOGI(TAG, "D7=%d", rgb_config.data_gpio_nums[7]);
    ESP_LOGI(TAG, "D8=%d", rgb_config.data_gpio_nums[8]);
    ESP_LOGI(TAG, "D9=%d", rgb_config.data_gpio_nums[9]);
    ESP_LOGI(TAG, "D10=%d", rgb_config.data_gpio_nums[10]);
    ESP_LOGI(TAG, "D11=%d", rgb_config.data_gpio_nums[11]);
    ESP_LOGI(TAG, "D12=%d", rgb_config.data_gpio_nums[12]);
    ESP_LOGI(TAG, "D13=%d", rgb_config.data_gpio_nums[13]);
    ESP_LOGI(TAG, "D14=%d", rgb_config.data_gpio_nums[14]);

    ESP_LOGI(TAG, "D15=%d", rgb_config.data_gpio_nums[15]);

    ESP_LOGI(TAG, "HSYNC=%d", rgb_config.hsync_gpio_num);
ESP_LOGI(TAG, "VSYNC=%d", rgb_config.vsync_gpio_num);
ESP_LOGI(TAG, "DE=%d", rgb_config.de_gpio_num);
ESP_LOGI(TAG, "PCLK=%d", rgb_config.pclk_gpio_num);


    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9503(io_handle, &rgb_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
ESP_LOGI(TAG, "Panel handle=%p", panel_handle);

    uint16_t *fb = NULL;
    /** ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true)); 
                                                                     * Don't call this function if `auto_del_panel_io` is set to 0
                                                                     **/

esp_err_t ret = esp_lcd_panel_disp_on_off(panel_handle, true);
ret = esp_lcd_panel_reset(panel_handle);
ESP_LOGI(TAG, "reset = %s", esp_err_to_name(ret));

ret = esp_lcd_panel_init(panel_handle);
ESP_LOGI(TAG, "init = %s", esp_err_to_name(ret));

ESP_ERROR_CHECK(
    esp_lcd_rgb_panel_get_frame_buffer(
        panel_handle,
        1,
        (void **)&fb
    )
);

ESP_LOGI(TAG, "FB = %p", fb);

for (int i = 0; i < 480 * 480; i++) {
    fb[i] = 0xF800;
}

ESP_LOGI(TAG, "Framebuffer filled");

while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
}


    ESP_LOGI(TAG, "disp_on_off returned %s", esp_err_to_name(ret));
    //ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

    ESP_LOGI(TAG, "Turn on LCD backlight");
    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

//     ESP_LOGI(TAG, "Initialize LVGL library");
//     lv_init();
//     // create a lvgl display
//     lv_display_t *display = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
//     // associate the rgb panel handle to the display
//     lv_display_set_user_data(display, panel_handle);
//     // set color depth
//     lv_display_set_color_format(display, EXAMPLE_LV_COLOR_FORMAT);

//     ESP_LOGI(TAG, "Bus Width = %d", EXAMPLE_DATA_BUS_WIDTH);

//     // create draw buffers
//     void *buf1 = NULL;
//     void *buf2 = NULL;
// #if CONFIG_EXAMPLE_USE_DOUBLE_FB
//     ESP_LOGI(TAG, "Use frame buffers as LVGL draw buffers");
//     ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2));
//     // set LVGL draw buffers and direct mode
//     lv_display_set_buffers(display, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * EXAMPLE_PIXEL_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);
// #else
//     ESP_LOGI(TAG, "Allocate LVGL draw buffers");
//     // it's recommended to allocate the draw buffer from internal memory, for better performance
//     size_t draw_buffer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_DRAW_BUF_LINES * EXAMPLE_PIXEL_SIZE;
//     buf1 = esp_lcd_rgb_alloc_draw_buffer(panel_handle, draw_buffer_sz, 0);
//     assert(buf1);
//     // set LVGL draw buffers and partial mode
//     lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
// #endif // CONFIG_EXAMPLE_USE_DOUBLE_FB

//     // set the callback which can copy the rendered image to an area of the display
//     lv_display_set_flush_cb(display, example_lvgl_flush_cb);

//     ESP_LOGI(TAG, "Register event callbacks");
//     esp_lcd_rgb_panel_event_callbacks_t cbs = {
//         .on_color_trans_done = example_notify_lvgl_flush_ready,
//     };
//     ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, display));

//     ESP_LOGI(TAG, "Install LVGL tick timer");
//     // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
//     const esp_timer_create_args_t lvgl_tick_timer_args = {
//         .callback = &example_increase_lvgl_tick,
//         .name = "lvgl_tick"
//     };
//     esp_timer_handle_t lvgl_tick_timer = NULL;
//     ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
//     ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

//     ESP_LOGI(TAG, "Create LVGL task");
//     xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

//     ESP_LOGI(TAG, "Display LVGL UI");
//     // Lock the mutex due to the LVGL APIs are not thread-safe
//     _lock_acquire(&lvgl_api_lock);
//     example_lvgl_demo_ui(display);
//     _lock_release(&lvgl_api_lock);
}
