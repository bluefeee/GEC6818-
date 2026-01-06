#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#define DEFAULT_PORT   4321
#define MAX_CLIENTS    10
#define BUF_SIZE       1024
#define MAX_USER_COUNT 5
#define LOGIN_TIMEOUT_SEC 30  // 仅用于登录阶段

typedef struct {
    int sockfd;
    char username[32];
    int is_online;
} ClientInfo;

ClientInfo client_list[MAX_USER_COUNT];
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

void init_client_list() {
    for (int i = 0; i < MAX_USER_COUNT; i++) {
        client_list[i].sockfd = -1;
        client_list[i].is_online = 0;
        memset(client_list[i].username, 0, sizeof(client_list[i].username));
    }
}

// 内部版本，调用者已持有锁
void get_online_users_str_locked(char *buf) {
    buf[0] = '\0';
    int first = 1;
    for (int i = 0; i < MAX_USER_COUNT; i++) {
        if (client_list[i].is_online) {
            if (!first) strcat(buf, " ");
            strcat(buf, client_list[i].username);
            first = 0;
        }
    }
    if (buf[0] == '\0') strcat(buf, "无在线用户");
}

// 对外接口，自动加锁
void get_online_users_str(char *buf) {
    pthread_mutex_lock(&client_mutex);
    get_online_users_str_locked(buf);
    pthread_mutex_unlock(&client_mutex);
}

void broadcast_message(int exclude_fd, const char *msg) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_USER_COUNT; i++) {
        if (client_list[i].is_online && client_list[i].sockfd != exclude_fd) {
            send(client_list[i].sockfd, msg, strlen(msg), MSG_DONTWAIT);
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

int find_client_by_username(const char *username) {
    pthread_mutex_lock(&client_mutex);
    int fd = -1;
    for (int i = 0; i < MAX_USER_COUNT; i++) {
        if (client_list[i].is_online && strcmp(client_list[i].username, username) == 0) {
            fd = client_list[i].sockfd;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
    return fd;
}

int find_client_index_by_fd_locked(int sockfd) {
    for (int i = 0; i < MAX_USER_COUNT; i++) {
        if (client_list[i].sockfd == sockfd) {
            return i;
        }
    }
    return -1;
}

int add_client(int sockfd, const char *username) {
    pthread_mutex_lock(&client_mutex);
    int online_count = 0;
    for (int i = 0; i < MAX_USER_COUNT; i++) {
        if (client_list[i].is_online) online_count++;
    }
    if (online_count >= MAX_USER_COUNT) {
        pthread_mutex_unlock(&client_mutex);
        return -1;
    }

    int ret = -1;
    for (int i = 0; i < MAX_USER_COUNT; i++) {
        if (client_list[i].sockfd == -1) {
            client_list[i].sockfd = sockfd;
            client_list[i].is_online = 1;
            strncpy(client_list[i].username, username, sizeof(client_list[i].username)-1);
            ret = 0;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
    return ret;
}

void remove_client(int sockfd) {
    char username[32] = {0};
    char online_users_buf[256] = {0};

    pthread_mutex_lock(&client_mutex);
    int index = find_client_index_by_fd_locked(sockfd);
    if (index != -1) {
        strcpy(username, client_list[index].username);
        client_list[index].sockfd = -1;
        client_list[index].is_online = 0;
        memset(client_list[index].username, 0, sizeof(client_list[index].username));
        get_online_users_str_locked(online_users_buf);
    }
    pthread_mutex_unlock(&client_mutex);

    if (username[0] != '\0') {
        close(sockfd);
        char broadcast_buf[BUF_SIZE];
        snprintf(broadcast_buf, sizeof(broadcast_buf), "当前在线成员: %s\n", online_users_buf);
        broadcast_message(-1, broadcast_buf);
        printf("客户端 %s 断开连接\n", username);
        printf("%s", broadcast_buf);
    }
}

// 设置socket超时（仅用于登录阶段）
int set_socket_timeout(int sockfd, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("设置接收超时失败");
        return -1;
    }
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        perror("设置发送超时失败");
        return -1;
    }
    
    return 0;
}

// 取消超时（永久等待）
void clear_socket_timeout(int sockfd) {
    struct timeval tv = {0, 0};  // 0秒 = 无超时
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/* ====== client_handler函数（服务端） ====== */
void *client_handler(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    
    char buf[BUF_SIZE];
    char username[32] = {0};

    // 1. 设置登录超时（防止客户端不发送用户名）
    struct timeval tv = {LOGIN_TIMEOUT_SEC, 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 2. 直接接收用户名（不再发送提示）
    int recv_len = recv(client_fd, username, sizeof(username)-1, 0);
    if (recv_len <= 0) {
        printf("客户端断开连接（未收到用户名）\n");
        close(client_fd);
        return NULL;
    }
    username[strcspn(username, "\n\r")] = '\0';  // 移除换行符

    // 3. 检查并添加客户端
    if (add_client(client_fd, username) == -1) {
        char full_msg[] = "系统提示：当前在线用户已达上限，无法连接！\n";
        send(client_fd, full_msg, strlen(full_msg), MSG_NOSIGNAL);
        close(client_fd);
        return NULL;
    }

    printf("用户 %s 登录成功\n", username);

    // 4. 取消超时，进入正常通信
    tv.tv_sec = 0;  // 取消超时
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 5. 广播在线用户列表
    char broadcast_buf[BUF_SIZE];
    char online_users_buf[256];
    get_online_users_str(online_users_buf);
    snprintf(broadcast_buf, sizeof(broadcast_buf), "当前在线成员: %s\n", online_users_buf);
    broadcast_message(client_fd, broadcast_buf);
    send(client_fd, broadcast_buf, strlen(broadcast_buf), MSG_NOSIGNAL);

    // 6. 消息处理循环（保持不变）
    while (1) {
        memset(buf, 0, sizeof(buf));
        recv_len = recv(client_fd, buf, sizeof(buf)-1, 0);
        
        if (recv_len == 0) {  // 客户端正常断开
            printf("客户端 %s 正常断开\n", username);
            break;
        } else if (recv_len < 0) {  // 错误
            if (errno == EINTR) continue;  // 被信号中断
            perror("接收数据错误");
            break;
        }
        
        buf[strcspn(buf, "\n\r")] = '\0';
        if (strlen(buf) == 0) continue;  // 空消息跳过

        // 群聊处理
        if (strncmp(buf, "all:", 4) == 0) {
            char group_msg[BUF_SIZE];
            snprintf(group_msg, sizeof(group_msg), "群聊:%s:%s\n", username, buf+4);
            broadcast_message(client_fd, group_msg);
            printf("%s", group_msg);
        }
        // 私聊处理
        else if (strncmp(buf, "one:", 4) == 0) {
            char *target_start = buf + 4;
            char *msg_start = strchr(target_start, ':');
            if (msg_start) {
                *msg_start = '\0';
                char *target = target_start;
                msg_start++;
                int target_fd = find_client_by_username(target);
                if (target_fd != -1) {
                    char private_msg[BUF_SIZE];
                    snprintf(private_msg, sizeof(private_msg), "私聊:%s:%s\n", username, msg_start);
                    send(target_fd, private_msg, strlen(private_msg), MSG_NOSIGNAL);
                    printf("私聊：%s -> %s：%s\n", username, target, msg_start);
                } else {
                    char err[] = "系统提示：目标用户不存在或离线！\n";
                    send(client_fd, err, strlen(err), MSG_NOSIGNAL);
                }
            } else {
                char err[] = "系统提示：私聊格式错误！正确格式：one:目标用户名:消息内容\n";
                send(client_fd, err, strlen(err), MSG_NOSIGNAL);
            }
        } else {
            char err[] = "系统提示：消息格式错误！\n群聊格式：all:消息内容\n私聊格式：one:目标用户名:消息内容\n";
            send(client_fd, err, strlen(err), MSG_NOSIGNAL);
        }
    }

    remove_client(client_fd);
    return NULL;
}


int main() {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    init_client_list();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("创建套接字失败");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(DEFAULT_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("绑定失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) == -1) {
        perror("监听失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("服务器已启动，监听端口 %d...\n", DEFAULT_PORT);

    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            perror("接受连接失败");
            continue;
        }

        printf("客户端 [%s:%d] 连接成功\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        pthread_t tid;
        int *fd_ptr = malloc(sizeof(int));
        if (fd_ptr == NULL) {
            perror("内存分配失败");
            close(client_fd);
            continue;
        }
        *fd_ptr = client_fd;
        
        if (pthread_create(&tid, NULL, client_handler, fd_ptr) != 0) {
            perror("创建线程失败");
            close(client_fd);
            free(fd_ptr);
        } else {
            pthread_detach(tid);
        }
    }

    close(server_fd);
    pthread_mutex_destroy(&client_mutex);
    return 0;
}