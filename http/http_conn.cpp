#include"http_conn.h"
#include<mysql/mysql.h>
#include<fstream>
int http_conn::m_epollfd = -1; 
int http_conn::m_user_count = 0;


//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request is bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";


locker m_mutex;
map<string,string> users;

int setnonblocking(int sockfd){
  int oldopt = fcntl(sockfd,F_GETFL);
  int newopt = oldopt | O_NONBLOCK;
  fcntl(sockfd,F_SETFL,newopt);
  return oldopt;
}
void addfd(int epollfd,int sockfd,bool one_shot,int trigmode){
  epoll_event event;
  event.data.fd = sockfd;
  if(trigmode == 1){
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
  }else{
    event.events = EPOLLIN | EPOLLRDHUP;
  }
  if(one_shot){
    event.events |= EPOLLONESHOT;
  }
  epoll_ctl(epollfd,EPOLL_CTL_ADD,sockfd,&event);
  setnonblocking(sockfd);
}
void removefd(int epollfd,int sockfd){
  epoll_ctl(epollfd,EPOLL_CTL_DEL,sockfd,0);
  close(sockfd);
}
void modfd(int epollfd,int sockfd,int ev,int trigmode){
  epoll_event event;
  event.data.fd = sockfd;

  if(trigmode == 1){
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
  }else{
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
  }
  epoll_ctl(epollfd,EPOLL_CTL_MOD,sockfd,&event);
}




void http_conn::init(const int sockfd,const sockaddr_in &address,const string name,const string passwd,const string sqlname,const int close_log,const int TRIGMode,char * root){
  m_sockfd = sockfd;
  m_address = address;
  m_close_log = close_log;
  m_TRIGMode = TRIGMode;
  
  addfd(m_epollfd,sockfd,true,TRIGMode);
  m_user_count += 1;

  doc_root = root;
  strcpy(sql_name,sqlname.c_str());
  strcpy(sql_passwd,passwd.c_str());
  strcpy(sql_user,name.c_str());

  init();
}

void http_conn::init(){
  
  bytes_have_send = 0;
  bytes_to_send = 0;
  m_check_state = CHECK_STATE_REQUESTLINE;
  m_linger = false;
  m_method = GET;
  m_url = 0;
  m_version = 0;
  m_content_length = 0;
  m_host = 0;
  m_start_line = 0;
  mysql = nullptr;
  m_checked_idx = 0;
  m_read_idx = 0;
  m_write_idx = 0;
  m_state = 0;
  timer_flag = 0;

  cgi = 0;
  improv = 0;

  memset(m_read_buf,'\0',READ_BUFFER_SIZE);
  memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
  memset(m_real_file,'\0',FILENAME_LEN);
}
void http_conn::close_conn(){
  if(m_sockfd != -1){
    removefd(m_epollfd,m_sockfd);
    LOG_INFO("关闭sockfd%d......",m_sockfd);
    close(m_sockfd);
    m_user_count -= 1;
    m_sockfd = -1;
  }
}

bool http_conn::read_once(){
  if(m_read_idx >= READ_BUFFER_SIZE){
    return false;
  }
  ssize_t types_read = 0;
  //LT读取数据
  if(m_TRIGMode == 0){
    types_read = recv(m_sockfd,m_read_buf + m_read_idx,READ_BUFFER_SIZE - m_read_idx,0);

    if(types_read <= 0){
      return false;
    }
    m_read_idx += types_read;
  //ET读取数据
  }else{
    while(true){
      types_read = recv(m_sockfd,m_read_buf + m_read_idx,READ_BUFFER_SIZE - m_read_idx,0);
      //发送缓冲区已满
      if(errno == EAGAIN || errno == EWOULDBLOCK){
        break;
      }
      else if(types_read <= 0){
        return false;
      }
      m_read_idx += types_read;
    }
  }
  return true;
}
http_conn::LINE_STATE http_conn::parse_line(){
  char tmp;
  for(;m_checked_idx < READ_BUFFER_SIZE;++m_checked_idx){
    tmp = m_read_buf[m_checked_idx];

    if(tmp == '\r'){
      if((m_checked_idx + 1) == m_read_idx){
        return LINE_OPEN;
      }
      if(m_read_buf[m_checked_idx + 1] == '\n'){
        m_read_buf[m_checked_idx++] = '\0';
        m_read_buf[m_checked_idx++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }else if(tmp == '\n'){
      if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r'){
        m_read_buf[m_checked_idx - 1] = '\0';
        m_read_buf[m_checked_idx++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }
  }
  return LINE_OPEN;
}
http_conn::HTTP_CODE http_conn::parse_requestLine(char * text){
  //找到text中第一个空格或者制表符，并且返回指向它的指针
  m_url = strpbrk(text," \t");
  if(!m_url){
    return BAD_REQUEST;
  }
  *m_url = '\0';
  m_url += 1;
  char *method = text;
  if(strcasecmp(method,"GET") == 0){
    m_method = GET;
  }else if(strcasecmp(method,"POST") == 0){
    m_method = POST;
    cgi = 1;
  }else{
    return BAD_REQUEST;
  }
  //m_url开头可能还保留空格或者制表符，
  //所有使用strspn函数返回m_url开头不包含" \t"的指针
  m_url += strspn(m_url," \t");
  m_version = strpbrk(m_url," \t");
  if(!m_version){
    return BAD_REQUEST;
  }
  *m_version = '\0';
  m_version += 1;
  m_version += strspn(m_version," \t");
  if(strcasecmp(m_version,"HTTP/1.1")){
    return BAD_REQUEST;
  }
  LOG_INFO("m_url:%s",m_url);
  if (strncasecmp(m_url, "http://", 7) == 0)
  {
      m_url += 7;
      m_url = strchr(m_url, '/');
  }

  if (strncasecmp(m_url, "https://", 8) == 0)
  {
      m_url += 8;
      m_url = strchr(m_url, '/');
  }
  if(!m_url || m_url[0] != '/'){
    return BAD_REQUEST;
  }
  if(strlen(m_url) == 1){
    strcat(m_url,"judge.html");
  }
  m_check_state = CHECK_STATE_HEADER;
  return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_header(char * text){
  if(!text){
    return NO_REQUEST;
  }
  if(text[0] == '\0'){
    if(m_content_length != 0){
      m_check_state = CHECK_STATE_CONTENT;
      return NO_REQUEST;
    }
    return GET_REQUEST;
  }else if(strncasecmp(text,"connection:",11) == 0){
    text += 11;
    text += strspn(text," \t");
    if(strcasecmp(text,"keep-alive") == 0){
      m_linger = true;
    }
  }else if(strncasecmp(text,"content-length:",15) == 0){
    text += 15;
    text += strspn(text," \t");
    m_content_length = atol(text);
  }else if(strncasecmp(text,"host:",5) == 0){
    text += 5;
    text += strspn(text," \t");
    m_host = text;
  }else{
    LOG_INFO("unknown header: %s",text);
  }
  return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_content(char * text){
  if(m_read_idx >= m_content_length + m_check_state){
    text[m_content_length] = '\0';
    m_string = text;
    return GET_REQUEST;
  }
  return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::do_request(){
  strcpy(m_real_file,doc_root);
  int len = strlen(m_real_file);
  const char *p = strrchr(m_url,'/');

  //POST请求
  if(cgi == 1 && (*(p + 1) == 2 || *(p + 1) == 3)){
    char * real_file = (char *)malloc(sizeof(char) * 200);
    strcpy(real_file,"/");
    strcat(real_file,m_url+2);
    strncpy(m_real_file + len,real_file,FILENAME_LEN - len - 1);
    free(real_file);

    LOG_INFO("real_file:%s: ",real_file);
    LOG_INFO("m_real_file:%s: ",m_real_file);

    //过去用户名和密码
    char name[200],passwd[200];
    int i = 0;
    for(i = 5;i < m_string[i] != '&';++i){
      name[i - 5] = m_string[i];
    }
    name[i - 5] = '\0';
    int j = 0;
    for(i += 10;i < m_string[i] != '&';++i,++j){
      passwd[j] = m_string[i];
    }
    passwd[j] = '\0';
    //注册
    if(*(p + 1) == '3'){
      char * sql_insert = (char *)malloc(sizeof(char) * 200);
      strcpy(sql_insert,"INSERT INTO user(username,passwd) VALUE(");
      strcat(sql_insert,"'");
      strcat(sql_insert,name);
      strcat(sql_insert,"','");
      strcat(sql_insert,passwd);
      strcat(sql_insert,"')");
      if(users.find(name) == users.end()){
        m_mutex.lock();
        int res = mysql_query(mysql,sql_insert);
        users.insert(map<string,string>::value_type(name, passwd));
        m_mutex.unlock();

        if(res == 0){
          strcpy(m_url, "/log.html");
        }else{
          strcpy(m_url, "/registerError.html");
        }
      }else{
        strcpy(m_url, "/registerError.html");
      }
    //如果为登录，直接检测
    }else if(*(p + 1) == '2'){
      if(users.find(name) != users.end() && users[name] == passwd){
        strcpy(m_url, "/welcome.html");
      }else{
        strcpy(m_url, "/logError.html");
      }
    }
  }
  //GET请求
  if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }else{
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

  if (stat(m_real_file, &m_file_stat) < 0)
    return NO_RESOURCE;
  //判断其他人是否可以获取读取权限，R:read;OTH:other
  if (!(m_file_stat.st_mode & S_IROTH))
    return FORBIDDED_REQUEST;
  //判断是否为目录
  if (S_ISDIR(m_file_stat.st_mode))
    return BAD_REQUEST;
  int fd = open(m_real_file,O_RDONLY);

  //映射文件地址信息
  m_file_address = (char*) mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
  close(fd);
  return FILE_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read(){
  HTTP_CODE res = NO_REQUEST;
  LINE_STATE line_state = LINE_OK;
  char * text = 0;
  while((m_check_state == CHECK_STATE_CONTENT && line_state == LINE_OK) ||
  (line_state = parse_line()) == LINE_OK){
    text = get_line();
    m_start_line = m_checked_idx;
    LOG_INFO("解析：%s",text);
    switch(m_check_state){
      case CHECK_STATE_REQUESTLINE:
        res = parse_requestLine(text);
        if(res == BAD_REQUEST){
          return BAD_REQUEST;
        }
        break;
      case CHECK_STATE_HEADER:
        res = parse_header(text);
        if(res == BAD_REQUEST){
          return BAD_REQUEST;
        }else if(res == GET_REQUEST){
          return do_request();
        }
        break;
      case CHECK_STATE_CONTENT:
        res = parse_content(text);
        if(res == GET_REQUEST){
          return do_request();
        }
        //为了可以执行parse_line()函数
        line_state = LINE_OPEN;
        break;
      default:
        return INTERNAL_ERROR;
    }
  }
  return NO_REQUEST;
}
void http_conn::unmap(){
  if(m_file_address){
    munmap(m_file_address,m_file_stat.st_size);
    m_file_address = 0;
  }
}

bool http_conn::write_buffer(const char * format,...){
  if(m_write_idx >= WRITE_BUFFER_SIZE){
    return false;
  }
  va_list alist;
  va_start(alist,format);
  int len = vsnprintf(m_write_buf + m_write_idx,WRITE_BUFFER_SIZE - 1 - m_write_idx,format,alist);
  if(len >= WRITE_BUFFER_SIZE - 1 -m_write_idx){
    va_end(alist);
    return false;
  }
  m_write_idx += len;

  LOG_INFO("m_write_buf:%s", m_write_buf);

  va_end(alist);
  return true;
}

bool http_conn::add_status_line(int status,const char * title){
  return write_buffer("%s %d %s\r\n","HTTP/1.1",status,title);
}

bool http_conn::add_linger(){
  return write_buffer("Connection:%s\r\n",(m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line(){
  return write_buffer("%s","\r\n");
}

bool http_conn::add_content_length(int len){
  return write_buffer("Content-length:%d\r\n",len);
}

bool http_conn::add_content(const char * s){
  return write_buffer("%s",s);
}


bool http_conn::add_header(int len){
  return add_content_length(len) && add_linger() && add_blank_line();
}

bool http_conn::process_write(HTTP_CODE res){
  switch(res){
    case BAD_REQUEST:
      add_status_line(404,error_404_title);
      add_header(strlen(error_404_title));
      if(!add_content(error_404_form)){
        return false;
      }
      break;
    case INTERNAL_ERROR:
      add_status_line(500,error_500_title);
      add_header(strlen(error_500_title));
      if(!add_content(error_500_title)){
        return false;
      }
      break;    
    case FILE_REQUEST:
      add_status_line(403,ok_200_title);
      if(m_file_stat.st_size != 0){
        add_header(m_file_stat.st_size);
        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        m_iv[1].iov_base = m_file_address;
        m_iv[1].iov_len = m_file_stat.st_size;
        m_iv_count = 2;
        bytes_to_send = m_write_idx + m_file_stat.st_size;
        return true;
      }else{
        const char * str = "<html><body></body></html>";
        if(!add_content(str)){
          return false;
        }
      }
      break;
    case FORBIDDED_REQUEST:
      add_status_line(403,error_403_title);
      add_header(strlen(error_403_title));
      if(!add_content(error_403_form)){
        return false;
      }
      break;
    default:
      return false;
  }
  m_iv[0].iov_base = m_write_buf;
  m_iv[0].iov_len = m_write_idx;
  m_iv_count = 1;
  bytes_to_send = m_write_idx;
  return true;
}

void http_conn::process(){
  HTTP_CODE res = process_read();
  if(res == NO_REQUEST){
    modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
    return;
  }
  bool write_ret = process_write(res);
  if (!write_ret)
  {
      close_conn();
  }
  modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); 
}

bool http_conn::write()
{

    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    int temp = 0;

    while (1)
    {
        temp = writev(m_sockfd,m_iv,m_iv_count);
        if(temp < 0){

          if(errno == EAGAIN){
            modfd(m_epollfd,m_sockfd,EPOLLOUT,m_TRIGMode);
            return true;
          }
          unmap();
          return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if(bytes_have_send >= m_iv[0].iov_len){
          m_iv[0].iov_len = 0;
          m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
          m_iv[1].iov_len = bytes_to_send;
        }else{
          m_iv[0].iov_base = m_write_buf + bytes_have_send;
          m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if(bytes_to_send <= 0){
          unmap();
          modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);

          if(m_linger){
            init();
            return true;
          }else{
            return false;
          }
        }
    }
}
void http_conn::initmysql_result(connection_pool *connPool){
  MYSQL * mysql = nullptr;
  connectionRAII(&mysql,connPool);
  //在user表中检索username，passwd数据
  if(mysql_query(mysql,"SELECT username,passwd FROM user")){
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
  }
  //从表中检索完整的结果集
  MYSQL_RES *res = mysql_store_result(mysql);
  //从结果集中获取下一行，将对应的用户名和密码，存入map中
  while(MYSQL_ROW row = mysql_fetch_row(res)){
    string str1(row[0]);
    string str2(row[1]);
    users[str1] = str2;
  }

}
