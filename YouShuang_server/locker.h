
/*
 对线程的三种同步方式进行封装
 */

#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

/*封装信号量的类*/
class sem{
public:
    /*创建并初始化的信号量*/
    sem(){
        //初始化 第二个0为在线程间使用信号量，第三个0表示信号量的初值，成功返回0
        if(sem_init(&m_sem, 0, 0) != 0){
            /*构造函数没有返回值，可以通过抛出异常来报告错误*/
            throw std::exception();
        }
    }
    sem(int num){
        if(sem_init(&m_sem, 0, num) != 0){
            throw std::exception();
        }
    }
    /*析沟函数，销毁信号量*/
    ~sem(){
        sem_destroy(&m_sem);
    }
    /*等待信号量, 给信号量加锁即--*/
    bool wait(){
        return sem_wait(&m_sem) == 0;
    }
    /*增加信号量即++, post*/
    bool post(){
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

/*封装互斥锁的类*/
class locker{
public:
    /*创建并初始化互斥锁*/
    locker(){
        if(pthread_mutex_init(&m_mutex, NULL) != 0){
            throw std::exception();
        }
    }
    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }
    /*获取锁*/
    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    /*释放锁*/
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    pthread_mutex_t* get(){
        return &m_mutex;
    }
private:
    pthread_mutex_t m_mutex;
};
/*封装条件变量的类*/
class cond{
public:
    /*创建并初始化条件变量*/
    cond(){
        /*if(pthread_mutex_init(&m_mutex, NULL) != 0){
            throw std::exception();
        }*/
        if(pthread_cond_init(&m_cond, NULL) != 0){
            /*构造函数中一旦出现问题，就应该立即释放已经成功分配了的资源*/
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    /*销毁条件变量*/
    ~cond(){
 //       pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }
    /*等待条件变量*/
    bool wait(pthread_mutex_t *m_mutex){
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
//        return ret;//==---------------------------又是一个bug----------------------
        return ret == 0;
    }
    //???????????????????????????????????????????????????????????下面这个函数没看//
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    /*唤醒等待条件变量的线程*/
    bool signal(){
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast(){
        return pthread_cond_broadcast(&m_cond) == 0;
    }
    
private:
    pthread_cond_t m_cond;
//    pthread_mutex_t m_mutex;
};


#endif

