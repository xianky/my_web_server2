

/*日志类的头文件*/

#ifndef LOG_H
#define LOG_H

#include <cstdio>
#include <iostream>
#include <cstring>
#include <pthread.h>
#include <stdarg.h>
#include "block_queue.h"

using namespace std;

class Log{
    public:
        //这里使用了单例模式
        static Log* get_instance(){
            static Log instance;
            return &instance;
        }
        /*可选择的参数有日志文件、日志缓冲区大小、最大行数、异步中的阻塞队列的长度即最长日志条队列*/
        bool init(const char* file_name, int log_buf_size = 8192, 
                  int split_lines = 5000000, int max_queue_size = 0);
        //异步写日志公有方法，调用私有方法async_write_log
        static void* flush_log_thread(void* arg){
            Log::get_instance()->async_write_log();
        }

        void write_log(int level, const char* format, ...);

        void flush(void);

    private:
        Log();
        /*析构函数写为虚函数，这个类作为父类，实现多态时，能够更好的释放子类对象，不然之释放父类对象空间*/
        virtual ~Log();
        //从阻塞队列中取出数据写到日志文件中*/
        void* async_write_log(){
            string single_log;
            //从阻塞队列中取出一个日志string，写入文件，没有的话就阻塞在这里，就是写线程一直阻塞在这里，等待条件变量的唤醒
            printf("线程开始pop阻塞\n");
            while(m_log_queue->pop(single_log)){
                /*往日志文件中写内容要对日志文件加锁*/
                printf("log写线程开始work\n");
                m_mutex.lock();
                fputs(single_log.c_str(), m_fp);
                //异步写线程自己fflush，防止fputs慢于log类的flush方法
                fflush(m_fp);
                m_mutex.unlock();
                //printf("已经放到stream中了\n");
            }
        }
    private:
        char dir_name[128]; //路径名
        char log_name[64]; //log文件名
        int m_split_lines; //日志最大行数
        int m_log_buf_size; //日志缓冲区的大小
        long long m_count; //日志行数记录
        int m_today;       //因为按天分类，记录当前事件是那一天
        FILE* m_fp;     //打开log的文件的指针
        char* m_buf;
        block_queue<string>* m_log_queue; //阻塞队列
        bool m_is_async;                  //是否同步的标志位
        locker m_mutex;

};

#define LOG_DEBUG(format, ...)  Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)   Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)   Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...)   Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif














