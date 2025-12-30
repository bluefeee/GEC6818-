#include "http.h"


//设计http请求头部字段数据结点
typedef struct HNode{
    char name[256]; //头部字段的属性名称
    char value[256];//头部字段的属性值

    struct HNode *next;
}HNode_t;


struct HttpInfo{
    char url[1024]; //请求的url
    HNode_t *head;  //请求头部字段链表头节点
    int fd;//socket套接字文件描述符
    char *jsonData; //应答的数据
}data;


/*
 * 函数作用：从url中 切割出 域名
 * 返回值：返回域名
 * 案例：比如从http(s)://qryweather.market.alicloudapi.com/lundroid/queryweather 中 获取 qryweather.market.alicloudapi.com
*/
static char* http_getHostName()
{
    //从 // 开始
    static char tempName[1024];

    memset(tempName,0,sizeof(tempName));
    memcpy(tempName,data.url,strlen(data.url));
    char *name = strstr(tempName,"//");
    return strtok(name+2,"/");
}
/*
 * 函数作用：根据传递进来的url找到服务器IP地址，并且创建套接字进行连接
 * 函数参数：传入参数，url
 * 返回值：成功连接服务器返回true  否则返回false
*/
bool http_init(const char*url)
{
    memset(data.url,0,sizeof(data.url));


    //获取传递进来的url
    memcpy(data.url,url,strlen(url));

    //进行解析,获取域名
    char* dnsName = http_getHostName();
    if(strlen(dnsName) == 0)
    {
        printf("传递的url格式不正确\n");
        return false;
    }
    //利用DNS服务查询指定域名的IP
    struct hostent *he = gethostbyname(dnsName);
    if(he == NULL)
    {
        perror("DNS查询失败");
        return false;
    }
    printf("IP: %s\n", inet_ntoa(*(struct in_addr*)((he->h_addr_list)[0])));


    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    bzero(&addr, len);

    addr.sin_family = AF_INET;
    addr.sin_addr   = *(struct in_addr*)((he->h_addr_list)[0]);
    addr.sin_port   = htons(80);

    // 创建TCP套接字(因为HTTP是基于TCP的)，并发起连接
    data.fd = socket(AF_INET, SOCK_STREAM, 0);
    if(connect(data.fd, (struct sockaddr *)&addr, len) == 0)
    {
        printf("连接服务器成功！\n");
    }else{
        perror("连接服务器失败\n");
        return false;
    }

    //初始化存储头部字段的链表
    data.head = (HNode_t*)calloc(1,sizeof(HNode_t));
    data.head->next = NULL;
}

void http_setRawHeader(char*name,char*value)
{
    //新建结点并且初始化
    HNode_t *newNode = (HNode_t*)calloc(1,sizeof(HNode_t));

    memcpy(newNode->name,name,strlen(name));
    memcpy(newNode->value,value,strlen(value));

    newNode->next = NULL;

    //头插链表
    newNode->next = data.head->next;
    data.head->next = newNode;
}

void http_get()
{
    //组合 请求报文
    char request[1024*2]={0}; //
    int total = 0;

    //请求行
    char *method = "GET";
    char *version = "HTTP/1.1";
    sprintf(request,"%s %s %s\r\n",method,data.url,version);
    total = strlen(request);

    //请求头部字段
    HNode_t *p = data.head->next;
    while(p)
    {
        sprintf(request+total,"%s: %s\r\n",p->name,p->value);
        total =strlen(request);
        p = p->next;
    }
    //请求包体
    sprintf(request+total,"\r\n");

    //发送数据
    int ret = write(data.fd,request,strlen(request));
    if(ret<=0)
    {
        perror("write error");
    }

    printf("%s\n",request);
}
void http_post(char*jsonData)
{
    //组合 请求报文
    char *request = calloc(1,1024*2+strlen(jsonData));
    int total = 0;

    //请求行
    char *method = "GET";
    char *version = "HTTP/1.1";
    sprintf(request,"%s %s %s\r\n",method,data.url,version);
    total = strlen(request);

    //请求头部字段
    HNode_t *p = data.head->next;
    while(p)
    {
        sprintf(request+total,"%s: %s\r\n",p->name,p->value);
        total =strlen(request);
        p = p->next;
    }
    //请求包体
    sprintf(request+total,"\r\n%s\r\n",jsonData);

    //发送数据
    int ret = write(data.fd,request,strlen(request));
    if(ret<=0)
    {
        perror("write error");
    }

    printf("%s\n",request);

    free(request);
}
// 分析响应报文头部
void http_parseHeader(char *res, int *ok, int *len)
{
    char *retcode = res + strlen("HTTP/1.x ");

    switch(atoi(retcode))
    {
    case 200 ... 299:
            *ok = 1;
            printf("查询成功\n");
            break;

    case 400 ... 499:
            *ok = 0;
            printf("客户端错误\n");
            exit(0);
            break;

    case 500 ... 599:
            *ok = 0;
            printf("服务端错误\n");
            exit(0);
            break;
    }

    // 若响应首部返回Content-Length，则len直接获取整个一次性响应报文的长度
    // 若响应首部返回Transfer-Encoding: chunked，则len设置为-1，按块接收响应报文
    char *p;
    if(p = strstr(res, "Content-Length: "))
        *len = atoi(p + strlen("Content-Length: "));
    else
        *len = -2;//应答头部 长度字段不存在

    if(p = strstr(res, "Transfer-Encoding: chunked"))
        *len = -1;

}

bool http_reply(char **p)
{
    /* --------------- 1. 先收完响应头 --------------- */
    char res[1024] = {0};
    int  total = 0;
    while(1)
    {
        int n = read(data.fd, res + total, 1);
        if(n <= 0){ perror("read header"); return false; }
        total += n;
        if(strstr(res, "\r\n\r\n")) break;          /* 头结束 */
    }

    /* --------------- 2. 解析头 --------------- */
    int ok, jsonlen = 0;
    http_parseHeader(res, &ok, &jsonlen);
    if(!ok) return false;

    /* --------------- 3. 收正文 --------------- */
    if(jsonlen == -1)                           /* Transfer-Encoding: chunked */
    {
        data.jsonData = calloc(1, 1024*1024);   /* 1MB 足够 */
        char *wp = data.jsonData;               /* 写指针 */
        char  line[64];

        while(1)
        {
            /* 读一行 16 进制长度 */
            int l = 0;
            do{
                int n = read(data.fd, line + l, 1);
                if(n <= 0) goto chunked_done;
            }while(line[l++] != '\n');
            line[l-2] = 0;                      /* 去 \r\n */
            int chunkLen = (int)strtol(line, NULL, 16);
            if(chunkLen == 0) break;            /* 最后一块 */

            /* 读 chunkLen 字节真实数据 */
            int need = chunkLen;
            while(need > 0){
                int n = read(data.fd, wp, need);
                if(n <= 0) goto chunked_done;
                wp  += n;
                need -= n;
            }
            read(data.fd, line, 2);             /* 跳过块尾 \r\n */
        }
        /* 跳过尾部的 0\r\n （以及可选 trailer） */
        do{
            int n = read(data.fd, line, 1);
            if(n <= 0) break;
        }while(line[0] != '\n');

chunked_done:
        *p = data.jsonData;                     /* 返回纯 JSON */
    }
    else                                        /* Content-Length 模式 */
    {
        data.jsonData = calloc(1, jsonlen + 1);
        total = 0;
        while(jsonlen > 0){
            int n = read(data.fd, data.jsonData + total, jsonlen);
            total += n;
            jsonlen -= n;
        }
        *p = data.jsonData;
    }
    return true;
}

void http_destory()
{
    close(data.fd);
    free(data.jsonData);

    //销毁链表
    HNode_t *p = data.head->next;
    HNode_t *pNext;
    while(p)
    {
        pNext = p->next;
        free(p);

        p = pNext;
    }

    free(data.head);
}
