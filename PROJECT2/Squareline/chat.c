#include "chat.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/socket.h"
#include "arpa/inet.h"
#include "unistd.h"
#include "pthread.h"
#include "sys/types.h"
#include "errno.h"

#define DEFAULT_PORT   4321
#define BUF_SIZE       1024
#define MAX_USER_COUNT 5
#define ALIYUN_SERVER_IP "47.110.72.112"

// 当前登录用户外部变量
extern char login_user[32]; 
extern int login_avatar_img;

/* [1-5]动态填充在线用户 */
char chat_obj[6][32] = {"群聊"};
int chat_obj_index = 0;
char current_chat_title[20] = "群聊";
int client_socket = -1;

/* 头像资源（彩色和灰色） */
lv_img_dsc_t *avatar_img[5] = {
    &ui_img_funina1_png, &ui_img_keqing1_png,
    &ui_img_linnite1_png, &ui_img_paimen1_png, &ui_img_qiqi1_png
};

lv_img_dsc_t *avatar_gray_img[5] = {
    &ui_img_funina2_png, &ui_img_keqing2_png,
    &ui_img_linnite2_png, &ui_img_paimen2_png, &ui_img_qiqi2_png
};

/* 会话池（动态标题） */
ChatSession_t chat_sessions[MAX_USER_COUNT];
ChatSession_t group_chat_session = {.title = "群聊", .msg_list = {0}, .msg_count = 0};

pthread_mutex_t msg_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 用户状态：在线/离线 + 固定头像索引 */
typedef struct {
    char username[32];
    int is_online;
    int avatar_img; // 固定的头像索引（0-4）
} UserStatus;
static UserStatus user_status[MAX_USER_COUNT] = {0};

/* ====== 发送用户名到服务器 ====== */
static int send_username_to_server(int sockfd)
{
    char user_buf[32];
    snprintf(user_buf, sizeof(user_buf), "%s\n", login_user);
    if (send(sockfd, user_buf, strlen(user_buf), MSG_NOSIGNAL) <= 0) {
        perror("Send username failed");
        return -1;
    }
    return 0;
}

/* ====== 解析在线成员列表并更新UI（核心修复） ====== */
static void parse_online_users(const char *buf)
{
    if (strncmp(buf, "当前在线成员:", 19) != 0) return;
    
    pthread_mutex_lock(&ui_mutex);
    
    // 1. 重置所有状态为离线（不重置username和avatar_img）
    for (int i = 0; i < MAX_USER_COUNT; i++) {
        user_status[i].is_online = 0;
    }
    
    // 2. 显示所有用户为灰色（使用固定的avatar_img）
    for (int i = 0; i < MAX_USER_COUNT; i++) {
        if (user_status[i].username[0] != '\0') {
            switch(user_status[i].avatar_img) {
                case 0:
                    lv_img_set_src(ui_Image29, avatar_gray_img[0]);
                    lv_obj_clear_flag(ui_Image29, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(ui_Label15, user_status[i].username);
                    lv_obj_clear_flag(ui_Label15, LV_OBJ_FLAG_HIDDEN);
                    break;
                case 1:
                    lv_img_set_src(ui_Image31, avatar_gray_img[1]);
                    lv_obj_clear_flag(ui_Image31, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(ui_Label16, user_status[i].username);
                    lv_obj_clear_flag(ui_Label16, LV_OBJ_FLAG_HIDDEN);
                    break;
                case 2:
                    lv_img_set_src(ui_Image32, avatar_gray_img[2]);
                    lv_obj_clear_flag(ui_Image32, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(ui_Label17, user_status[i].username);
                    lv_obj_clear_flag(ui_Label17, LV_OBJ_FLAG_HIDDEN);
                    break;
                case 3:
                    lv_img_set_src(ui_Image33, avatar_gray_img[3]);
                    lv_obj_clear_flag(ui_Image33, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(ui_Label18, user_status[i].username);
                    lv_obj_clear_flag(ui_Label18, LV_OBJ_FLAG_HIDDEN);
                    break;
                case 4:
                    lv_img_set_src(ui_Image34, avatar_gray_img[4]);
                    lv_obj_clear_flag(ui_Image34, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(ui_Label19, user_status[i].username);
                    lv_obj_clear_flag(ui_Label19, LV_OBJ_FLAG_HIDDEN);
                    break;
            }
        }
    }
    
    // 3. 手动解析在线用户并切换为彩色
    const char *p = buf + 19;
    while (*p == ' ') p++;
    
    char token[32];
    
    while (*p != '\0' && *p != '\n') {
        int len = 0;
        while (*p != ' ' && *p != '\n' && *p != '\0' && len < 31) {
            token[len++] = *p++;
        }
        token[len] = '\0';
        
        if (len > 0 && strcmp(token, login_user) != 0) {
            printf("在线用户: %s\n", token);
            
            // 查找该用户名在 user_status 中的索引
            int user_idx = -1;
            for (int i = 0; i < MAX_USER_COUNT; i++) {
                if (strcmp(user_status[i].username, token) == 0) {
                    user_idx = i;  // 已存在，使用固定槽位
                    break;
                }
            }
            
            // 如果不存在，找到第一个空槽位
            if (user_idx == -1) {
                for (int i = 0; i < MAX_USER_COUNT; i++) {
                    if (user_status[i].username[0] == '\0') {
                        user_idx = i;
                        break;
                    }
                }
            }
            
            if (user_idx != -1) {
                // 更新用户名（如果是新用户）和状态
                if (user_status[user_idx].username[0] == '\0') {
                    strncpy(user_status[user_idx].username, token, sizeof(user_status[user_idx].username) - 1);
                    strncpy(chat_sessions[user_idx].title, token, sizeof(chat_sessions[user_idx].title) - 1);
                    strncpy(chat_obj[user_idx + 1], token, sizeof(chat_obj[user_idx + 1]) - 1);
                }
                user_status[user_idx].is_online = 1;
                
                // 使用固定的avatar_img切换彩色头像
                switch(user_status[user_idx].avatar_img) {
                    case 0:
                        lv_label_set_text(ui_Label15, token);
                        lv_img_set_src(ui_Image29, avatar_img[0]);
                        break;
                    case 1:
                        lv_label_set_text(ui_Label16, token);
                        lv_img_set_src(ui_Image31, avatar_img[1]);
                        break;
                    case 2:
                        lv_label_set_text(ui_Label17, token);
                        lv_img_set_src(ui_Image32, avatar_img[2]);
                        break;
                    case 3:
                        lv_label_set_text(ui_Label18, token);
                        lv_img_set_src(ui_Image33, avatar_img[3]);
                        break;
                    case 4:
                        lv_label_set_text(ui_Label19, token);
                        lv_img_set_src(ui_Image34, avatar_img[4]);
                        break;
                }
            }
        }
        
        while (*p == ' ') p++;
    }
    
    pthread_mutex_unlock(&ui_mutex);
}

/* ====== 连接服务器 ====== */
int client_connect_to_aliyun(void)
{
    if (client_socket >= 0) return 0;

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Socket creation error");
        return -1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DEFAULT_PORT);
    if (inet_pton(AF_INET, ALIYUN_SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid server IP");
        close(client_socket);
        client_socket = -1;
        return -1;
    }

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        close(client_socket);
        client_socket = -1;
        return -1;
    }

    printf("Connected to server %s:%d\n", ALIYUN_SERVER_IP, DEFAULT_PORT);
    return send_username_to_server(client_socket);
}

/* ====== 工具函数 ====== */
static int get_private_session_index(const char* title)
{
    for (int i = 0; i < MAX_USER_COUNT; ++i)
        if (strcmp(title, chat_sessions[i].title) == 0)
            return i;
    return -1;
}

static void add_msg_to_private_session(const char* title, ChatMsg_t* msg)
{
    int idx = get_private_session_index(title);
    if (idx == -1) return;
    pthread_mutex_lock(&msg_mutex);
    if (chat_sessions[idx].msg_count < 20)
        chat_sessions[idx].msg_list[chat_sessions[idx].msg_count++] = *msg;
    pthread_mutex_unlock(&msg_mutex);
}

static void add_msg_to_group_session(ChatMsg_t* msg)
{
    pthread_mutex_lock(&msg_mutex);
    if (group_chat_session.msg_count < 20)
        group_chat_session.msg_list[group_chat_session.msg_count++] = *msg;
    pthread_mutex_unlock(&msg_mutex);
}

static void show_current_session_msgs(void)
{
    pthread_mutex_lock(&msg_mutex);
    if (strcmp(current_chat_title, "群聊") == 0) {
        for (int i = 0; i < group_chat_session.msg_count; ++i)
            add_chat_msg_to_panel(&group_chat_session.msg_list[i]);
    } else {
        int idx = get_private_session_index(current_chat_title);
        if (idx != -1)
            for (int i = 0; i < chat_sessions[idx].msg_count; ++i)
                add_chat_msg_to_panel(&chat_sessions[idx].msg_list[i]);
    }
    pthread_mutex_unlock(&msg_mutex);
}

/* ====== LVGL UI接口 ====== */
void chat_init(char *username)
{
    strncpy(current_chat_title, username, sizeof(current_chat_title) - 1);
    clear_chat_panel();
    
    lv_obj_t *chat_title = lv_label_create(ui_Panel10);
    lv_obj_set_width(chat_title, lv_pct(100));
    lv_obj_set_height(chat_title, LV_SIZE_CONTENT);
    lv_obj_set_align(chat_title, LV_ALIGN_CENTER);
    char buf[100] = {0};
    snprintf(buf, sizeof(buf), "                                    %s", username);
    lv_label_set_text(chat_title, buf);
    lv_obj_clear_flag(chat_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(chat_title, &ui_font_TextFont32, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    show_current_session_msgs();
}

void clear_chat_panel(void)
{
    if (!ui_Panel10) return;
    lv_obj_clean(ui_Panel10);
}

void add_chat_msg_to_panel(ChatMsg_t *msg)
{
    if (!msg || !ui_Panel10) return;
    
    lv_obj_t *avatar = lv_img_create(ui_Panel10);
    lv_img_set_src(avatar, avatar_img[msg->avatar_img]);
    
    lv_obj_t *name_label = lv_label_create(ui_Panel10);
    lv_label_set_text(name_label, msg->username);
    lv_obj_set_style_text_font(name_label, &ui_font_TextFont16, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    lv_obj_t *content_label = lv_label_create(ui_Panel10);
    msg->msg_content[strcspn(msg->msg_content, "\r\n")] = '\0';
    lv_label_set_text(content_label, msg->msg_content);
    lv_obj_set_style_text_font(content_label, &ui_font_TextFont32, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void send_message_to_server(const char *content, const char *chat_obj)
{
    if (!content || !chat_obj || client_socket < 0) {
        if (client_socket < 0) client_connect_to_aliyun();
        return;
    }
    
    char send_buf[BUF_SIZE] = {0};
    if (strcmp(chat_obj, "群聊") == 0) {
        snprintf(send_buf, sizeof(send_buf), "all:%s", content);
    } else {
        snprintf(send_buf, sizeof(send_buf), "one:%s:%s", chat_obj, content);
    }
    
    if (send(client_socket, send_buf, strlen(send_buf), MSG_NOSIGNAL) <= 0) {
        perror("Send failed");
    }
    
    ChatMsg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.avatar_img = login_avatar_img;
    strncpy(msg.username, "我", sizeof(msg.username) - 1);
    strncpy(msg.msg_content, content, sizeof(msg.msg_content) - 1);
    
    if (strcmp(chat_obj, "群聊") == 0) {
        add_msg_to_group_session(&msg);
        if (strcmp(current_chat_title, "群聊") == 0) {
            add_chat_msg_to_panel(&msg);
        }
    } else {
        add_msg_to_private_session(chat_obj, &msg);
        if (strcmp(current_chat_title, chat_obj) == 0) {
            add_chat_msg_to_panel(&msg);
        }
    }
}

/* ====== 接收线程 ====== */
static void* recv_msg_from_aliyun(void* arg)
{
    char buf[BUF_SIZE];
    while (1) {
        if (client_socket < 0) {
            sleep(1);
            continue;
        }
        
        memset(buf, 0, sizeof(buf));
        int len = recv(client_socket, buf, sizeof(buf) - 1, 0);
        
        if (len == 0) {
            printf("Server disconnected\n");
            close(client_socket);
            client_socket = -1;
            continue;
        }
        if (len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Recv error");
                close(client_socket);
                client_socket = -1;
            }
            usleep(100000);
            continue;
        }
        
        buf[len] = '\0';
        printf("Received: %s\n", buf);
        
        if (strncmp(buf, "当前在线成员:", 19) == 0) {
            parse_online_users(buf);
        } else if (strncmp(buf, "群聊:", 7) == 0) {
            char username[32] = {0};
            char content[BUF_SIZE] = {0};
            sscanf(buf + 7, "%[^:]:%[^\n]", username, content);
            printf("username: %s, content: %s\n", username, content);
            
            ChatMsg_t recv_msg;
            memset(&recv_msg, 0, sizeof(recv_msg));
            strncpy(recv_msg.username, username, sizeof(recv_msg.username) - 1);
            strncpy(recv_msg.msg_content, content, sizeof(recv_msg.msg_content) - 1);
            
            int avatar_idx = 0;
            for (int i = 0; i < MAX_USER_COUNT; i++) {
                if (strcmp(user_status[i].username, username) == 0) {
                    avatar_idx = i;
                    break;
                }
            }
            recv_msg.avatar_img = user_status[avatar_idx].avatar_img;
            
            add_msg_to_group_session(&recv_msg);
            if (strcmp(current_chat_title, "群聊") == 0) {
                add_chat_msg_to_panel(&recv_msg);
            }
        } else if (strncmp(buf, "私聊:", 7) == 0) {
            char username[32] = {0};
            char content[BUF_SIZE] = {0};
            sscanf(buf + 7, "%[^:]:%[^\n]", username, content);
            printf("username: %s, content: %s\n", username, content);
            
            ChatMsg_t recv_msg;
            memset(&recv_msg, 0, sizeof(recv_msg));
            strncpy(recv_msg.username, username, sizeof(recv_msg.username) - 1);
            strncpy(recv_msg.msg_content, content, sizeof(recv_msg.msg_content) - 1);
            
            int avatar_idx = 0;
            for (int i = 0; i < MAX_USER_COUNT; i++) {
                if (strcmp(user_status[i].username, username) == 0) {
                    avatar_idx = i;
                    break;
                }
            }
            recv_msg.avatar_img = user_status[avatar_idx].avatar_img;
            
            add_msg_to_private_session(username, &recv_msg);
            if (strcmp(current_chat_title, username) == 0) {
                add_chat_msg_to_panel(&recv_msg);
            }
        }
    }
    return NULL;
}

void chat_client_init(void)
{
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;
    
    // 初始化用户状态和会话标题 + 分配固定头像索引
    for (int i = 0; i < MAX_USER_COUNT; i++) {
        user_status[i].username[0] = '\0';  // 空字符串表示未占用
        user_status[i].avatar_img = i;  // 固定分配头像索引0-4
        chat_sessions[i].title[0] = '\0';
    }
    
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, recv_msg_from_aliyun, NULL);
    pthread_detach(recv_tid);
    
    client_connect_to_aliyun();
}