#include "log.h"
#include<sys/time.h>
#include<string.h>
#include<string>
Log::Log()
{
    m_cur_line = 0;
    m_is_async = false;
}

Log::~Log(){
  if(m_fp != nullptr){
    fclose(m_fp);
  }
}
//异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size){
  //true 为异步日志系统，创建线程进行异步写入日志 
  if(max_queue_size >= 1){
    m_is_async = true;
    m_log_queue = new block_queue<std::string>(max_queue_size);
    pthread_t tid;
    tid = pthread_create(&tid,NULL,flush_log_thread,NULL);
  }
  m_close_log = close_log;
  m_log_buf_size = log_buf_size;
  m_split_lines = split_lines;
  m_buf = new char[m_log_buf_size];
  memset(m_buf,'\0',m_log_buf_size);

  time_t t = time(NULL);
  struct tm *sys_tm = localtime(&t);
  struct tm my_tm = *sys_tm;
  //找到 '/' 最后一次出现的位置
  const char * p = strrchr(file_name,'/');
  char log_full_name[256] = {0};


  if(p == nullptr){
    snprintf(log_full_name,255,"%d_%02d_%02d_%s",my_tm.tm_year + 1900,my_tm.tm_mon + 1,my_tm.tm_mday, file_name);
  }else{
    strcpy(log_name,p + 1);
    strncpy(dir_name,file_name,p - file_name + 1);
    snprintf(log_full_name,255,"%s%d_%02d_%02d_%s",dir_name,my_tm.tm_year + 1900,my_tm.tm_mon + 1,my_tm.tm_mday, log_name); 
  }
  m_day = my_tm.tm_mday;

  m_fp = fopen(log_full_name,"a");
  if(m_fp == nullptr){
    return false;
  }
  return true;
}

void Log::write_log(int level,const char * format,...){
  //秒和纳秒
  struct timeval now = {0,0};
  //从1970年1月1号00:00（UTC）到当前的时间跨度
  gettimeofday(&now,NULL);
  time_t t = now.tv_sec;
  //localtime根据t的秒数和当地时间的格式来格式化,但是localtime只会定义一个指针，每次调用都会修改同一处内存，然后都会返回这同一个指针，所以localtime并不是线程安全的
  struct tm *sys_tm = localtime(&t);
  struct tm my_tm = *sys_tm;
  char start[16] = {0};
  switch (level)
  {
  case 0:
    strcpy(start,"[debug]:");
    break;
  case 1:
    strcpy(start,"[info]:");
    break;
  case 2:
    strcpy(start,"[warn]:");
    break;
  case 3:
    strcpy(start,"[erro]:");
    break;
  default:
    strcpy(start,"[info]:");
    break;
  }
  m_mutex.lock();
  m_cur_line += 1;

  if(m_day != my_tm.tm_mday || m_cur_line % m_split_lines == 0){
    char new_log[256] = {0};
    char tail[16] = {0};
    snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

    if (m_day != my_tm.tm_mday)
    {
        snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
        m_day = my_tm.tm_mday;
        m_cur_line = 0;
    }else{
        snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name,m_cur_line / m_split_lines);
    }
    fopen(new_log,"a");
  }
  m_mutex.unlock();
  va_list valist;

  va_start(valist,format);

  string log_str;
  m_mutex.lock();
  
    int n = snprintf(m_buf, 48, "%d_%02d_%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, start);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valist);
  m_buf[m + n] = '\n';
  m_buf[n + m + 1] = '\0';

  log_str = m_buf;
  m_mutex.unlock();

  if (m_is_async && !m_log_queue->full())
  {
      m_log_queue->push(log_str);
  }else{
      m_mutex.lock();
      fputs(log_str.c_str(), m_fp);
      m_mutex.unlock();
  }
  va_end(valist);
}

void Log::flush(void)
{
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}
