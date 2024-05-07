#ifndef LOCKER_H
#define LOCKER_H

#include<pthread.h>
#include<exception>
#include<semaphore.h>
//互斥锁
class locker{
public:
  locker(){
    if(pthread_mutex_init(&m_mutex,NULL) != 0){
      throw std::exception();
    }
  }
  ~locker(){
    pthread_mutex_destroy(&m_mutex);
  }
  bool lock(){
    pthread_mutex_lock(&m_mutex);
  }
  bool unlock(){
    pthread_mutex_unlock(&m_mutex);
  }
  pthread_mutex_t* get(){
    return &m_mutex;
  }

private:
  pthread_mutex_t m_mutex;
};
//信号量
class sem{
private:
  sem_t m_sem;
public:
  sem(){
    if(sem_init(&m_sem,0,0) != 0){
      throw std::exception();
    }
  }
  sem(int val){
    if(sem_init(&m_sem,0,val) != 0){
      throw std::exception();
    }
  }
  ~sem(){
    sem_destroy(&m_sem);
  }
  bool post(){
    return sem_post(&m_sem) == 0;
  }
  bool wait(){
    return sem_wait(&m_sem) == 0;
  }
};
//条件变量
class cond{
private:
pthread_cond_t m_cond;
public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)
    {
      int res = 0;
      //std::pthread_mutex_lock(m_mutex);
      res = pthread_cond_wait(&m_cond,m_mutex);
      //std::pthread_mutex_unlock(m_mutex);
      return res == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex,struct timespec t)
    {
      int res = 0;
      //std::pthread_mutex_lock(m_mutex);
      res = pthread_cond_timedwait(&m_cond,m_mutex,&t);
      //std::pthread_mutex_unlock(m_mutex);
      return res == 0;
    }
    bool signal(){
      return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast(){
      return pthread_cond_broadcast(&m_cond) == 0;
    }
};


#endif
