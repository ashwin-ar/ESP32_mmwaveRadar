/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl.h"

static lv_obj_t * main_chart = NULL;
static lv_chart_series_t * main_ser = NULL;

int32_t x_min = -100, x_max = 100;
int32_t y_min = -100, y_max = 100;

#define MAX_POINTS 50
static lv_color_t point_colors[MAX_POINTS];

// #if LV_USE_CHART && LV_BUILD_EXAMPLES
static void draw_event_cb(lv_event_t * e)
{
    lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t * base_dsc = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(draw_task);
    if(base_dsc->part != LV_PART_INDICATOR) return;
    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_chart_series_t * ser = lv_chart_get_series_next(obj, NULL);
    lv_draw_fill_dsc_t * fill_draw_dsc = lv_draw_task_get_fill_dsc(draw_task);
    if(fill_draw_dsc == NULL) return;
    uint32_t cnt = lv_chart_get_point_count(obj);
    
    /*Make older value more transparent*/
    fill_draw_dsc->opa = (lv_opa_t)((LV_OPA_COVER * base_dsc->id2) / (cnt - 1));

    uint32_t start_point = lv_chart_get_x_start_point(obj, ser);
    uint32_t p_act = (start_point + base_dsc->id2) % cnt; /*Consider start point to get the index of the array*/

    fill_draw_dsc->color = point_colors[p_act];
}

/**
 * Call this function from main application loop/task to push data
 */

void chart_add_custom_point(int32_t x, int32_t y, lv_color_t color)
{
    if(main_chart == NULL || main_ser == NULL) return;

    /* Get the upcoming index where the chart will place this new value */
    uint32_t next_idx = lv_chart_get_x_start_point(main_chart, main_ser);
    
    /* Store the color at that exact index location */
    point_colors[next_idx] = color;

    /* Push the value into the scatter chart */
    lv_chart_set_next_value2(main_chart, main_ser, x, y);
}

void lv_example_chart_scatter(void)
{
    main_chart = lv_chart_create(lv_screen_active());
    lv_obj_set_size(main_chart, 440, 440);
    lv_obj_align(main_chart, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(main_chart, draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(main_chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_set_style_line_width(main_chart, 0, LV_PART_ITEMS);   
    lv_chart_set_div_line_count(main_chart, 9, 9); 

    lv_chart_set_type(main_chart, LV_CHART_TYPE_SCATTER);
    lv_chart_set_axis_range(main_chart, LV_CHART_AXIS_PRIMARY_X, x_min, x_max);
    lv_chart_set_axis_range(main_chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
    lv_chart_set_point_count(main_chart, MAX_POINTS);
    
    main_ser = lv_chart_add_series(main_chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);

    /**** Draw a quadrant line *******/
    lv_chart_cursor_t * axes_cursor = lv_chart_add_cursor(main_chart, lv_color_black(), LV_DIR_ALL);
    lv_obj_update_layout(main_chart); 
    
    int32_t center_x = lv_obj_get_content_width(main_chart) / 2;
    int32_t center_y = lv_obj_get_content_height(main_chart) / 2;
    lv_point_t center_point = { .x = center_x, .y = center_y };
    lv_chart_set_cursor_pos(main_chart, axes_cursor, &center_point);
    
    // Note: The periodic timer has been removed here since main will now feed data.
}
// #endif