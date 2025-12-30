#ifndef __USER_H__
#define __USER_H__

#include "ui.h"

// CUSTOM CODE AREA
typedef struct list
{
    struct list *next;
    struct list *prev;   
}list_t, *list_p;

typedef struct 
{
    char username[20];
    char password[20];
    list_t list;
}user_t, *user_p;

int loginUser( );
void registerUser( );
user_p UserInit();
user_p UserCreat(char *username,char *password);
void  ListAdd(list_p new,list_p pre);
void my_timer(lv_timer_t * timer);
void UserCleanup(void);
void ReadFromFile(const char* filename, user_p head);
void Write2File(const char* filename, user_p head);
void create_auto_destroy_timer(lv_timer_cb_t cb, uint32_t period) ;
void my_timer(lv_timer_t * t);
// CUSTOM VARIABLES


/*将大小结构体转化*/
#define list_entry(ptr, type, member) \
((type *)((char *)(ptr)-(unsigned long long)(&((type *)0)->member)))


extern volatile char flag;
user_p head;
extern lv_timer_t *timer;


#endif 