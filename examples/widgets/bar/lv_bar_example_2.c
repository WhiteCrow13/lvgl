#include "../../../lvgl.h"
#if LV_USE_BAR

/**
 * Example of styling the bar
 */
void lv_ex_bar_2(void)
{
    static lv_style_t style_bg;
    static lv_style_t style_indic;

    lv_style_init(&style_bg);
    lv_style_set_border_color(&style_bg, LV_COLOR_BLUE);
    lv_style_set_border_width(&style_bg, 2);
    lv_style_set_pad_all(&style_bg, 6); /*To make the indicator smaller*/
    lv_style_set_radius(&style_bg, 6);
    lv_style_set_anim_time(&style_bg, 1000);

    lv_style_init(&style_indic);
    lv_style_set_bg_opa(&style_indic, LV_OPA_COVER);
    lv_style_set_bg_color(&style_indic, LV_COLOR_BLUE);
    lv_style_set_radius(&style_indic, 3);

    lv_obj_t * bar = lv_bar_create(lv_scr_act(), NULL);
    lv_obj_remove_style(bar, LV_PART_ANY, LV_STATE_ANY, NULL);  /*To have a clean start*/
    lv_obj_add_style(bar, LV_PART_MAIN, LV_STATE_DEFAULT, &style_bg);
    lv_obj_add_style(bar, LV_PART_INDICATOR, LV_STATE_DEFAULT, &style_indic);

    lv_obj_set_size(bar, 200, 20);
    lv_obj_align(bar, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_value(bar, 100, LV_ANIM_ON);
}

#endif
