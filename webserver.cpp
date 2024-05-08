#include "webserver.h"
WebServer::WebServer()
{
  users = new http_conn[MAX_FD];

  char  server_path[200];
  getcwd(server_path,200);

  char root[6] = "/root";

  m_root = (char *) malloc(strlen(root) + strlen(server_path) + 1);

  strcpy(m_root,server_path);
  strcat(m_root,root);

  users_timer = new client_data[MAX_FD];

}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write(){
  if(m_close_log == 0){
    if(m_log_write == 1){
      Log::get_instance()->init("./ServerLog",m_close_log,2000,20000,800);
    }else{
      Log::get_instance()->init("./ServerLog",m_close_log,2000,20000,0);
    }
  }
}
void WebServer::sql_pool(){
    m_connPool = connection_pool::GetInstance();
  	// void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log); 
    m_connPool->init("localhost",m_user,m_passWord,m_databaseName,3306,m_sql_num,m_close_log);

    users->initmysql_result(m_connPool);
}
void WebServer::thread_pool(){
  m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen(){
  m_listenfd = socket(PF_INET,SOCK_STREAM,0);
  assert(m_listenfd >= 0);
  //是否优雅关闭连接，是否设置延迟时间，时间由tmp的l_linger决定
  if(m_OPT_LINGER == 0){
    struct linger tmp = {0,1};
    setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
  }else if(m_OPT_LINGER == 1){
    struct linger tmp = {1,1};
    setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
  }

  struct sockaddr_in address;
  bzero(&address,sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(m_port);

  int flag = 1;
  setsockopt(m_listenfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));

  int res = 0;
  res = bind(m_listenfd,(struct sockaddr *)&address,sizeof(address));
  assert(res >= 0);

  res = listen(m_listenfd,5);
  assert(res >= 0);

  //创建epoll句柄
  m_epollfd = epoll_create(1);
  assert(m_epollfd != 0);
  utils.addfd(m_epollfd,m_listenfd,false,m_LISTENTrigmode);
  utils.init(TIMESLOT);
  http_conn::m_epollfd = m_epollfd;

  res = socketpair(PF_UNIX,SOCK_STREAM,0,m_pipefd);
  assert(res != -1);
  
  utils.setnonblocking(m_pipefd[1]);
  utils.addfd(m_epollfd,m_pipefd[0],false,0);
  //如果客户端关闭，服务端还向其发送信号，则会收到SIGPIPE的信号，此信号默认行为为关闭进程,故忽使用SIG_IGN略该信号
  utils.addsig(SIGPIPE, SIG_IGN);
  utils.addsig(SIGALRM, utils.sig_handler, false);
  utils.addsig(SIGTERM, utils.sig_handler, false);

  alarm(TIMESLOT);
  utils.u_epollfd = m_epollfd;
  utils.u_pipefd = m_pipefd;
}
void WebServer::timer(int connfd, struct sockaddr_in client_address){

  users[connfd].init(connfd,client_address,m_user,m_passWord,m_databaseName,m_close_log,m_CONNTrigmode,m_root);

  //初始化client_data数据
  //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
  users_timer[connfd].sockfd = connfd;
  users_timer[connfd].address = client_address;

  util_timer * timer = new util_timer();
  timer->user_data = &users_timer[connfd];
  timer->cb_func = cb_func;

  time_t cur = time(NULL);
  timer->expire = cur + 3 * TIMESLOT;
  users_timer[connfd].timer = timer;
  utils.m_timer_lst.add_timer(timer);
}

void WebServer::del_timer(util_timer *timer, int sockfd){
  cb_func(&users_timer[sockfd]);
  if(timer){
    utils.m_timer_lst.del_timer(timer);
  }
  LOG_INFO("close fd %d",users_timer[sockfd].sockfd);
}

void WebServer::adjust_timer(util_timer *timer){
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s","timer adjust once");
}

bool WebServer::dealclientdata(){
  struct sockaddr_in client_address;
  socklen_t clientAddr_len = sizeof(client_address);
  int client_sockfd = 0;
  if(m_LISTENTrigmode == 0){
    client_sockfd = accept(m_listenfd,(struct sockaddr *)&client_address,&clientAddr_len);

    if(client_sockfd < 0){
      LOG_ERROR("accept error is %d",errno);
      return false;
    }

    if(http_conn::m_user_count >= MAX_FD){
      utils.show_error(client_sockfd,"interal server busy");
      LOG_ERROR("%s", "Internal server busy");
      return false;
    }

    timer(client_sockfd,client_address);
    
  }else{
    while(1){
      client_sockfd = accept(m_listenfd,(struct sockaddr *)&client_address,&clientAddr_len);

      if(client_sockfd < 0){
        LOG_ERROR("accept error is %d",errno);
        return false;
      }
      if(http_conn::m_user_count >= MAX_FD){
        utils.show_error(client_sockfd,"interal server busy");
        LOG_ERROR("%s", "Internal server busy");
        return false;
      }
      timer(client_sockfd,client_address);     
    }
  }
  return true;
}
bool WebServer::dealwithsignal(bool& timeout, bool& stop_server){
  char masg[1024];
  int len = recv(m_pipefd[0],masg,sizeof(masg),0);
  if(len <= 0){
    return false;
  }else{
    for(int i = 0;i < len;++i){
      switch(masg[i]){
        case SIGALRM:
          timeout = true;
          break;
        case SIGTERM:
          stop_server = true;
          break;
      }
    }
  }
  return true;
}
void WebServer::dealwithread(int sockfd){
  util_timer * timer = users_timer[sockfd].timer;
  if(m_actormodel == 1){
    if(timer){
      adjust_timer(timer);
    }
    m_pool->append(users + sockfd,0);
    while(1){
      //是否调用函数
      if(users[sockfd].improv == 1){
        //接收是否数据失败
        if(users[sockfd].timer_flag == 1){
          del_timer(timer,sockfd);
          users[sockfd].timer_flag = 0;
        }
        users[sockfd].improv = 0;
        break;
      }
    }
  }else{
    if(users[sockfd].read_once()){
      LOG_INFO("read_deal with the client:%s ,sockfd: %d",inet_ntoa(users[sockfd].get_address()->sin_addr),sockfd);
      //读取成功，异步处理缓冲区数据
      m_pool->append_p(users + sockfd);
      if(timer){
        adjust_timer(timer);
      }
    }else{
      del_timer(timer,sockfd);
    }
  }
}
void WebServer::dealwithwrite(int sockfd){
  util_timer * timer = users_timer[sockfd].timer;
  //reactor
  if(m_actormodel == 1){
    if(timer){
      adjust_timer(timer);
    }

    m_pool->append(users + sockfd,1);

    while(1){
      if(users[sockfd].improv == 1){
        if(users[sockfd].timer_flag == 1){
          del_timer(timer,sockfd);
          users[sockfd].timer_flag = 0;
        }
        users[sockfd].improv = 0;
        break;
      }
    }

  }else{
    if(users[sockfd].write()){
      LOG_INFO("write_deal with the client: %d,sockfd: %d",inet_ntoa(users[sockfd].get_address()->sin_addr),sockfd);
      if(timer){
        adjust_timer(timer);
      }
    }else{
      del_timer(timer,sockfd);
    }
  }
}
void WebServer::eventLoop(){
  bool stop_server = false;
  bool timeout = false;
  while(!stop_server){
    int number = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER, -1);
    //EINTR表示信号中断引起的错误，故继续循环。
    if(number < 0 && errno != EINTR){
      LOG_ERROR("%s","epoll failure");
      break;
    }
    for(int i = 0; i < number;++i){
      int sockfd = events[i].data.fd;
      //处理新连接
      if(sockfd == m_listenfd){
        bool flags = dealclientdata();
        if(!flags){
          continue;
        }
        //服务器端关闭连接，将对应的定时器关闭
      }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP |EPOLLERR)){
        util_timer * timer = users_timer[sockfd].timer;
        del_timer(timer,sockfd);
      }else if(sockfd == m_pipefd[0] && (events[i].events & EPOLLIN)){
        bool flags = dealwithsignal(timeout,stop_server);
        if(!flags){
          LOG_ERROR("%s", "dealwithsignal failure");
        }
      }else if(events[i].events & EPOLLIN){
        dealwithread(sockfd);
      }else if(events[i].events & EPOLLOUT){
        dealwithwrite(sockfd);
      }
    }
    if(timeout){
      utils.timer_handler();
      LOG_INFO("%s","timer tick");
      timeout = false;
    }
  }
}
