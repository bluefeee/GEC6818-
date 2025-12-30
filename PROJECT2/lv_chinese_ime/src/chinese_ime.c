/**
 * @file lv_chinese_ime.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "../lv_chinese_ime.h"
#include "chinese_ime.h"

#if LV_CHINESE_IME
#include "cJSON.h"
#include <string.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_chinese_ime_constructor(lv_obj_t * obj);
static void lv_chinese_ime_def_event_cb(lv_event_t * e);
static void parse_match_dict(lv_obj_t * keyboard);
static void select_font_event_cb(lv_event_t * e);
static void lv_chinese_ime_destruct(void);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_chinese_ime_pt  lv_chinese_ime = NULL; // 全局单例

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Create a keyboard objects
 * @param par pointer to an object, it will be the parent of the new keyboard
 * @return pointer to the created keyboard
 */
lv_obj_t * lv_chinese_ime_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_keyboard_create(parent);
    if(obj == NULL) return NULL;

    lv_chinese_ime_constructor(obj);

    // BUG修复：传递键盘对象自身作为user_data
    lv_obj_add_event_cb(obj, lv_chinese_ime_def_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return obj;
}

/**
 * This is the inheritance and customization of "lv_obj_del".
 * @param obj       pointer to an object
 */
void lv_chinese_ime_del(lv_obj_t * obj)
{
    lv_chinese_ime_destruct();
    if(obj) lv_obj_del(obj);
}

/**
 * Set dict of input method
 * @param obj       pointer to an dict
 */
void lv_chinese_ime_set_dict(const char * dict)
{
    if(lv_chinese_ime == NULL) return;
    lv_chinese_ime->dict = dict;
}

/**
 * Similar to "lv_obj_set_style_text_font"
 * @param value       pointer to a font
 * @param selector    selector
 */
void lv_chinese_ime_set_text_font(const lv_font_t * value, lv_style_selector_t selector)
{
    if(lv_chinese_ime == NULL || lv_chinese_ime->font_panel == NULL) return;

    // 核心修复：存储字体到结构体，确保后续创建标签时能获取
    lv_chinese_ime->font = value;
    
    // 设置面板字体（仅作为备份）
    lv_obj_set_style_text_font(lv_chinese_ime->font_panel, value, selector);
    
    // 遍历更新现有标签字体（动态切换时生效）
    uint32_t child_cnt = lv_obj_get_child_cnt(lv_chinese_ime->font_panel);
    for(uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t * label = lv_obj_get_child(lv_chinese_ime->font_panel, i);
        if(label && lv_obj_check_type(label, &lv_label_class)) {
            lv_obj_set_style_text_font(label, value, selector);
        }
    }
}

/*=====================
 * Other functions
 *====================*/

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * This is the inheritance and customization of "lv_keyboard_def_event_cb".
 * @param kb pointer to a keyboard
 * @param event the triggering event
 */
static void lv_chinese_ime_def_event_cb(lv_event_t * e)
{
    if(lv_chinese_ime == NULL || lv_chinese_ime->font_panel == NULL) return;

    lv_obj_t * obj = lv_event_get_target(e);
    if(obj == NULL) return;

    uint16_t btn_id   = lv_btnmatrix_get_selected_btn(obj);
    if(btn_id == LV_BTNMATRIX_BTN_NONE) return;

    const char * txt = lv_btnmatrix_get_btn_text(obj, btn_id);
    if(txt == NULL) return;

    if(strcmp(txt, "Enter") == 0 || strcmp(txt, LV_SYMBOL_NEW_LINE) == 0) {
        lv_obj_clean(lv_chinese_ime->font_panel);
        lv_chinese_ime->ta_count = 0;
        memset(lv_chinese_ime->input_char, 0, sizeof(lv_chinese_ime->input_char));
    }
    else if(strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        int input_len = strlen(lv_chinese_ime->input_char);
        if(input_len > 0) {
            lv_chinese_ime->input_char[input_len - 1] = '\0';
            parse_match_dict(obj);
            lv_chinese_ime->ta_count = (lv_chinese_ime->ta_count > 0) ? (lv_chinese_ime->ta_count - 1) : 0;
        }
    }
    else if ((txt[0] >= 'a' && txt[0] <= 'z') || (txt[0] >= 'A' && txt[0] <= 'Z')){
        if(strlen(lv_chinese_ime->input_char) < (sizeof(lv_chinese_ime->input_char) - 1)) {
            strcat(lv_chinese_ime->input_char, txt);
            parse_match_dict(obj);
            lv_chinese_ime->ta_count++;
        }
    }
}

/**
 * Triggered when a font is selected.
 * @param kb pointer to a keyboard
 * @param event the triggering event
 */
static void select_font_event_cb(lv_event_t * e)
{
    if(lv_chinese_ime == NULL || lv_chinese_ime->font_panel == NULL) return;

    lv_obj_t * obj = lv_event_get_target(e);
    if(obj == NULL) return;



    // BUG修复：从全局变量获取键盘对象
    lv_obj_t * keyboard = lv_chinese_ime->keyboard;
    if(keyboard == NULL) return;

    lv_obj_t * ta = lv_keyboard_get_textarea(keyboard);
    if(ta == NULL) return;

    // 删除已输入的拼音
    int del_count = (lv_chinese_ime->ta_count < 0) ? 0 : lv_chinese_ime->ta_count;
    for (int i = 0; i < del_count; i++) {
        lv_textarea_del_char(ta);
    }

    // 插入选中的汉字
    const char * label_text = lv_label_get_text(obj);
    if(label_text != NULL && strlen(label_text) > 0) {
        lv_textarea_add_text(ta, label_text);
    }

    // 清空候选面板
    lv_obj_clean(lv_chinese_ime->font_panel);
    lv_chinese_ime->ta_count = 0;
    memset(lv_chinese_ime->input_char, 0, sizeof(lv_chinese_ime->input_char));
}

/**
 * Parse and match dict (based on cJson)
 * @param text
 * @param label
 */
static void parse_match_dict(lv_obj_t * kb)
{
    if(lv_chinese_ime == NULL || lv_chinese_ime->dict == NULL) return;

    cJSON* cjson_parse_result = NULL;
    cJSON* cjson_skill = NULL;
    cJSON* cjson_skill_item = NULL;
    int    skill_array_size = 0, i = 0;

    /* 解析整段JSON数据 */
    cjson_parse_result = cJSON_Parse(lv_chinese_ime->dict);
    if(cjson_parse_result == NULL) {
        printf("parse fail.\n");
        return;
    }

    /* 清空并重建候选面板 */
    lv_obj_t * label;
    lv_obj_clean(lv_chinese_ime->font_panel);
    
    cjson_skill = cJSON_GetObjectItem(cjson_parse_result, lv_chinese_ime->input_char);
    if(cjson_skill == NULL || !cJSON_IsArray(cjson_skill)) {
        cJSON_Delete(cjson_parse_result);
        return;
    }

    skill_array_size = cJSON_GetArraySize(cjson_skill);
    
    // 核心修复：优先使用结构体中存储的字体
    const lv_font_t * font = lv_chinese_ime->font;
    if(font == NULL) {
        // 如果未设置，使用面板字体作为备选
        font = lv_obj_get_style_text_font(lv_chinese_ime->font_panel, 0);
    }
    
    for(i = 0; i < skill_array_size; i++) {
        cjson_skill_item = cJSON_GetArrayItem(cjson_skill, i);
        if(cjson_skill_item == NULL || cjson_skill_item->valuestring == NULL) {
            continue;
        }

        /* 创建候选标签 */
        label = lv_label_create(lv_chinese_ime->font_panel);
        lv_label_set_text(label, cjson_skill_item->valuestring);
        
        // 应用存储的字体（确保不为NULL）
        if(font) {
            lv_obj_set_style_text_font(label, font, 0);
        }
        
        lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(label, select_font_event_cb, LV_EVENT_CLICKED, NULL);
    }
    cJSON_Delete(cjson_parse_result);
}
/**
 * lv_chinese_ime constructor
 * @param obj pointer to a keyboard
 */
static void lv_chinese_ime_constructor(lv_obj_t * obj)
{
    static lv_style_t panel_style;

    /* 释放旧内存 */
    if(lv_chinese_ime != NULL) {
        if(lv_chinese_ime->font_panel) lv_obj_del(lv_chinese_ime->font_panel);
        lv_mem_free(lv_chinese_ime);
    }

    /* 申请内存并初始化 */
    lv_chinese_ime = (lv_chinese_ime_t *)lv_mem_alloc(sizeof(lv_chinese_ime_t));
    if(lv_chinese_ime == NULL) return;

    memset(lv_chinese_ime, 0, sizeof(lv_chinese_ime_t));
    lv_chinese_ime->dict = zh_cn_pinyin_dict;
    lv_chinese_ime->ta_count = 0;
    memset(lv_chinese_ime->input_char, 0, sizeof(lv_chinese_ime->input_char));
    
    // BUG修复：存储键盘对象引用
    lv_chinese_ime->keyboard = obj;

    /* 候选面板样式 */
    lv_style_init(&panel_style);
    lv_style_set_bg_opa(&panel_style, 0);
    lv_style_set_border_opa(&panel_style, 0);
    lv_style_set_radius(&panel_style, 0);
    lv_style_set_pad_all(&panel_style, 0);

    /* 创建候选面板 */
    lv_chinese_ime->font_panel = lv_obj_create(lv_scr_act());
    if(lv_chinese_ime->font_panel == NULL) return;

    lv_obj_set_size(lv_chinese_ime->font_panel, LV_PCT(100), LV_PCT(12));
    lv_obj_set_flex_flow(lv_chinese_ime->font_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lv_chinese_ime->font_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align_to(lv_chinese_ime->font_panel, obj, LV_ALIGN_OUT_TOP_MID, 0, 0);
    lv_obj_add_style(lv_chinese_ime->font_panel, &panel_style, 0);
}

/**
 * lv_chinese_ime destruct
 */
static void lv_chinese_ime_destruct(void)
{
    if(lv_chinese_ime != NULL) {
        if(lv_chinese_ime->font_panel) lv_obj_del(lv_chinese_ime->font_panel);
        lv_mem_free(lv_chinese_ime);
        lv_chinese_ime = NULL; // 置空防止野指针
    }
}

#endif  /*LV_CHINESE_IME*/
