

/*半同步/半反应堆的并发模式来实现线程池*/

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
/*引入前面的线程同步机制的包装类*/
#include "locker.h"


//线程池类，将它定义为模板类是为了代码复用。模板参数T是任务类
//（不同情况有不同任务类型，就像之前写的线程池中的NJOB类/home/xky/web_server_folder/TreadPool/threadpool.c）
template <typename T>
class threadpool
{
public:
    /*参数thread_number是线程池中线程的数量，max_requests是请求（任务）队列中最多允许的，等待处理的请求的数量*/
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    /*往任务队列中添加一个任务*/
    bool append(T *request);

private:
    /*工作线程运行的函数，它不断的从任务队列中取出任务执行之*/
    //worker是线程入口函数，run才是线程真正执行的函数
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number; /*线程池中的线程数量*/
    int m_max_requests; /*请求队列中允许的最大请求数*/
    pthread_t *m_threads; /*执行队列用的数组实现， 描述线程池的数组，其大小为m_thread_number;之前线程池中任务队列和执行队列都用的双向连表*/
    std::list<T*> m_workqueue; //请求队列（任务队列），名字起得不好,这里的任务队列用的是STL连表
    locker m_queuelocker; /*保护任务队列的互斥锁*/
    sem m_queuestat; /*信号量来判断是否任务队列中有任务*/
    bool m_stop; //是否结束线程， 相当与之前线程池中的每个线程数据结构的的terminate成员变量,不过这里他被放在线程池类中
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : 
    m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_threads(NULL)
{
    if((thread_number <= 0) || (max_requests <= 0)){
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];//线程池的构造函数中初始化线程数组（执行队列）
    if(!m_threads){
        throw std::exception();
    }
    /*for循环创建thread_number个线程，并把他们设为分离线程*/
    for(int i = 0; i < thread_number; i++){
        printf("create the %dth thread\n", i);
        if(pthread_create(&m_threads[i], NULL, worker, this) != 0){
            delete []m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i]) != 0){
            delete []m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete []m_threads;
    m_stop = true;//通知线程结束自己？和之前的线程池一样的作用？
    //m_stop表示要求线程池的所有线程都停止
}

template<typename T>
bool threadpool<T>::append(T *request){
    /*操作工作(任务)队列时一定要加锁，因为它被所有线程共享访问操作*/
    m_queuelocker.lock();//这里的互斥锁是定义好的类对象，直接调用对象的方法
    if(m_workqueue.size() > m_max_requests){
        //任务队列已经满了，即已经有m_max_requests个任务在任务队列中了，加入任务失败
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    //给信号量加一，表示在任务队列中又添加了一个任务
    m_queuestat.post();
    return true;
}

//worker是线程入口函数，run才是线程真正执行的函数
template<typename T>
void* threadpool<T>::worker(void *arg){
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(!m_stop){
        //信号量减1，若原本信号量为0， 则阻塞，等待任务加入任务队列后信号量+1；
        //信号量也是一种锁,为0就锁住，可能多个线程都在运行
        m_queuestat.wait(); 
        //对多个线程共享的任务队列上锁，一个一个来访问任务队列，即使任务队列中有多个任务，即使你这个线程没有被信号量锁住
        m_queuelocker.lock();
        //???????????
        //个人觉得，下面这个判空没意义，信号量指示有多少个任务在队列中，你信号量放进来的线程就是任务数，不可能出现一个线程取走两个任务，并且只耗费一个信号量。
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        //???????????
        //这个任务队列取出来的为NULL？没有必要吧
        if(!request){
            continue;
        }
        //worker -->  run  -->  request->process();
        //请求（任务自带的执行函数才知道让线程怎么处理这个任务）process
        request->process();
    }
}

#endif
