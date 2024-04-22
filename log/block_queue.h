#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H
#include"../lock/lock.h"
#include<sys/time.h>
#include<stdlib.h>
template<class T>
class block_queue{
public:
  block_queue(int max_size = 1000):m_max_size(max_size),m_cur_size(0),m_front(-1),m_back(-1){
    if(max_size <= 0){
      exit(-1);
    }
    m_array = new T[max_size];
  }
  ~block_queue(){
    m_mutex.lock();
    if(m_array != nullptr){
      delete [] m_array;
    }
    m_mutex.unlock();
  }
  bool full(){
    m_mutex.lock();
    if (m_cur_size >= m_max_size)
    {

        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
  }
  bool empty(){
    m_mutex.lock();
    if(m_cur_size == 0){
      m_mutex.unlock();
      return true;
    }
    m_mutex.unlock();
    return false;
  }
  void clear(){
    m_mutex.lock();
    m_cur_size = 0;
    m_front = -1;
    m_back = -1;
    m_mutex.unlock();
  }
  bool front(T & value){
    m_mutex.lock();
    if (0 == m_cur_size)
    {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_front];
    m_mutex.unlock();
    return true;
  }
  bool back(T & value){
    m_mutex.lock();
    if (0 == m_cur_size)
    {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_back];
    m_mutex.unlock();
    return true;
  }
  int size(){
    int tmp = 0;
    m_mutex.lock();
    tmp = m_cur_size;
    m_mutex.unlock();
    return tmp;
  }
  int max_size(){
    int tmp = 0;

    m_mutex.lock();
    tmp = m_max_size;

    m_mutex.unlock();
    return tmp;
  }
  bool push(const T & item){
    m_mutex.lock();
    if(m_cur_size >= m_max_size){
      m_cond.broadcast();
      m_mutex.unlock();
      return false;
    }

    back = (back + 1) % m_max_size;
    m_array[back] = item;
    m_cur_size += 1;

    m_cond.broadcast();
    m_mutex.unlock();
    return true;
  }
  bool pop(T &item){
    m_mutex.lock();
    if(m_cur_size <= 0){
      if(!m_cond.wait(m_mutex.get())){
        m_mutex.unlock();
        return false;
      }
    }

    m_front = (front + 1) % m_max_size;
    item = m_array[m_front];
    m_cur_size -= 1;
    m_mutex.unlock();
    return true;
  }
  bool pop(T &item,int time_out){
    struct timespec t = {0,0};
    struct timeval now = {0,0};
    gettimeofday(&now,NULL);
    m_mutex.lock();
    if(m_cur_size <= 0){
      t.tv_sec = now.tv_sec + time_out / 1000;
      t.tv_nsec = (time_out % 1000) * 1000;
      if(!m_cond.wait(m_mutex.get(),t)){
        m_mutex.unlock();
        return false;
      }
    }
    if (m_cur_size <= 0)
    {
        m_mutex.unlock();
        return false;
    }
    m_front = (front + 1) % m_max_size;
    item = m_array[m_front];
    m_cur_size -= 1;
    m_mutex.unlock();
    return true; 
  } 
private:
  locker m_mutex;
  cond m_cond;

  T *m_array;
  int m_cur_size;
  int m_max_size;
  int m_front;
  int m_back;


};



#endif
