#include "user.h"
#include "ui.h"
user_p head;

char login_user[32] = {0};

/* 把当前下拉框序号读出来 */
static uint8_t get_cur_profile_idx(void)
{
    return lv_dropdown_get_selected(ui_Dropdown1);   /* 0-4 */
}

/* 把序号→指针（仅用于显示，不落盘） */
static lv_img_dsc_t *index_to_img(uint8_t idx)
{
    static lv_img_dsc_t *const map[] = {
        &ui_img_funina_png, &ui_img_keqing_png, &ui_img_linnite_png,
        &ui_img_paimen_png, &ui_img_qiqi_png
    };
    return map[idx % 5];
}

static void save_user_profile(uint8_t idx)
{
    if (!head) return;
    /* 找到当前登录用户（最后一条即是） */
    user_p u = list_entry(head->list.prev, user_t, list);
    u->profile_index = idx;
    Write2File("user.txt", head);        /* 立即落盘 */
}


// 通用定时器回调：仅隐藏标签
static void hide_label_cb(lv_timer_t *t) {
    lv_obj_add_flag(ui_Label13, LV_OBJ_FLAG_HIDDEN);
    lv_timer_del(t);
}

// 显示临时提示（1.5秒后自动隐藏）
static void show_message(const char *text) {
    lv_label_set_text(ui_Label13, text);
    lv_obj_clear_flag(ui_Label13, LV_OBJ_FLAG_HIDDEN);
    
    lv_timer_t *timer = lv_timer_create(hide_label_cb, 1500, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

// 登录成功专用回调：隐藏标签+页面跳转
static void login_success_cb(lv_timer_t *t) {
    lv_obj_add_flag(ui_Label13, LV_OBJ_FLAG_HIDDEN);
    _ui_screen_change(&ui_Screen2, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, &ui_Screen2_screen_init);
    lv_timer_del(t);
}

// 登录成功处理：显示提示+延迟跳转
static void show_login_success(void) {
    lv_label_set_text(ui_Label13, "\n      登录成功      \n");
    lv_obj_clear_flag(ui_Label13, LV_OBJ_FLAG_HIDDEN);
    
    lv_timer_t *timer = lv_timer_create(login_success_cb, 500, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

user_p UserInit() {
    if(head) return head;
    head = (user_p)malloc(sizeof(user_t));
    head->list.next = &head->list;
    head->list.prev = &head->list;
    head->profile_index = 0;  
    return head;
}

void UserCleanup() {
    if (!head) return;
    
    list_p current = head->list.next;
    while (current != &head->list) {
        list_p next = current->next;
        user_p user = list_entry(current, user_t, list);
        free(user);
        current = next;
    }
    free(head);
    head = NULL;
}

user_p UserCreat(char *username, char *password) {
    user_p node = (user_p)malloc(sizeof(user_t));
    strcpy(node->username, username);
    strcpy(node->password, password);
    printf("username:%s\tpassword:%s\n", node->username, node->password);
    node->profile_index = 0;
    node->list.next = &node->list;
    node->list.prev = &node->list;   
    return node;
}

void ListAdd(list_p new, list_p pre) {
    new->next = pre->next;
    pre->next->prev = new;
    pre->next = new;
    new->prev = pre;
}

void registerUser() {
    lv_obj_add_flag(ui_Label13, LV_OBJ_FLAG_HIDDEN);
    
    char username[20];
    char password[20];
    
    if (strlen(lv_textarea_get_text(ui_UserInputTextArea))==0 ||
        strlen(lv_textarea_get_text(ui_PasswordInputTextArea))==0) {
        show_message("\n    账号密码不允许为空    \n");
        printf("输入为空\n");
        return;
    }

    strcpy(username, lv_textarea_get_text(ui_UserInputTextArea));
    strcpy(password, lv_textarea_get_text(ui_PasswordInputTextArea));
    printf("username:%s\npassword:%s\n", username, password);
    
    user_p ptr;
    list_p tmp = head->list.next;
    while (tmp != &head->list) {
        ptr = list_entry(tmp, user_t, list);
        if (!strcmp(ptr->username, username)) {
            printf("用户名已存在，请重新输入\n");
            show_message("\n    用户名已存在    \n");
            return;
        }
        tmp = tmp->next;
    }
    
    user_p node = UserCreat(username, password);
    ListAdd(&node->list, &head->list);
    Write2File("user.txt", head);
    show_message("\n      注册成功      \n");
}

int loginUser() {
    lv_obj_add_flag(ui_Label13, LV_OBJ_FLAG_HIDDEN);
    
    char username[20];
    char password[20];
    
    strcpy(username, lv_textarea_get_text(ui_UserInputTextArea));
    strcpy(password, lv_textarea_get_text(ui_PasswordInputTextArea));
    printf("usr:%s\tpwd:%s\n", username, password);
    
    user_p ptr;
    list_p tmp = head->list.next;
    while (tmp != &head->list) {
        ptr = list_entry(tmp, user_t, list);
        if (!strcmp(ptr->username, username) && !strcmp(ptr->password, password)) {
            printf("登录成功\n");
            strcpy(login_user, username);
            show_login_success();
            save_user_profile(get_cur_profile_idx());
            return 1;
        } else {
            tmp = tmp->next;
        }   
    }
    
    printf("请检查用户名/密码是否正确\n");
    show_message("\n      登陆失败      \n");
    return 0;
}

void Write2File(const char* filename, user_p head)
{
    if (!head) return;                 /* ① 判空 */

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("fopen failed");
        return;
    }

    /* ② 从第一个真实用户开始写（跳过哨兵） */
    list_p tmp = head->list.next;
    while (tmp != &head->list) {       /* 哨兵自己指向自己，循环终止正确 */
        user_p node = list_entry(tmp, user_t, list);
        fprintf(fp, "%s %s %hhu\n", node->username, node->password, node->profile_index);
        tmp = tmp->next;
    }
    fclose(fp);
}

void ReadFromFile(const char* filename, user_p head) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("用户文件不存在，创建新文件\n");
        return;
    }
    
    char username[64], password[64];
    int  idx;
    while (fscanf(fp, "%63s %63s %hhu", username, password, &idx) == 3) {
        user_p node = UserCreat(username, password);
        node->profile_index = idx;           /* 文件里存的序号 */
        ListAdd(&node->list, &head->list);
    }
    fclose(fp);
}
