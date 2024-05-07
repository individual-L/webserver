#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include<map>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<sys/socket.h>
#include<fcntl.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<errno.h>
#include<sys/stat.h>
#include <sys/mman.h>
#include<assert.h>
#include <sys/uio.h>


#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"
using namespace std;

class http_conn{
public:
  static const int FILENAME_LEN = 200;
  static const int READ_BUFFER_SIZE = 2048;
  static const int WRITE_BUFFER_SIZE = 1024;
  enum METHOD{
    GET = 0,
    POST,
    HEAD,
    PUT,
    DELETE
  };
  enum HTTP_CODE{
    NO_REQUEST = 0,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDED_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSE_CONNECTION
  };
  enum CHECK_STATE{
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
  };
  enum LINE_STATE{
    LINE_OK = 0,
    LINE_BAD,
    LINE_OPEN
  };

public:
  http_conn(){}
  ~http_conn(){}
  //初始化连接，外部调用初始化套接字地址
  void init(const int m_sockfd,const sockaddr_in &m_address,const string name,const string passwd,const string sql_name,const int m_close_log,const int TRIGMode,char * doc_root);
  sockaddr_in* get_address(){
    return &m_address;
  }
  //关闭连接，关闭一个连接，客户总量减一
  void close_conn();
  //读取客户端数据，直到无数据可读
  //ET模式下，需要一次性读完数据
  bool read_once();
  //处理读缓冲区和写缓冲区的数据
  void process();

  bool write();

  void initmysql_result(connection_pool *connPool);

public:

  static int m_epollfd; 
  static int m_user_count;
  MYSQL *mysql;
  int m_state;  //读为0, 写为1
  int timer_flag;
  int improv;
  
private:
  //初始化连接成员变量  
  void init();
  //处理读缓冲区的数据
  HTTP_CODE process_read();
  char * get_line(){return m_read_buf + m_start_line;};
  //解析一行，并返回从状态机的结果
  LINE_STATE parse_line();
  //解析请求行，获取请求方法，url，http版本号
  HTTP_CODE parse_requestLine(char *);
  //解析请求头部
  HTTP_CODE parse_header(char *);
  //解析请求数据
  HTTP_CODE parse_content(char *);
  //处理解析后的请求
  HTTP_CODE do_request();
  //取消文件映射
  void unmap();
  //处理写缓冲区的数据
  bool process_write(HTTP_CODE);
  //将数据按照特定格式写入写缓冲区
  bool write_buffer(const char * format,...);
  //向写缓冲区添加状态行
  bool add_status_line(int,const char * stirng);
  //向写缓冲区添加头部信息
  bool add_header(int);
  //向写缓冲区添加响应内容
  bool add_content(const char *);
  //向写缓冲区添加保持连接信息
  bool add_linger();
  //向写缓冲区添加空行
  bool add_blank_line();
  //向写缓冲区添加响应信息的长度信息
  bool add_content_length(int);
  //向写缓冲区添加响应信息
  bool add_content(string);





private:
  int m_sockfd;
  sockaddr_in m_address;

  char m_read_buf[READ_BUFFER_SIZE];    //最后一个字符的下一个下标
  int m_read_idx;
  int m_start_line;
  int m_checked_idx;

  char m_write_buf[WRITE_BUFFER_SIZE];  //最后一个字符的下一个下标
  int m_write_idx;

  METHOD m_method;                      //http的请求类型
  CHECK_STATE m_check_state;            //http报文的内容分类
  char * m_url;                           //请求资源的路径
  char * doc_root;                      //资源的根目录
  char m_real_file[FILENAME_LEN];
  char * m_version;                     //http版本号
  char * m_host;                        //
  long m_content_length;                //请求内容的长度
  bool m_linger;                        //是否保持连接状态
  char * m_file_address;
  struct stat m_file_stat;
  struct iovec m_iv[2];
  int m_iv_count;
  int bytes_to_send;
  int bytes_have_send;
  char *m_string;                       //存储请求头数据  
  int cgi;                              //是否启用的POST

  int m_close_log;
  int m_TRIGMode;

  char sql_user[100];
  char sql_passwd[100];
  char sql_name[100];
};

#endif
