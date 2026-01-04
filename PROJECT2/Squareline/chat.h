#ifndef __CHAT_H__
#define __CHAT_H__

#include "ui.h"
#include <string.h>

// 聊天记录数据结构体
typedef struct {
    int avatar_img;     // 头像
    char username[20];     // 用户名
    char msg_content[1024];  // 消息内容
} ChatMsg_t;

typedef struct {
    char title[20];                // 聊天标题
    ChatMsg_t msg_list[20];           // 存储最多20条聊天记录
    int msg_count;                    // 当前消息数量
} ChatSession_t;

extern lv_img_dsc_t *avatar_img[5];  // 头像数组

// ChatSession_t g_chat_session_15 = {
//     .title = "技术交流群",  // 聊天标题
//     .msg_list = {0},  // 初始无消息
//     .msg_count = 0,  // 初始无消息
// };
extern char chat_obj[20]; // 聊天对象

//函数声明
void clear_chat_panel(void);
void add_chat_msg_to_panel(ChatMsg_t *msg);
void chat_init(char *username);
extern void send_message_to_server(const char *content, const char *chat_obj);
extern int server_init();
#endif // !__CHAT_H__