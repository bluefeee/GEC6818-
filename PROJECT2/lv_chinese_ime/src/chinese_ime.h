/**
 * @file chinese_ime.h
 *
 */

#ifndef CHINESE_IME_H
#define CHINESE_IME_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "../../lvgl/lvgl.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
/*Data of lv_chinese_ime*/
typedef struct {
    lv_obj_t * font_panel;      /* 候选词面板 */
    lv_obj_t * keyboard;        /* 键盘对象指针 */
    const char * dict;          /* 词库数据 */
    const lv_font_t * font;     /* BUG修复：显式存储字体指针 */
    char input_char[16];        /* 当前输入的拼音 */
    int ta_count;               /* 需要删除的字符数 */
} lv_chinese_ime_t, *lv_chinese_ime_pt;

/***********************
 * GLOBAL VARIABLES
 ***********************/
extern const char * zh_cn_pinyin_dict;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create lv_chinese_ime (keyboard objects)
 * @param par pointer to an object, it will be the parent of the new keyboard
 * @return pointer to the created keyboard
 */
lv_obj_t * lv_chinese_ime_create(lv_obj_t * parent);

/**
 * This is the inheritance and customization of "lv_obj_del".
 * @param obj       pointer to an object
 */
void lv_chinese_ime_del(lv_obj_t * obj);

/**
 * Set dict of input method
 * @param obj       pointer to an dict
 */
void lv_chinese_ime_set_dict(const char * dict);

/**
 * Similar to "lv_obj_set_style_text_font"
 * @param value       pointer to a font
 * @param selector    selector
 */
void lv_chinese_ime_set_text_font(const lv_font_t * value, lv_style_selector_t selector);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*CHINESE_IME_H*/
