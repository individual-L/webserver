#ifndef LST_TIMER
#define LST_TIMER

#include<arpa/inet.h>
#include<sys/socket.h>
#include<sys/wait.h>
#include <time.h>
#include <errno.h>
#include<assert.h>
#include<signal.h>
#include <unistd.h>
#include <stdlib.h>
#include<string.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include <netinet/in.h>
#include"../log/log.h"

class util_timer;

struct client_data{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

class util_timer{
public:
    util_timer():prev(nullptr),next(nullptr){}
    time_t expire;
    util_timer * prev,*next;
    client_data *user_data;
    void (*cb_func)(client_data *);

};

class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    //把列表里到时间的定时器删除并调用回调函数
    void tick();    

private:
    void add_timer(util_timer *timer,util_timer *head);
    util_timer *head;
    util_timer *tail;
};


class Utils
{
public:
    Utils() {}
    ~Utils() {}
    void init(int timeslot);
    //信号处理函数
    static void sig_handler(int sig);
    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart);
    //对文件描述符设置非阻塞
    int setnonblocking(int fd);
    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);
    static int u_epollfd;
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    int m_TIMESLOT;

};
void cb_func(client_data *user_data);
#endif
