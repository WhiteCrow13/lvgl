/**
 * @file lv_btnmatrix.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_btnmatrix.h"
#if LV_USE_BTNMATRIX != 0

#include "../lv_misc/lv_debug.h"
#include "../lv_core/lv_indev.h"
#include "../lv_core/lv_group.h"
#include "../lv_draw/lv_draw.h"
#include "../lv_core/lv_refr.h"
#include "../lv_misc/lv_txt.h"
#include "../lv_misc/lv_txt_ap.h"

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS &lv_btnmatrix

#define BTN_EXTRA_CLICK_AREA_MAX (LV_DPI_DEF / 4)
#define LV_BTNMATRIX_WIDTH_MASK 0x0007

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_btnmatrix_constructor(lv_obj_t * obj, lv_obj_t * parent, const lv_obj_t * copy);
static void lv_btnmatrix_destructor(lv_obj_t * obj);

static lv_res_t lv_btnmatrix_signal(lv_obj_t * obj, lv_signal_t sign, void * param);
static lv_draw_res_t lv_btnmatrix_draw(lv_obj_t * obj, const lv_area_t * clip_area, lv_draw_mode_t mode);

static uint8_t get_button_width(lv_btnmatrix_ctrl_t ctrl_bits);
static bool button_is_hidden(lv_btnmatrix_ctrl_t ctrl_bits);
static bool button_is_repeat_disabled(lv_btnmatrix_ctrl_t ctrl_bits);
static bool button_is_inactive(lv_btnmatrix_ctrl_t ctrl_bits);
static bool button_is_click_trig(lv_btnmatrix_ctrl_t ctrl_bits);
static bool button_is_tgl_enabled(lv_btnmatrix_ctrl_t ctrl_bits);
static bool button_get_checked(lv_btnmatrix_ctrl_t ctrl_bits);
static uint16_t get_button_from_point(lv_obj_t * obj, lv_point_t * p);
static void allocate_btn_areas_and_controls(const lv_obj_t * obj, const char ** map);
static void invalidate_button_area(const lv_obj_t * obj, uint16_t btn_idx);
static void make_one_button_checked(lv_obj_t * obj, uint16_t btn_idx);

/**********************
 *  STATIC VARIABLES
 **********************/
static const char * lv_btnmatrix_def_map[] = {"Btn1", "Btn2", "Btn3", "\n", "Btn4", "Btn5", ""};

const lv_obj_class_t lv_btnmatrix = {
        .constructor_cb = lv_btnmatrix_constructor,
        .destructor_cb = lv_btnmatrix_destructor,
        .signal_cb = lv_btnmatrix_signal,
        .draw_cb = lv_btnmatrix_draw,
        .instance_size = sizeof(lv_btnmatrix_t),
        .editable = LV_OBJ_CLASS_EDITABLE_TRUE,
        .base_class = &lv_obj
    };

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * lv_btnmatrix_create(lv_obj_t * parent, const lv_obj_t * copy)
{
    return lv_obj_create_from_class(&lv_btnmatrix, parent, copy);
}

/*=====================
 * Setter functions
 *====================*/

void lv_btnmatrix_set_map(lv_obj_t * obj, const char * map[])
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    LV_ASSERT_NULL(map);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;

    /*Analyze the map and create the required number of buttons*/
    allocate_btn_areas_and_controls(obj, map);
    btnm->map_p = map;

    lv_bidi_dir_t base_dir = lv_obj_get_base_dir(obj);

    /*Set size and positions of the buttons*/
    lv_coord_t pleft = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
    lv_coord_t ptop = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
    lv_coord_t prow = lv_obj_get_style_pad_row(obj, LV_PART_MAIN);
    lv_coord_t pcol = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);

    lv_coord_t max_w            = lv_obj_get_width_fit(obj);
    lv_coord_t max_h            = lv_obj_get_height_fit(obj);

    /*Count the lines to calculate button height*/
    uint8_t row_cnt = 1;
    uint32_t i;
    for(i = 0; strlen(map[i]) != 0; i++) {
        if(strcmp(map[i], "\n") == 0) row_cnt++;
    }

    /*Calculate the position of each row*/
    lv_coord_t max_h_no_gap = max_h - (prow * (row_cnt - 1));

    /* Count the units and the buttons in a line
     * (A button can be 1,2,3... unit wide)*/
    uint32_t txt_tot_i = 0; /*Act. index in the str map*/
    uint32_t btn_tot_i = 0; /*Act. index of button areas*/
    const char ** map_row = map;

    /*Count the units and the buttons in a line*/
    uint32_t row;
    for(row = 0; row < row_cnt; row++) {
        uint16_t unit_cnt = 0;           /*Number of units in a row*/
        uint16_t btn_cnt = 0;            /*Number of buttons in a row*/
        /*Count the buttons and units in this row*/
        while(strcmp(map_row[btn_cnt], "\n") != 0 && strlen(map_row[btn_cnt]) != '\0') {
            unit_cnt += get_button_width(btnm->ctrl_bits[btn_tot_i + btn_cnt]);
            btn_cnt++;
        }

        /*Only deal with the non empty lines*/
        if(btn_cnt == 0) {
            map_row = &map_row[btn_cnt + 1];       /*Set the map to the next row*/
            continue;
        }

        lv_coord_t row_y1 = ptop + (max_h_no_gap * row) / row_cnt + row * prow;
        lv_coord_t row_y2 = ptop + (max_h_no_gap * (row + 1)) / row_cnt + row * prow - 1;

        /*Set the button size and positions*/
        lv_coord_t max_w_no_gap = max_w - (pcol * (btn_cnt - 1));
        if(max_w_no_gap < 0) max_w_no_gap = 0;

        uint32_t row_unit_cnt = 0;  /*The current unit position in the row*/
        uint32_t btn;
        for(btn = 0; btn < btn_cnt; btn++, btn_tot_i++, txt_tot_i++) {
            uint32_t btn_u = get_button_width(btnm->ctrl_bits[btn_tot_i]);

            lv_coord_t btn_x1 = pleft + (max_w_no_gap * row_unit_cnt) / unit_cnt + btn * pcol;
            lv_coord_t btn_x2 = pleft + (max_w_no_gap * (row_unit_cnt + btn_u)) / unit_cnt + btn * pcol - 1;

            /*If RTL start from the right*/
            if(base_dir == LV_BIDI_DIR_RTL) {
                lv_coord_t tmp = btn_x1;
                btn_x1 = btn_x2;
                btn_x2 = tmp;
                btn_x1 = max_w - btn_x1;
                btn_x2 = max_w - btn_x2;
            }

            lv_area_set(&btnm->button_areas[btn_tot_i], btn_x1, row_y1, btn_x2, row_y2);

            row_unit_cnt += btn_u;
        }

        map_row = &map_row[btn_cnt + 1];       /*Set the map to the next line*/
    }

    lv_obj_invalidate(obj);
}

void lv_btnmatrix_set_ctrl_map(lv_obj_t * obj, const lv_btnmatrix_ctrl_t ctrl_map[])
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;
    lv_memcpy(btnm->ctrl_bits, ctrl_map, sizeof(lv_btnmatrix_ctrl_t) * btnm->btn_cnt);

    lv_btnmatrix_set_map(obj, btnm->map_p);
}

void lv_btnmatrix_set_focused_btn(lv_obj_t * obj, uint16_t id)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;

    if(id >= btnm->btn_cnt && id != LV_BTNMATRIX_BTN_NONE) return;

    if(id == btnm->btn_id_focused) return;

    btnm->btn_id_focused = id;
    lv_obj_invalidate(obj);
}

void lv_btnmatrix_set_recolor(const lv_obj_t * obj, bool en)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;

    btnm->recolor = en;
    lv_obj_invalidate(obj);
}

void lv_btnmatrix_set_btn_ctrl(lv_obj_t * obj, uint16_t btn_id, lv_btnmatrix_ctrl_t ctrl)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;

    if(btn_id >= btnm->btn_cnt) return;

    if(btnm->one_check && (ctrl & LV_BTNMATRIX_CTRL_CHECKED)) {
        lv_btnmatrix_clear_btn_ctrl_all(obj, LV_BTNMATRIX_CTRL_CHECKED);
    }

    btnm->ctrl_bits[btn_id] |= ctrl;
    invalidate_button_area(obj, btn_id);
}

void lv_btnmatrix_clear_btn_ctrl(const lv_obj_t * obj, uint16_t btn_id, lv_btnmatrix_ctrl_t ctrl)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;

    if(btn_id >= btnm->btn_cnt) return;

    btnm->ctrl_bits[btn_id] &= (~ctrl);
    invalidate_button_area(obj, btn_id);
}

void lv_btnmatrix_set_btn_ctrl_all(lv_obj_t * obj, lv_btnmatrix_ctrl_t ctrl)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;
    uint16_t i;
    for(i = 0; i < btnm->btn_cnt; i++) {
        lv_btnmatrix_set_btn_ctrl(obj, i, ctrl);
    }
}

void lv_btnmatrix_clear_btn_ctrl_all(lv_obj_t * obj, lv_btnmatrix_ctrl_t ctrl)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;
    uint16_t i;
    for(i = 0; i < btnm->btn_cnt; i++) {
        lv_btnmatrix_clear_btn_ctrl(obj, i, ctrl);
    }
}

void lv_btnmatrix_set_btn_width(lv_obj_t * obj, uint16_t btn_id, uint8_t width)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;
    if(btn_id >= btnm->btn_cnt) return;
    btnm->ctrl_bits[btn_id] &= (~LV_BTNMATRIX_WIDTH_MASK);
    btnm->ctrl_bits[btn_id] |= (LV_BTNMATRIX_WIDTH_MASK & width);

    lv_btnmatrix_set_map(obj, btnm->map_p);
}

void lv_btnmatrix_set_one_checked(lv_obj_t * obj, bool en)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;
    btnm->one_check     = en;

    /*If more than one button is toggled only the first one should be*/
    make_one_button_checked(obj, 0);
}

/*=====================
 * Getter functions
 *====================*/

const char ** lv_btnmatrix_get_map(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;
    return btnm->map_p;
}

bool lv_btnmatrix_get_recolor(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;

    return btnm->recolor;
}

uint16_t lv_btnmatrix_get_active_btn(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;
    return btnm->btn_id_act;
}

uint16_t lv_btnmatrix_get_pressed_btn(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;
    return btnm->btn_id_pr;
}


uint16_t lv_btnmatrix_get_focused_btn(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;
    return btnm->btn_id_focused;
}

const char * lv_btnmatrix_get_btn_text(const lv_obj_t * obj, uint16_t btn_id)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    if(btn_id == LV_BTNMATRIX_BTN_NONE) return NULL;

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;
    if(btn_id > btnm->btn_cnt) return NULL;

    uint16_t txt_i = 0;
    uint16_t btn_i = 0;

    /* Search the text of btnm->btn_pr the buttons text in the map
     * Skip "\n"-s*/
    while(btn_i != btn_id) {
        btn_i++;
        txt_i++;
        if(strcmp(btnm->map_p[txt_i], "\n") == 0) txt_i++;
    }

    if(btn_i == btnm->btn_cnt) return NULL;

    return btnm->map_p[txt_i];
}

bool lv_btnmatrix_has_btn_ctrl(lv_obj_t * obj, uint16_t btn_id, lv_btnmatrix_ctrl_t ctrl)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;
    if(btn_id >= btnm->btn_cnt) return false;

    return (btnm->ctrl_bits[btn_id] & ctrl) ? true : false;
}

bool lv_btnmatrix_get_one_checked(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;

    return btnm->one_check;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_btnmatrix_constructor(lv_obj_t * obj, lv_obj_t * parent, const lv_obj_t * copy)
{
    lv_obj_construct_base(obj, parent, copy);

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;
    btnm->btn_cnt        = 0;
    btnm->btn_id_pr      = LV_BTNMATRIX_BTN_NONE;
    btnm->btn_id_focused = LV_BTNMATRIX_BTN_NONE;
    btnm->btn_id_act     = LV_BTNMATRIX_BTN_NONE;
    btnm->button_areas   = NULL;
    btnm->ctrl_bits      = NULL;
    btnm->map_p          = NULL;
    btnm->recolor        = 0;
    btnm->one_check      = 0;

    /*Init the new button matrix object*/
    if(copy == NULL) {
        lv_btnmatrix_set_map(obj, lv_btnmatrix_def_map);
        lv_obj_set_size(obj, LV_DPI_DEF * 2, LV_DPI_DEF * 1);
    }
    /*Copy an existing object*/
    else {
        lv_btnmatrix_t * copy_btnm = (lv_btnmatrix_t *) copy;
        lv_btnmatrix_set_map(obj, copy_btnm->map_p);
        lv_btnmatrix_set_ctrl_map(obj, copy_btnm->ctrl_bits);
    }


    LV_LOG_INFO("button matrix created");
}

static void lv_btnmatrix_destructor(lv_obj_t * obj)
{
    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;
    lv_mem_free(btnm->button_areas);
    lv_mem_free(btnm->ctrl_bits);
    btnm->button_areas = NULL;
    btnm->ctrl_bits = NULL;
}

/**
 * Handle the drawing related tasks of the button matrix
 * @param obj pointer to a button matrix object
 * @param clip_area the object will be drawn only in this area
 * @param mode LV_DRAW_COVER_CHK: only check if the object fully covers the 'mask_p' area
 *                                  (return 'true' if yes)
 *             LV_DRAW_DRAW: draw the object (always return 'true')
 *             LV_DRAW_DRAW_POST: drawing after every children are drawn
 * @param return an element of `lv_draw_res_t`
 */
static lv_draw_res_t lv_btnmatrix_draw(lv_obj_t * obj, const lv_area_t * clip_area, lv_draw_mode_t mode)
{
    if(mode == LV_DRAW_MODE_COVER_CHECK) {
        return lv_obj_draw_base(MY_CLASS, obj, clip_area, mode);
    }
    /*Draw the object*/
    else if(mode == LV_DRAW_MODE_MAIN_DRAW) {
        lv_obj_draw_base(MY_CLASS, obj, clip_area, mode);

        lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;
        if(btnm->btn_cnt == 0) return LV_DRAW_RES_OK;

        obj->style_list.skip_trans = 1;

        lv_area_t area_obj;
        lv_obj_get_coords(obj, &area_obj);

        lv_area_t btn_area;

        uint16_t btn_i = 0;
        uint16_t txt_i = 0;

        lv_draw_rect_dsc_t draw_rect_dsc_act;
        lv_draw_label_dsc_t draw_label_dsc_act;

        lv_draw_rect_dsc_t draw_rect_def_default;
        lv_draw_label_dsc_t draw_label_def_default;

        lv_text_flag_t recolor_flag = btnm->recolor ? LV_TEXT_FLAG_RECOLOR : 0;

        lv_state_t state_ori = obj->state;
        obj->state = LV_STATE_DEFAULT;
        obj->style_list.skip_trans = 1;
        lv_draw_rect_dsc_init(&draw_rect_def_default);
        lv_draw_label_dsc_init(&draw_label_def_default);
        lv_obj_init_draw_rect_dsc(obj, LV_PART_ITEMS, &draw_rect_def_default);
        lv_obj_init_draw_label_dsc(obj, LV_PART_ITEMS, &draw_label_def_default);
        draw_label_dsc_act.flag |= recolor_flag;
        obj->style_list.skip_trans = 0;
        obj->state = state_ori;

        lv_coord_t ptop = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
        lv_coord_t pbottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
        lv_coord_t pleft = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
        lv_coord_t pright = lv_obj_get_style_pad_right(obj, LV_PART_MAIN);

#if LV_USE_ARABIC_PERSIAN_CHARS
        const size_t txt_ap_size = 256 ;
        char * txt_ap = lv_mem_buf_get(txt_ap_size);
#endif

        lv_obj_draw_hook_dsc_t hook_dsc;
        lv_obj_draw_hook_dsc_init(&hook_dsc, clip_area);
        hook_dsc.part = LV_PART_ITEMS;
        hook_dsc.rect_dsc = &draw_rect_dsc_act;
        hook_dsc.label_dsc = &draw_label_dsc_act;

        for(btn_i = 0; btn_i < btnm->btn_cnt; btn_i++, txt_i++) {
            /*Search the next valid text in the map*/
            while(strcmp(btnm->map_p[txt_i], "\n") == 0) {
                txt_i++;
            }

            /*Skip hidden buttons*/
            if(button_is_hidden(btnm->ctrl_bits[btn_i])) continue;

            /*Get the state of the button*/
            lv_state_t btn_state = LV_STATE_DEFAULT;
            if(button_get_checked(btnm->ctrl_bits[btn_i])) btn_state |= LV_STATE_CHECKED;
            if(button_is_inactive(btnm->ctrl_bits[btn_i])) btn_state |= LV_STATE_DISABLED;
            if(btn_i == btnm->btn_id_pr) btn_state |= LV_STATE_PRESSED;
            if(btn_i == btnm->btn_id_focused) {
                btn_state |= LV_STATE_FOCUSED;
                if(state_ori & LV_STATE_EDITED) btn_state |= LV_STATE_EDITED;
            }

            /*Get the button's area*/
            lv_area_copy(&btn_area, &btnm->button_areas[btn_i]);
            btn_area.x1 += area_obj.x1;
            btn_area.y1 += area_obj.y1;
            btn_area.x2 += area_obj.x1;
            btn_area.y2 += area_obj.y1;

            /*Set up the draw descriptors*/
            if(btn_state == LV_STATE_DEFAULT) {
                lv_memcpy(&draw_rect_dsc_act, &draw_rect_def_default, sizeof(lv_draw_rect_dsc_t));
                lv_memcpy(&draw_label_dsc_act, &draw_label_def_default, sizeof(lv_draw_label_dsc_t));
            }
            /*In other cases get the styles directly without caching them*/
            else {
                obj->state = btn_state;
                obj->style_list.skip_trans = 1;
                lv_draw_rect_dsc_init(&draw_rect_dsc_act);
                lv_draw_label_dsc_init(&draw_label_dsc_act);
                lv_obj_init_draw_rect_dsc(obj, LV_PART_ITEMS, &draw_rect_dsc_act);
                lv_obj_init_draw_label_dsc(obj, LV_PART_ITEMS, &draw_label_dsc_act);
                draw_label_dsc_act.flag = recolor_flag;
                obj->state = state_ori;
                obj->style_list.skip_trans = 0;
            }

            hook_dsc.draw_area = &btn_area;
            hook_dsc.id = btn_i;
            lv_event_send(obj,LV_EVENT_DRAW_PART_BEGIN, &hook_dsc);

            /*Remove borders on the edges if `LV_BORDER_SIDE_INTERNAL`*/
            if(draw_rect_dsc_act.border_side & LV_BORDER_SIDE_INTERNAL) {
                if(btn_area.x1 == obj->coords.x1 + pleft) draw_rect_dsc_act.border_side &= ~LV_BORDER_SIDE_LEFT;
                if(btn_area.y2 == obj->coords.x2 - pright) draw_rect_dsc_act.border_side &= ~LV_BORDER_SIDE_RIGHT;
                if(btn_area.y1 == obj->coords.y1 + ptop) draw_rect_dsc_act.border_side &= ~LV_BORDER_SIDE_TOP;
                if(btn_area.y2 == obj->coords.y2 - pbottom) draw_rect_dsc_act.border_side &= ~LV_BORDER_SIDE_BOTTOM;
            }

            /*Draw the background*/
            lv_draw_rect(&btn_area, clip_area, &draw_rect_dsc_act);

            /*Calculate the size of the text*/
            const lv_font_t * font = draw_label_dsc_act.font;
            lv_coord_t letter_space = draw_label_dsc_act.letter_space;
            lv_coord_t line_space = draw_label_dsc_act.line_space;
            const char * txt = btnm->map_p[txt_i];

#if LV_USE_ARABIC_PERSIAN_CHARS
            /*Get the size of the Arabic text and process it*/
            size_t len_ap = _lv_txt_ap_calc_bytes_cnt(txt);
            if(len_ap < txt_ap_size) {
                _lv_txt_ap_proc(txt, txt_ap);
                txt = txt_ap;
            }
#endif

            lv_point_t txt_size;
            lv_txt_get_size(&txt_size, txt, font, letter_space,
                             line_space, lv_area_get_width(&area_obj), recolor_flag);

            btn_area.x1 += (lv_area_get_width(&btn_area) - txt_size.x) / 2;
            btn_area.y1 += (lv_area_get_height(&btn_area) - txt_size.y) / 2;
            btn_area.x2 = btn_area.x1 + txt_size.x;
            btn_area.y2 = btn_area.y1 + txt_size.y;

            /*Draw the text*/
            lv_draw_label(&btn_area, clip_area, &draw_label_dsc_act, txt, NULL);


            lv_event_send(obj,LV_EVENT_DRAW_PART_END, &hook_dsc);
        }

        obj->style_list.skip_trans = 0;
#if LV_USE_ARABIC_PERSIAN_CHARS
        lv_mem_buf_release(txt_ap);
#endif
    }
    else if(mode == LV_DRAW_MODE_POST_DRAW) {
        lv_obj_draw_base(MY_CLASS, obj, clip_area, mode);
    }
    return LV_DRAW_RES_OK;
}

/**
 * Signal function of the button matrix
 * @param obj pointer to a button matrix object
 * @param sign a signal type from lv_signal_t enum
 * @param param pointer to a signal specific variable
 * @return LV_RES_OK: the object is not deleted in the function; LV_RES_INV: the object is deleted
 */
static lv_res_t lv_btnmatrix_signal(lv_obj_t * obj, lv_signal_t sign, void * param)
{
    lv_res_t res;

    /* Include the ancient signal function */
    res = lv_obj_signal_base(MY_CLASS, obj, sign, param);
    if(res != LV_RES_OK) return res;

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;
    lv_point_t p;

    if(sign == LV_SIGNAL_STYLE_CHG) {
        lv_btnmatrix_set_map(obj, btnm->map_p);
    }
    else if(sign == LV_SIGNAL_COORD_CHG) {
        if(param &&
           (lv_obj_get_width(obj) != lv_area_get_width(param) || lv_obj_get_height(obj) != lv_area_get_height(param)))
        {
            lv_btnmatrix_set_map(obj, btnm->map_p);
        }
    }
    else if(sign == LV_SIGNAL_PRESSED) {
        invalidate_button_area(obj, btnm->btn_id_pr);

        lv_indev_type_t indev_type = lv_indev_get_type(lv_indev_get_act());
        if(indev_type == LV_INDEV_TYPE_POINTER || indev_type == LV_INDEV_TYPE_BUTTON) {
            uint16_t btn_pr;
            /*Search the pressed area*/
            lv_indev_get_point(param, &p);
            btn_pr = get_button_from_point(obj, &p);
            /*Handle the case where there is no button there*/
            if(btn_pr != LV_BTNMATRIX_BTN_NONE) {
                if(button_is_inactive(btnm->ctrl_bits[btn_pr]) == false &&
                   button_is_hidden(btnm->ctrl_bits[btn_pr]) == false) {
                    invalidate_button_area(obj, btnm->btn_id_pr) /*Invalidate the old area*/;
                    btnm->btn_id_pr = btn_pr;
                    btnm->btn_id_act = btn_pr;
                    invalidate_button_area(obj, btnm->btn_id_pr); /*Invalidate the new area*/
                }
            }
        }
        else if(indev_type == LV_INDEV_TYPE_KEYPAD || (indev_type == LV_INDEV_TYPE_ENCODER &&
                                                       lv_group_get_editing(lv_obj_get_group(obj)))) {
            btnm->btn_id_pr = btnm->btn_id_focused;
            invalidate_button_area(obj, btnm->btn_id_focused);
        }

        if(btnm->btn_id_pr != LV_BTNMATRIX_BTN_NONE) {
            if(button_is_click_trig(btnm->ctrl_bits[btnm->btn_id_pr]) == false &&
               button_is_inactive(btnm->ctrl_bits[btnm->btn_id_pr]) == false &&
               button_is_hidden(btnm->ctrl_bits[btnm->btn_id_pr]) == false) {
                uint32_t b = btnm->btn_id_pr;
                res        = lv_event_send(obj, LV_EVENT_VALUE_CHANGED, &b);
            }
        }
    }
    else if(sign == LV_SIGNAL_PRESSING) {
        uint16_t btn_pr = LV_BTNMATRIX_BTN_NONE;
        /*Search the pressed area*/
        lv_indev_t * indev = lv_indev_get_act();
        lv_indev_type_t indev_type = lv_indev_get_type(indev);
        if(indev_type == LV_INDEV_TYPE_ENCODER || indev_type == LV_INDEV_TYPE_KEYPAD) return LV_RES_OK;

        lv_indev_get_point(indev, &p);
        btn_pr = get_button_from_point(obj, &p);
        /*Invalidate to old and the new areas*/
        if(btn_pr != btnm->btn_id_pr) {
            if(btnm->btn_id_pr != LV_BTNMATRIX_BTN_NONE) {
                invalidate_button_area(obj, btnm->btn_id_pr);
            }

            btnm->btn_id_pr  = btn_pr;
            btnm->btn_id_act = btn_pr;

            lv_indev_reset_long_press(param); /*Start the log press time again on the new button*/
            if(btn_pr != LV_BTNMATRIX_BTN_NONE &&
               button_is_inactive(btnm->ctrl_bits[btn_pr]) == false &&
               button_is_hidden(btnm->ctrl_bits[btn_pr]) == false) {
                invalidate_button_area(obj, btn_pr);
                /* Send VALUE_CHANGED for the newly pressed button */
                if(button_is_click_trig(btnm->ctrl_bits[btn_pr]) == false) {
                    uint32_t b = btn_pr;
                    lv_event_send(obj, LV_EVENT_VALUE_CHANGED, &b);
                }
            }
        }
    }
    else if(sign == LV_SIGNAL_RELEASED) {
        if(btnm->btn_id_pr != LV_BTNMATRIX_BTN_NONE) {
            /*Toggle the button if enabled*/
            if(button_is_tgl_enabled(btnm->ctrl_bits[btnm->btn_id_pr]) &&
               !button_is_inactive(btnm->ctrl_bits[btnm->btn_id_pr])) {
                if(button_get_checked(btnm->ctrl_bits[btnm->btn_id_pr]) && !btnm->one_check) {
                    btnm->ctrl_bits[btnm->btn_id_pr] &= (~LV_BTNMATRIX_CTRL_CHECKED);
                }
                else {
                    btnm->ctrl_bits[btnm->btn_id_pr] |= LV_BTNMATRIX_CTRL_CHECKED;
                }
                if(btnm->one_check) make_one_button_checked(obj, btnm->btn_id_pr);
            }

            /*Invalidate to old pressed area*/;
            invalidate_button_area(obj, btnm->btn_id_pr);
            invalidate_button_area(obj, btnm->btn_id_focused);

            lv_indev_type_t indev_type = lv_indev_get_type(lv_indev_get_act());
            if(indev_type == LV_INDEV_TYPE_KEYPAD || indev_type == LV_INDEV_TYPE_ENCODER) {
                btnm->btn_id_focused = btnm->btn_id_pr;
            }

            btnm->btn_id_pr = LV_BTNMATRIX_BTN_NONE;

            if(button_is_click_trig(btnm->ctrl_bits[btnm->btn_id_act]) == true &&
               button_is_inactive(btnm->ctrl_bits[btnm->btn_id_act]) == false &&
               button_is_hidden(btnm->ctrl_bits[btnm->btn_id_act]) == false) {
                uint32_t b = btnm->btn_id_act;
                res        = lv_event_send(obj, LV_EVENT_VALUE_CHANGED, &b);
            }
        }
    }
    else if(sign == LV_SIGNAL_LONG_PRESS_REP) {
        if(btnm->btn_id_act != LV_BTNMATRIX_BTN_NONE) {
            if(button_is_repeat_disabled(btnm->ctrl_bits[btnm->btn_id_act]) == false &&
               button_is_inactive(btnm->ctrl_bits[btnm->btn_id_act]) == false &&
               button_is_hidden(btnm->ctrl_bits[btnm->btn_id_act]) == false) {
                uint32_t b = btnm->btn_id_act;
                res        = lv_event_send(obj, LV_EVENT_VALUE_CHANGED, &b);
            }
        }
    }
    else if(sign == LV_SIGNAL_PRESS_LOST) {
        btnm->btn_id_pr  = LV_BTNMATRIX_BTN_NONE;
        btnm->btn_id_act = LV_BTNMATRIX_BTN_NONE;
        lv_obj_invalidate(obj);
    }
    else if(sign == LV_SIGNAL_FOCUS) {
        lv_indev_t * indev         = lv_indev_get_act();
        lv_indev_type_t indev_type = lv_indev_get_type(indev);

        /*If not focused by an input device assume the last input device*/
        if(indev == NULL) {
            indev = lv_indev_get_next(NULL);
            indev_type = lv_indev_get_type(indev);
        }

        if(indev_type == LV_INDEV_TYPE_ENCODER) {
            /*In navigation mode don't select any button but in edit mode select the fist*/
            if(lv_group_get_editing(lv_obj_get_group(obj))) {
                uint32_t b = 0;
                while(button_is_hidden(btnm->ctrl_bits[b]) || button_is_inactive(btnm->ctrl_bits[b])) b++;
                btnm->btn_id_focused = b;
                btnm->btn_id_act = b;
            }
            else {
                btnm->btn_id_focused = LV_BTNMATRIX_BTN_NONE;
            }
        }
        else if(indev_type == LV_INDEV_TYPE_KEYPAD) {
            uint32_t b = 0;
            while(button_is_hidden(btnm->ctrl_bits[b]) || button_is_inactive(btnm->ctrl_bits[b])) {
                b++;
            }
            btnm->btn_id_focused = b;
            btnm->btn_id_act = b;
        }
    }
    else if(sign == LV_SIGNAL_DEFOCUS || sign == LV_SIGNAL_LEAVE) {
        if(btnm->btn_id_focused != LV_BTNMATRIX_BTN_NONE) invalidate_button_area(obj, btnm->btn_id_focused);
        if(btnm->btn_id_pr != LV_BTNMATRIX_BTN_NONE) invalidate_button_area(obj, btnm->btn_id_pr);
        btnm->btn_id_focused = LV_BTNMATRIX_BTN_NONE;
        btnm->btn_id_pr = LV_BTNMATRIX_BTN_NONE;
        btnm->btn_id_act = LV_BTNMATRIX_BTN_NONE;
    }
    else if(sign == LV_SIGNAL_CONTROL) {
        char c = *((char *)param);
        if(c == LV_KEY_RIGHT) {
            if(btnm->btn_id_focused == LV_BTNMATRIX_BTN_NONE)  btnm->btn_id_focused = 0;
            else btnm->btn_id_focused++;
            if(btnm->btn_id_focused >= btnm->btn_cnt) btnm->btn_id_focused = 0;

            while(button_is_hidden(btnm->ctrl_bits[btnm->btn_id_focused]) || button_is_inactive(btnm->ctrl_bits[btnm->btn_id_focused])) {
                btnm->btn_id_focused++;
                if(btnm->btn_id_focused >= btnm->btn_cnt) btnm->btn_id_focused = 0;
            }

            btnm->btn_id_act = btnm->btn_id_focused;
            lv_obj_invalidate(obj);
        }
        else if(c == LV_KEY_LEFT) {
            if(btnm->btn_id_focused == LV_BTNMATRIX_BTN_NONE) btnm->btn_id_focused = 0;
            if(btnm->btn_id_focused > 0) btnm->btn_id_focused--;

            while(button_is_hidden(btnm->ctrl_bits[btnm->btn_id_focused]) || button_is_inactive(btnm->ctrl_bits[btnm->btn_id_focused])) {
                if(btnm->btn_id_focused > 0) btnm->btn_id_focused--;
                else btnm->btn_id_focused = btnm->btn_cnt - 1;
            }

            btnm->btn_id_act = btnm->btn_id_focused;
            lv_obj_invalidate(obj);
        }
        else if(c == LV_KEY_DOWN) {
            lv_coord_t col_gap = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);
            /*Find the area below the the current*/
            if(btnm->btn_id_focused == LV_BTNMATRIX_BTN_NONE) {
                btnm->btn_id_focused = 0;
            }
            else {
                uint16_t area_below;
                lv_coord_t pr_center =
                    btnm->button_areas[btnm->btn_id_focused].x1 + (lv_area_get_width(&btnm->button_areas[btnm->btn_id_focused]) >> 1);

                for(area_below = btnm->btn_id_focused; area_below < btnm->btn_cnt; area_below++) {
                    if(btnm->button_areas[area_below].y1 > btnm->button_areas[btnm->btn_id_focused].y1 &&
                       pr_center >= btnm->button_areas[area_below].x1 &&
                       pr_center <= btnm->button_areas[area_below].x2 + col_gap &&
                       button_is_inactive(btnm->ctrl_bits[area_below]) == false &&
                       button_is_hidden(btnm->ctrl_bits[area_below]) == false) {
                        break;
                    }
                }

                if(area_below < btnm->btn_cnt) btnm->btn_id_focused = area_below;
            }
            btnm->btn_id_act = btnm->btn_id_focused;
            lv_obj_invalidate(obj);
        }
        else if(c == LV_KEY_UP) {
            lv_coord_t col_gap = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);
            /*Find the area below the the current*/
            if(btnm->btn_id_focused == LV_BTNMATRIX_BTN_NONE) {
                btnm->btn_id_focused = 0;
            }
            else {
                int16_t area_above;
                lv_coord_t pr_center =
                    btnm->button_areas[btnm->btn_id_focused].x1 + (lv_area_get_width(&btnm->button_areas[btnm->btn_id_focused]) >> 1);

                for(area_above = btnm->btn_id_focused; area_above >= 0; area_above--) {
                    if(btnm->button_areas[area_above].y1 < btnm->button_areas[btnm->btn_id_focused].y1 &&
                       pr_center >= btnm->button_areas[area_above].x1 - col_gap &&
                       pr_center <= btnm->button_areas[area_above].x2 &&
                       button_is_inactive(btnm->ctrl_bits[area_above]) == false &&
                       button_is_hidden(btnm->ctrl_bits[area_above]) == false) {
                        break;
                    }
                }
                if(area_above >= 0) btnm->btn_id_focused = area_above;
            }
            btnm->btn_id_act = btnm->btn_id_focused;
            lv_obj_invalidate(obj);
        }
    }
    return res;
}

/**
 * Create the required number of buttons and control bytes according to a map
 * @param obj pointer to button matrix object
 * @param map_p pointer to a string array
 */
static void allocate_btn_areas_and_controls(const lv_obj_t * obj, const char ** map)
{
    /*Count the buttons in the map*/
    uint16_t btn_cnt = 0;
    uint16_t i       = 0;
    while(strlen(map[i]) != 0) {
        if(strcmp(map[i], "\n") != 0) { /*Do not count line breaks*/
            btn_cnt++;
        }
        i++;
    }

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;

    /*Do not allocate memory for the same amount of buttons*/
    if(btn_cnt == btnm->btn_cnt) return;

    if(btnm->button_areas != NULL) {
        lv_mem_free(btnm->button_areas);
        btnm->button_areas = NULL;
    }
    if(btnm->ctrl_bits != NULL) {
        lv_mem_free(btnm->ctrl_bits);
        btnm->ctrl_bits = NULL;
    }

    btnm->button_areas = lv_mem_alloc(sizeof(lv_area_t) * btn_cnt);
    LV_ASSERT_MEM(btnm->button_areas);
    btnm->ctrl_bits = lv_mem_alloc(sizeof(lv_btnmatrix_ctrl_t) * btn_cnt);
    LV_ASSERT_MEM(btnm->ctrl_bits);
    if(btnm->button_areas == NULL || btnm->ctrl_bits == NULL) btn_cnt = 0;

    lv_memset_00(btnm->ctrl_bits, sizeof(lv_btnmatrix_ctrl_t) * btn_cnt);

    btnm->btn_cnt = btn_cnt;
}

/**
 * Get the width of a button in units (default is 1).
 * @param ctrl_bits least significant 3 bits used (1..7 valid values)
 * @return the width of the button in units
 */
static uint8_t get_button_width(lv_btnmatrix_ctrl_t ctrl_bits)
{
    uint8_t w = ctrl_bits & LV_BTNMATRIX_WIDTH_MASK;
    return w != 0 ? w : 1;
}

static bool button_is_hidden(lv_btnmatrix_ctrl_t ctrl_bits)
{
    return (ctrl_bits & LV_BTNMATRIX_CTRL_HIDDEN) ? true : false;
}

static bool button_is_repeat_disabled(lv_btnmatrix_ctrl_t ctrl_bits)
{
    return (ctrl_bits & LV_BTNMATRIX_CTRL_NO_REPEAT) ? true : false;
}

static bool button_is_inactive(lv_btnmatrix_ctrl_t ctrl_bits)
{
    return (ctrl_bits & LV_BTNMATRIX_CTRL_DISABLED) ? true : false;
}

static bool button_is_click_trig(lv_btnmatrix_ctrl_t ctrl_bits)
{
    return (ctrl_bits & LV_BTNMATRIX_CTRL_CLICK_TRIG) ? true : false;
}

static bool button_is_tgl_enabled(lv_btnmatrix_ctrl_t ctrl_bits)
{
    return (ctrl_bits & LV_BTNMATRIX_CTRL_CHECKABLE) ? true : false;
}

static bool button_get_checked(lv_btnmatrix_ctrl_t ctrl_bits)
{
    return (ctrl_bits & LV_BTNMATRIX_CTRL_CHECKED) ? true : false;
}

/**
 * Gives the button id of a button under a given point
 * @param obj pointer to a button matrix object
 * @param p a point with absolute coordinates
 * @return the id of the button or LV_BTNMATRIX_BTN_NONE.
 */
static uint16_t get_button_from_point(lv_obj_t * obj, lv_point_t * p)
{
    lv_area_t obj_cords;
    lv_area_t btn_area;
    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;
    uint16_t i;
    lv_obj_get_coords(obj, &obj_cords);

    lv_coord_t w = lv_obj_get_width(obj);
    lv_coord_t h = lv_obj_get_height(obj);
    lv_coord_t pleft = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
    lv_coord_t pright = lv_obj_get_style_pad_right(obj, LV_PART_MAIN);
    lv_coord_t ptop = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
    lv_coord_t pbottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
    lv_coord_t prow = lv_obj_get_style_pad_row(obj, LV_PART_MAIN);
    lv_coord_t pcol = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);

    /*Get the half gap. Button look larger with this value. (+1 for rounding error)*/
    prow = (prow / 2) + 1 + (prow & 1);
    pcol = (pcol / 2) + 1 + (pcol & 1);

    prow = LV_MIN(prow, BTN_EXTRA_CLICK_AREA_MAX);
    pcol = LV_MIN(pcol, BTN_EXTRA_CLICK_AREA_MAX);
    pright = LV_MIN(pright, BTN_EXTRA_CLICK_AREA_MAX);
    ptop = LV_MIN(ptop, BTN_EXTRA_CLICK_AREA_MAX);
    pbottom = LV_MIN(pbottom, BTN_EXTRA_CLICK_AREA_MAX);

    for(i = 0; i < btnm->btn_cnt; i++) {
        lv_area_copy(&btn_area, &btnm->button_areas[i]);
        if(btn_area.x1 <= pleft) btn_area.x1 += obj_cords.x1 - LV_MIN(pleft, BTN_EXTRA_CLICK_AREA_MAX);
        else btn_area.x1 += obj_cords.x1 - pcol;

        if(btn_area.y1 <= ptop) btn_area.y1 += obj_cords.y1 - LV_MIN(ptop, BTN_EXTRA_CLICK_AREA_MAX);
        else btn_area.y1 += obj_cords.y1 - prow;

        if(btn_area.x2 >= w - pright - 2) btn_area.x2 += obj_cords.x1 + LV_MIN(pright,
                                                                                         BTN_EXTRA_CLICK_AREA_MAX);  /*-2 for rounding error*/
        else btn_area.x2 += obj_cords.x1 + pcol;

        if(btn_area.y2 >= h - pbottom - 2) btn_area.y2 += obj_cords.y1 + LV_MIN(pbottom,
                                                                                          BTN_EXTRA_CLICK_AREA_MAX); /*-2 for rounding error*/
        else btn_area.y2 += obj_cords.y1 + prow;

        if(_lv_area_is_point_on(&btn_area, p, 0) != false) {
            break;
        }
    }

    if(i == btnm->btn_cnt) i = LV_BTNMATRIX_BTN_NONE;

    return i;
}

static void invalidate_button_area(const lv_obj_t * obj, uint16_t btn_idx)
{
    if(btn_idx == LV_BTNMATRIX_BTN_NONE) return;

    lv_area_t btn_area;
    lv_area_t obj_area;

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;;
    lv_area_copy(&btn_area, &btnm->button_areas[btn_idx]);
    lv_obj_get_coords(obj, &obj_area);

    /* Convert relative coordinates to absolute */
    btn_area.x1 += obj_area.x1;
    btn_area.y1 += obj_area.y1;
    btn_area.x2 += obj_area.x1;
    btn_area.y2 += obj_area.y1;

    lv_obj_invalidate_area(obj, &btn_area);
}

/**
 * Enforces a single button being toggled on the button matrix.
 * It simply clears the toggle flag on other buttons.
 * @param obj Button matrix object
 * @param btn_idx Button that should remain toggled
 */
static void make_one_button_checked(lv_obj_t * obj, uint16_t btn_idx)
{
    /*Save whether the button was toggled*/
    bool was_toggled = lv_btnmatrix_has_btn_ctrl(obj, btn_idx, LV_BTNMATRIX_CTRL_CHECKED);

    lv_btnmatrix_clear_btn_ctrl_all(obj, LV_BTNMATRIX_CTRL_CHECKED);

    if(was_toggled) lv_btnmatrix_set_btn_ctrl(obj, btn_idx, LV_BTNMATRIX_CTRL_CHECKED);
}

#endif
