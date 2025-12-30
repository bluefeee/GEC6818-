#ifndef __WEATHER_H__
#define __WEATHER_H__
#include "ui.h"
#include "http.h"
#include "cJSON.h"


#define WEATHER_URL "http://apis.juhe.cn/simpleWeather/query"
#define KEY "9559bed4b5a407b3c60015860167fac3"

#ifndef __WEATHERVALUE_H__
#define __WEATHERVALUE_H__


char city[32];
#endif // __WEATHERVALUE_H__

void getWeather(char* city);


#endif // __WEATHER_H__