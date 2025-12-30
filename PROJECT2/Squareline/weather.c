#include "weather.h"

// replyData:{"reason":"查询成功!","result":{"city":"深圳","realtime":{"temperature":"22","humidity":"51","info":"多云","wid":"01","direct":"西南风","power":"1级","aqi":"54"},"future":[{"date":"2025-12-30","temperature":"16\/23℃","weather":"晴","wid":{"day":"00","night":"00"},"direct":"持续无风向"},{"date":"2025-12-31","temperature":"16\/23℃","weather":"阴","wid":{"day":"02","night":"02"},"direct":"持续无风向"},{"date":"2026-01-01","temperature":"11\/16℃","weather":"多云","wid":{"day":"01","night":"01"},"direct":"持续无风向"},{"date":"2026-01-02","temperature":"8\/16℃","weather":"晴","wid":{"day":"00","night":"00"},"direct":"持续无风向"},{"date":"2026-01-03","temperature":"10\/16℃","weather":"晴","wid":{"day":"00","night":"00"},"direct":"持续无风向"}]},"error_code":0}

static lv_img_dsc_t *weather_img[] = {
    &ui_img_frame_27_png, //晴
    &ui_img_1298383682,   //多云
    &ui_img_frame_31_png, //阴
    &ui_img_frame_40_png, //小雨
    &ui_img_frame_32_png, //中雨
    &ui_img_frame_33_png, //大雨
    &ui_img_frame_35_png, //雷阵雨
    &ui_img_frame_36_png, //雨夹雪
    &ui_img_frame_38_png, //小雪
    &ui_img_frame_41_png, //中雪
    &ui_img_frame_42_png, //大雪
    &ui_img_701658425,    //雾
    &ui_img_1938478117    //霾

};

static int weather_code(const char *info)
{
    if (!info) return 0;
    if (strstr(info, "晴"))        return 0;
    if (strstr(info, "多云"))      return 1;
    if (strstr(info, "阴"))        return 2;
    if (strstr(info, "小雨"))      return 3;
    if (strstr(info, "中雨"))      return 4;
    if (strstr(info, "大雨"))      return 5;
    if (strstr(info, "雷阵雨"))    return 6;
    if (strstr(info, "雨夹雪"))    return 7;
    if (strstr(info, "小雪"))      return 8;
    if (strstr(info, "中雪"))      return 9;
    if (strstr(info, "大雪"))      return 10;
    if (strstr(info, "雾"))        return 11;
    if (strstr(info, "霾"))        return 12;
    return 0;
}

void ShowInfo(char *replyData)
{
    if (!replyData) return;

    /* 1. 先复制到本地，防止 cJSON 原地写只读内存 */
    size_t len = strlen(replyData) + 1;
    char *buf = malloc(len);
    if (!buf) return;
    memcpy(buf, replyData, len);

    cJSON *root = cJSON_Parse(buf);   // 用可写副本
    free(buf);                        // 复制完即可释放
    if (!root) return;

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!result) goto end;

    /* 2. 今日天气（全部字段判空） */
    cJSON *city = cJSON_GetObjectItem(result, "city");
    cJSON *real = cJSON_GetObjectItem(result, "realtime");
    if (city && real) {
        cJSON *tem   = cJSON_GetObjectItem(real, "temperature");
        cJSON *hum   = cJSON_GetObjectItem(real, "humidity");
        cJSON *info  = cJSON_GetObjectItem(real, "info");
        cJSON *power = cJSON_GetObjectItem(real, "power");
        cJSON *direct= cJSON_GetObjectItem(real, "direct");
        cJSON *aqi   = cJSON_GetObjectItem(real, "aqi");

        char today[256];
        snprintf(today, sizeof(today),
                 "%s\n"
                 "%s\n"
                 "温度:%s℃\n"
                 "湿度:%s%%\n"
                 "%s%s\n"
                 "空气质量%s",
                 city->valuestring  ? city->valuestring  : "",
                 info->valuestring  ? info->valuestring  : "",
                 tem->valuestring   ? tem->valuestring   : "",
                 hum->valuestring   ? hum->valuestring   : "",
                 power->valuestring ? power->valuestring : "",
                 direct->valuestring? direct->valuestring: "",
                 (aqi && aqi->valuestring) ?
                    (atoi(aqi->valuestring) <= 50 ? "优秀" : "良好") : "--");
        printf("info：%s\n", info->valuestring);
        lv_img_set_src(ui_ImageSunny, weather_img[weather_code(info->valuestring)]);
        lv_label_set_text(ui_LabelWeatertoday, today);
    }

    /* 3. 未来5天（同样全部判空） */
    cJSON *future = cJSON_GetObjectItem(result, "future");
    if (future) {
        char futureBuf[512] = {0};
        int size = cJSON_GetArraySize(future);
        for (int i = 0; i < 5 && i < size; i++) {
            cJSON *day  = cJSON_GetArrayItem(future, i);
            if (!day) continue;
            cJSON *date = cJSON_GetObjectItem(day, "date");
            cJSON *temp = cJSON_GetObjectItem(day, "temperature");
            cJSON *wea  = cJSON_GetObjectItem(day, "weather");
            if (date && temp && wea) {
                char line[64];
                snprintf(line, sizeof(line), "%s 温度%s %s\n",
                         date->valuestring,
                         temp->valuestring,
                         wea->valuestring);
                strcat(futureBuf, line);
            }
        }
        lv_label_set_text(ui_LabelWeaterfuther, futureBuf);
    }

end:
    cJSON_Delete(root);
}



void getWeather(char *city)
{
    char url[256];
    sprintf(url, "%s?city=%s&key=%s", WEATHER_URL, city, KEY);
    http_init(url);
    http_setRawHeader("Host", "apis.juhe.cn");
    http_get();

    char *raw = NULL;
    http_reply(&raw);          // 可能指向内部只读缓冲区
    if (!raw) { http_destory(); return; }

    size_t len = strlen(raw) + 1;
    char *writable = malloc(len);
    if (writable) {
        memcpy(writable, raw, len);
        // printf("replyData:%s\n", writable);
        ShowInfo(writable);    // 用可写副本
        free(writable);
    }
    http_destory();
}

