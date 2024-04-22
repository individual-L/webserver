#ifndef LOG_H
#define LOG_H
#include"block_queue.h"
#include<pthread.h>
#include<string>
#include<stdio.h>
#include"block_queue.h"
using namespace std;
class Log{
public:
  Log * get_instance(){
    static Log log();
    return &log;
  }
  static void *flush_log_thread(void *args)
  {
      Log::get_instance()->async_write_log();
  }
  //初始化成员变量，创建日志文件，初始化日志名（加上时间信息）
  bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 50000, int max_queue_size = 0);
  //强制刷新缓冲区fflush(m_buf);
  void flush(void);
  //像缓冲区写入信息
  void write_log(int level, const char *format, ...);
private:
  void *async_write_log(){
    string single_log;
    //从阻塞队列中取出一个日志string，写入文件
    while (m_log_queue->pop(single_log))
    {
        m_mutex.lock();
        fputs(single_log.c_str(), m_fp);
        m_mutex.unlock();
    }
  };
  Log();
  ~Log();
private:
  char log_name[128];               //日志名
  char dir_name[128];               //路径名
  int m_log_buf_size;               //日志缓冲区大小
  int m_day;                        //记录日志的日数
  int m_split_lines;                 //最好行数
  long long m_cur_line;             //当前行数
  FILE * m_fp;                      //指向日志文件的指针
  char *m_buf;                      //缓冲区
  block_queue<string> *m_log_queue; //日志请求队列
  bool m_is_async;                  //是否启动异步
  locker m_mutex;                   //互斥锁
  int m_close_log;                  //是否关闭日志

}
#define LOG_DEBUG(format,...){
  if(m_close_log == 0){
    get_instance()->write_log(0,format,##__VA_ARGS__);
  }
  get_instance()->flush();
}
#define LOG_INFO(format,...){
  if(m_close_log == 0){
    get_instance()->write_log(1,format,##__VA_ARGS__);
  }
  get_instance()->flush();
}
#define LOG_WARN(format,...){
  if(m_close_log == 0){
    get_instance()->write_log(2,format,##__VA_ARGS__);
  }
  get_instance()->flush();
}
#define LOG_ERROR(format,...){
  if(m_close_log == 0){
    get_instance()->write_log(3,format,##__VA_ARGS__);
  }
  get_instance()->flush();
};


#endif
