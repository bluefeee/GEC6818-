#include "chat.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/socket.h"
#include "arpa/inet.h"
#include "unistd.h"
#include "pthread.h"
#include "sys/types.h"
#include "signal.h"
#include "errno.h"
#include <netinet/tcp.h>

#define DEFAULT_PORT   4321
#define MAX_CLIENTS    10
#define BUF_SIZE       1024
#define MAX_USER_COUNT 5

// 声明函数
void send_message_to_server(const char *content, const char *chat_obj);
int server_init(void);

char chat_obj[20] = "群聊";

/* 客户端信息 */
typedef struct {
    int  fd;
    char username[20];
    int  avatar_img;
    struct sockaddr_in addr;
} ClientInfo_t;

/* 会话池 */
ChatSession_t chat_sessions[MAX_USER_COUNT] = {
    {.title = "用户1", .msg_list = {0}, .msg_count = 0},
    {.title = "用户2", .msg_list = {0}, .msg_count = 0},
    {.title = "用户3", .msg_list = {0}, .msg_count = 0},
    {.title = "用户4", .msg_list = {0}, .msg_count = 0},
    {.title = "用户5", .msg_list = {0}, .msg_count = 0},
};
ChatSession_t group_chat_session = {.title = "群聊", .msg_list = {0}, .msg_count = 0};

/* 全局变量 */
ClientInfo_t clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_fd;
pthread_t recv_thread;
char current_chat_title[20] = "群聊";

/* 头像数组 */
lv_img_dsc_t *avatar_img[5] = {
    &ui_img_funina1_png, &ui_img_keqing1_png,
    &ui_img_linnite1_png, &ui_img_paimen1_png, &ui_img_qiqi1_png
};

/* ====== 工具函数 ====== */
static ClientInfo_t* find_client(int fd)
{
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < client_count; ++i)
        if (clients[i].fd == fd) {
            pthread_mutex_unlock(&client_mutex);
            return &clients[i];
        }
    pthread_mutex_unlock(&client_mutex);
    return NULL;
}

/* 把 sockaddr_in 转成 64bit 整数 key：IP 在上，Port 在下 */
static uint64_t addr_key(const struct sockaddr_in *a)
{
    return ((uint64_t)a->sin_addr.s_addr << 32) | a->sin_port;
}

static ClientInfo_t* find_client_by_name(const char* username)
{
    for (int i = 0; i < client_count; ++i)
        if (strcmp(clients[i].username, username) == 0) {
            pthread_mutex_unlock(&client_mutex);
            return &clients[i];
        }printf("find_client_by_name\n");
    return NULL;
}

static int get_private_session_index(const char* title)
{
    for (int i = 0; i < MAX_USER_COUNT; ++i)
        if (strcmp(title, chat_sessions[i].title) == 0)
            return i;
    return -1;
}

static void add_msg_to_private_session(const char* title, ChatMsg_t* msg)
{   printf("add_msg_to_private_session\n");
    int idx = get_private_session_index(title);
    if (idx == -1) return;
    // 快速加锁/解锁，减少UI线程阻塞概率
    pthread_mutex_lock(&client_mutex);
    if (chat_sessions[idx].msg_count < 20)
        chat_sessions[idx].msg_list[chat_sessions[idx].msg_count++] = *msg;
    pthread_mutex_unlock(&client_mutex);
    printf("add_msg_to_private_session\n");
}

static void add_msg_to_group_session(ChatMsg_t* msg)
{
    // 快速加锁/解锁，减少UI线程阻塞概率
    pthread_mutex_lock(&client_mutex);
    if (group_chat_session.msg_count < 20)
        group_chat_session.msg_list[group_chat_session.msg_count++] = *msg;
    pthread_mutex_unlock(&client_mutex);
}

/* ====== 显示历史消息 ====== */
void show_current_session_msgs(void)
{
    // 快速加锁/解锁，减少UI线程阻塞概率
    pthread_mutex_lock(&client_mutex);
    if (strcmp(current_chat_title, "群聊") == 0) {
        for (int i = 0; i < group_chat_session.msg_count; ++i)
            add_chat_msg_to_panel(&group_chat_session.msg_list[i]);
    } else {
        int idx = get_private_session_index(current_chat_title);
        if (idx != -1)
            for (int i = 0; i < chat_sessions[idx].msg_count; ++i)
                add_chat_msg_to_panel(&chat_sessions[idx].msg_list[i]);
    }
    pthread_mutex_unlock(&client_mutex);
}

/* ====== 收到消息的处理 ====== */
void process_message(int client_fd, const char* buf)
{
    ClientInfo_t* client = find_client(client_fd);
    if (!client) return;

    ChatMsg_t msg;
    memset(&msg, 0, sizeof(ChatMsg_t));
    strncpy(msg.username, client->username, sizeof(msg.username) - 1);
    msg.avatar_img = client->avatar_img;

    char pure_content[BUF_SIZE] = {0};

    /* 群聊 */
    if (strncmp(buf, "群聊:", 3) == 0) {
        // 提取纯净内容，跳过"群聊:"前缀
        strncpy(pure_content, buf + 7, sizeof(pure_content) - 1);
        strncpy(msg.msg_content, pure_content, sizeof(msg.msg_content) - 1);

        // 添加到群聊会话并显示
        add_msg_to_group_session(&msg);
        if (strcmp(current_chat_title, "群聊") == 0)
            add_chat_msg_to_panel(&msg);

        // 转发给其他客户端（快速加锁/解锁）
        pthread_mutex_lock(&client_mutex);
        for (int i = 0; i < client_count; ++i)
            if (clients[i].fd != client_fd) {
                char send_buf[BUF_SIZE] = {0};
                snprintf(send_buf, sizeof(send_buf), "群聊:%s:%s", msg.username, pure_content);
                send(clients[i].fd, send_buf, strlen(send_buf), MSG_DONTWAIT); // 非阻塞发送，避免线程阻塞
            }
        pthread_mutex_unlock(&client_mutex);
    }
    /* 私聊 */
    else if (strncmp(buf, "私聊:", 3) == 0) {
        char target_name[20] = {0};
        char content[BUF_SIZE] = {0};
        if (sscanf(buf + 7, "%[^:]:%s", target_name, content) != 2) {
            printf("私聊格式错误\n");
            return;
        }
        // 提取纯净内容
        strncpy(pure_content, content, sizeof(pure_content) - 1);
        strncpy(msg.msg_content, pure_content, sizeof(msg.msg_content) - 1);

        // 目标是用户0/我：记录到发送方会话
        if (strcmp(target_name, "用户0") == 0 || strcmp(target_name, "我") == 0) {
            add_msg_to_private_session(client->username, &msg);
            if (strcmp(current_chat_title, client->username) == 0)
                add_chat_msg_to_panel(&msg);
        } else {
            // 目标是其他用户：记录到双方会话
            ClientInfo_t* target = find_client_by_name(target_name);
            if (target) {
                add_msg_to_private_session(client->username, &msg);
                add_msg_to_private_session(target_name, &msg);

                // 当前显示对应会话则展示消息
                if (strcmp(current_chat_title, client->username) == 0 ||
                    strcmp(current_chat_title, target_name) == 0)
                    add_chat_msg_to_panel(&msg);

                // 非阻塞发送，避免线程阻塞
                char send_buf[BUF_SIZE] = {0};
                snprintf(send_buf, sizeof(send_buf), "(私聊)%s:%s", msg.username, pure_content);
                send(target->fd, send_buf, strlen(send_buf), MSG_DONTWAIT);
            }
        }
    }
}

/* ====== 收发线程 ====== */
static void* recv_client_msg(void* arg)
{
    struct linger ling = {1, 0};  // l_onoff=1, l_linger=0
    int client_fd = *(int*)arg;
    free(arg);

    char buf[BUF_SIZE];
    while (1) {
        memset(buf, 0, sizeof(buf));
        int len = recv(client_fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);

        if (len == 0) {
            printf("client %d 正常关闭\n", client_fd);
            goto disconnect;
        }
        if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("client %d 异常断开: %s\n", client_fd, strerror(errno));
            goto disconnect;
        }
        if (len < 0) {
            usleep(10000);
            continue;
        }

        process_message(client_fd, buf);
        continue;

disconnect:
        
        setsockopt(client_fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
        close(client_fd);
        /* ======  在这里补回隐藏头像  ====== */
        pthread_mutex_lock(&client_mutex);
        for (int i = 0; i < client_count; ++i) {
            if (clients[i].fd == client_fd) {
                int idx;
                /* 解析“用户%d” */
                if (sscanf(clients[i].username, "用户%d", &idx) == 1 && idx >= 1 && idx <= 5) {
                    /* 照搬你原来的 hide 逻辑 */
                    if (idx == 1) {
                        lv_img_set_src(ui_Image29, &ui_img_funina2_png);
                    } else if (idx == 2) {
                        lv_img_set_src(ui_Image31, &ui_img_keqing2_png);
                    } else if (idx == 3) {
                        lv_img_set_src(ui_Image32, &ui_img_linnite2_png);
                    } else if (idx == 4) {
                        lv_img_set_src(ui_Image33, &ui_img_paimen2_png);
                    } else if (idx == 5) {
                        lv_img_set_src(ui_Image34, &ui_img_qiqi2_png);
                    }
                }

                /* 仅清 fd，保留其余字段供复用 */
                clients[i].fd = -1;
                break;
            }
        }
        pthread_mutex_unlock(&client_mutex);
        close(client_fd);
        break;
    }
    return NULL;
}

static void* server_listen(void* arg)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000);
                continue;
            }
            perror("accept error");
            continue;
        }

        uint64_t key = addr_key(&client_addr);   // 生成唯一键

        pthread_mutex_lock(&client_mutex);

        /* 1. 在已有数组里找 key */
        int idx = -1;
        for (int i = 0; i < client_count; ++i)
            if (addr_key(&clients[i].addr) == key && clients[i].fd == -1) {
                idx = i;          // 发现“老用户”刚断开，复用
                break;
            }

        /* 2. 若没找到且数组未满，新建条目 */
        if (idx == -1 && client_count < MAX_CLIENTS) {
            idx = client_count++;
            clients[idx].addr = client_addr;        // 记录地址
            /* 分配固定用户名与头像（永不重复） */
            snprintf(clients[idx].username, sizeof(clients[idx].username),
                     "用户%d", idx + 1);
            clients[idx].avatar_img = idx;          // 0~4
        }

        /* 3. 数组满了 or 没找到空闲槽 → 直接关 */
        if (idx == -1) {
            pthread_mutex_unlock(&client_mutex);
            close(client_fd);
            continue;
        }

        /* 把新 fd 填进去，其他字段保持原样 */
        clients[idx].fd = client_fd;

        /* 立即显示头像（老用户重连也重新显示） */
        switch (idx + 1) {
        case 1:
            lv_obj_clear_flag(ui_Image29, LV_OBJ_FLAG_HIDDEN);
            lv_img_set_src(ui_Image29, &ui_img_funina1_png);
            lv_obj_clear_flag(ui_Label15, LV_OBJ_FLAG_HIDDEN);
            break;
        case 2:
            lv_obj_clear_flag(ui_Image31, LV_OBJ_FLAG_HIDDEN);
            lv_img_set_src(ui_Image31, &ui_img_keqing1_png);
            lv_obj_clear_flag(ui_Label16, LV_OBJ_FLAG_HIDDEN);
            break;
        case 3:
            lv_obj_clear_flag(ui_Image32, LV_OBJ_FLAG_HIDDEN);
            lv_img_set_src(ui_Image32, &ui_img_linnite1_png);
            lv_obj_clear_flag(ui_Label17, LV_OBJ_FLAG_HIDDEN);
            break;
        case 4:
            lv_obj_clear_flag(ui_Image33, LV_OBJ_FLAG_HIDDEN);
            lv_img_set_src(ui_Image33, &ui_img_paimen1_png);
            lv_obj_clear_flag(ui_Label18, LV_OBJ_FLAG_HIDDEN);
            break;
        case 5:
            lv_obj_clear_flag(ui_Image34, LV_OBJ_FLAG_HIDDEN);
            lv_img_set_src(ui_Image34, &ui_img_qiqi1_png);
            lv_obj_clear_flag(ui_Label19, LV_OBJ_FLAG_HIDDEN);
            break;
        }

        pthread_mutex_unlock(&client_mutex);

        /* 6. 打开 Keep-Alive（8 秒踢掉） */
        int keepAlive = 1;
        int idle = 5, interval = 1, probes = 3;
        setsockopt(client_fd, SOL_SOCKET,  SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));
        setsockopt(client_fd, IPPROTO_TCP,  TCP_KEEPIDLE,  &idle,    sizeof(idle));
        setsockopt(client_fd, IPPROTO_TCP,  TCP_KEEPINTVL, &interval,sizeof(interval));
        setsockopt(client_fd, IPPROTO_TCP,  TCP_KEEPCNT,   &probes,  sizeof(probes));

        /* 7. 启动接收线程 */
        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = client_fd;
        pthread_t tid;
        pthread_create(&tid, NULL, recv_client_msg, fd_ptr);
        pthread_detach(tid);
    }
    return NULL;
}


/* ====== 服务器初始化 ====== */
int server_init(void)
{
    static int initialized = 0;
    if (initialized) return 0;

    // 创建套接字
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket creation error");
        return -1;
    }

    // 端口复用
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt error");
        close(server_fd);
        return -1;
    }

    // 设置套接字为非阻塞模式，避免accept/recv阻塞线程
    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl get flag error");
        close(server_fd);
        return -1;
    }
    if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl set nonblock error");
        close(server_fd);
        return -1;
    }

    // 绑定地址和端口
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("192.168.59.172");
    server_addr.sin_port = htons(DEFAULT_PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0 ||
        listen(server_fd, 5) < 0) {
        perror("bind/listen error");
        close(server_fd);
        return -1;
    }

    // 创建监听线程
    if (pthread_create(&recv_thread, NULL, server_listen, NULL) != 0) {
        perror("pthread_create error");
        close(server_fd);
        return -1;
    }

    printf("Server initialized successfully\n");
    initialized = 1;

    return 0;
}

/* ====== UI 接口 ====== */
void chat_init(char *username)
{
    // 保存当前聊天标题
    strncpy(current_chat_title, username, sizeof(current_chat_title) - 1);
    // 清空面板
    clear_chat_panel();

    // 创建聊天标题标签
    lv_obj_t *chat_title = lv_label_create(ui_Panel10);
    lv_obj_set_width(chat_title, lv_pct(100));
    lv_obj_set_height(chat_title, LV_SIZE_CONTENT);
    lv_obj_set_align(chat_title, LV_ALIGN_CENTER);
    char buf[100] = {0};
    snprintf(buf, sizeof(buf), "                                    %s", username);
    lv_label_set_text(chat_title, buf);
    lv_obj_clear_flag(chat_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(chat_title, &ui_font_TextFont32, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 显示对应历史消息
    show_current_session_msgs();
}

void clear_chat_panel(void)
{
    if (!ui_Panel10) return;
    lv_obj_clean(ui_Panel10);
}

void add_chat_msg_to_panel(ChatMsg_t *msg)
{
    printf("add_chat_msg_to_panel\n");
    if (!msg || !ui_Panel10 || !msg->username || !msg->msg_content) return;

    lv_obj_t *avatar = lv_img_create(ui_Panel10);
    lv_img_set_src(avatar, avatar_img[msg->avatar_img]);

    lv_obj_t *name_label = lv_label_create(ui_Panel10);
    lv_label_set_text(name_label, msg->username);
    lv_obj_set_style_text_font(name_label, &ui_font_TextFont16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *content_label = lv_label_create(ui_Panel10);
    msg->msg_content[strcspn(msg->msg_content, "\r\n")] = '\0';
    lv_label_set_text(content_label, msg->msg_content);
    lv_obj_set_style_text_font(content_label, &ui_font_TextFont32, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(content_label, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(content_label, lv_color_hex(0xC7C7C7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(content_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    printf("add_chat_msg_to_panel\n");
}

/* ====== 发送消息====== */
void send_message_to_server(const char *content, const char *chat_obj)
{
    if (strlen(content) == 0 || !chat_obj) return;

    ChatMsg_t msg;
    memset(&msg, 0, sizeof(ChatMsg_t));
    // 获取本地头像
    FILE *fp = fopen("user.txt", "r");
    if (fp) {
        fscanf(fp, "%*s %*s %d", &msg.avatar_img);
        fclose(fp);
    } else {
        msg.avatar_img = 0;
    }
    strncpy(msg.username, "我", sizeof(msg.username) - 1);
    strncpy(msg.msg_content, content, sizeof(msg.msg_content) - 1); // 仅保存纯净内容
    sprintf(content, "%s\n", content);
    // 群聊消息处理
    if (strcmp(chat_obj, "群聊") == 0) {
        add_msg_to_group_session(&msg);
        add_chat_msg_to_panel(&msg);

        // 转发给所有客户端（快速加锁+非阻塞发送）
        char send_buf[BUF_SIZE] = {0};
        snprintf(send_buf, sizeof(send_buf), "群聊:%s", content);
        pthread_mutex_lock(&client_mutex);
        for (int i = 0; i < client_count; ++i)
            send(clients[i].fd, send_buf, strlen(send_buf), MSG_DONTWAIT); // 非阻塞发送，避免UI卡死
        pthread_mutex_unlock(&client_mutex);
    }
    // 私聊消息处理
    else {printf("私聊\n");
        add_msg_to_private_session(chat_obj, &msg);
        add_chat_msg_to_panel(&msg);

        // 转发给目标客户端（快速加锁+非阻塞发送）
        char send_buf[BUF_SIZE] = {0};
        snprintf(send_buf, sizeof(send_buf), "私聊:%s:%s", chat_obj, content);printf("私聊:%s:%s", chat_obj, content);
        pthread_mutex_lock(&client_mutex);
        ClientInfo_t *target = find_client_by_name(chat_obj);
        if (target)
            send(target->fd, send_buf, strlen(send_buf), MSG_DONTWAIT); // 非阻塞发送，避免UI卡死
        pthread_mutex_unlock(&client_mutex);
    }
}

