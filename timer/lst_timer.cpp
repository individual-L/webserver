// #include "../http/http_conn.h"
#include"lst_timer.h"

sort_timer_lst::sort_timer_lst(){
    head = nullptr;
    tail = nullptr;
}
sort_timer_lst::~sort_timer_lst(){
    util_timer * tmp = head;
    while(head){
        tmp = head->next;
        delete head;
        head = tmp;
    }
}

void sort_timer_lst::add_timer(util_timer *timer){
    if(!timer){
        return;
    }
    if(!head){
        head = timer;
        tail = timer;
        return;
    }
    if(timer->expire < head->expire){
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}
void sort_timer_lst::add_timer(util_timer *timer,util_timer *head){
    util_timer * pre = head;
    util_timer * cur = head->next;
    while(cur){
        if(timer->expire < cur->expire){
            pre->next = timer;
            timer->next = cur;
            timer->prev = pre;
            cur->prev = timer;
            break;
        }
        pre = cur;
        cur = cur->next;
    }
    if(cur == nullptr){
        timer->next = nullptr;
        pre->next = timer;
        timer->prev = pre;
        tail = timer;
    }
    return;
}

void sort_timer_lst::adjust_timer(util_timer *timer){
    if (!timer)
    {
        return;
    }
    util_timer *tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }else{
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}
void sort_timer_lst::tick(){
    if(head == nullptr){
        return;
    }
    time_t cur = time(NULL);
    util_timer * tmp = head;
    while(tmp){
        if(cur < tmp->expire){
            break;
        }
        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        if (head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
} 
void Utils::init(int timeslot){
    m_TIMESLOT = timeslot;
}
int Utils::setnonblocking(int fd){
    int oldopt = fcntl(fd,F_GETFL);
    int newopt = O_NONBLOCK | oldopt;
    fcntl(fd,F_SETFL,newopt);
    return oldopt;
} 
//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode){
    epoll_event event;
    event.data.fd = fd;
    if(TRIGMode == 1){
        event.events = EPOLLIN | EPOLLET |EPOLLRDHUP;
    }else{
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if (one_shot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}

void Utils::addsig(int sig, void(handler)(int), bool restart){
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler = handler;
    //重新调用被中断的系统调用
    if (restart){
        sa.sa_flags |= SA_RESTART;
    }
    //初始化信号集
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL) != -1);
}
void Utils::sig_handler(int sig)
{
    //保留原来的errno,为了不对main函数的errno产生影响
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}
void Utils::timer_handler(){
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info){
    send(connfd,info,strlen(info),0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data){
    epoll_ctl(Utils::u_epollfd,EPOLL_CTL_DEL,user_data->sockfd,0);
    assert(user_data);
    close(user_data->sockfd);
    // http_conn::m_user_count--;

}
