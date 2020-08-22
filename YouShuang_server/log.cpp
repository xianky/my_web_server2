
/*日志类的实现*/

#include <time.h>
#include <sys/time.h>

#include "log.h"

Log::Log(){
    m_count = 0; //初始化日志行数的记录
    m_is_async = false; //是否异步吧？？？？i
}

Log::~Log(){
    if(m_fp != NULL){
        fclose(m_fp);
        cout << "析构执行" << endl;
    }
}
//异步需要设置阻塞队列的长度，同步不需要
//Log::get_instance()->init("ServerLog", 2000, 800000, 8); //异步日志模型
bool Log::init(const char* file_name, int log_buf_size, int split_lines, int max_queue_size){//
    printf("log 类创建\n");
    //如果设置了max_queue_size,则为异步，因为要用阻塞队列
    if(max_queue_size >= 1){
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数，是线程的入口函数，表示创建线程异步写日志、
        printf("进入线程处理函数\n");
        pthread_create(&tid, NULL, flush_log_thread, NULL);//阻塞队列中就只有一个工作线程。阻塞队列中的锁它随便用，没人抢
        //但是，这个线程会阻塞再pop那里，等待push进去一个string，即任务。pop就得到那个string，然后线程就fputs到日志文件中
    }
    m_log_buf_size = log_buf_size;//日志缓冲区大小,一条log记录的最大长度
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t tt = time(NULL); //当前时间，1970以来的秒数
    struct tm* sys_tm = localtime(&tt);
    struct tm my_tm = *sys_tm;

    const char* p = strrchr(file_name, '/');
    char log_full_name[256] = {0};
    //把时期放进创建的文件名中方便查找
    memset(log_name, 0, sizeof(log_name));
    memset(dir_name, 0, sizeof(dir_name));
    if(p == NULL){
        strcpy(log_name, file_name);
        //??????????????????????=======256应该也可以把================？？？？？？？？？？？
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else{
        strcpy(log_name, p+1);
        strncpy(dir_name, file_name, p - file_name + 1);//会把'/'也copy过去，但是应该给dir_name先全部memset(0);？？？？？？？？
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }
    m_today = my_tm.tm_mday;//表示创建log文件的日子
    m_fp = fopen(log_full_name, "a");//a arg表示没有就创建，一般这个文件都会创建，以“追加”方式打开文件。
    if(m_fp == NULL){
        return false;
    }
    return true;
}

//下面这个被宏调用，头文件中定义的4个宏函数
//在要进行日志写入的时候调用这个函数，这个函数会根据同步还是异步，进行同步写还是交给写线程去写。
//比如
/*LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
 *Log::get_instance()->flush();
 */

void Log::write_log(int level, const char* format, ...){
     struct timeval now = {0, 0};//当前写日志条目的时间
     gettimeofday(&now, NULL);
     time_t t = now.tv_sec;
     struct tm* sys_tm = localtime(&t);
     struct tm my_tm = *sys_tm;
     char s[16] = {0};
     switch(level)
     {
     case 0:
         {
             strcpy(s, "[debug]:");
             break;
         }
     case 1:
         {
             strcpy(s, "[info]");
             break;
         }
     case 2:
         {
             strcpy(s, "[warn]");
             break;
         }
     case 3:
         {
             strcpy(s, "[error]");
             break;
         }
     default:
         {
             strcpy(s, "[info]");
             break;
         }
     }
     //写入一个log，对m_count++, 其中m_split_lines是限制日志文件的条目的最大数量
     m_mutex.lock();
     m_count++;
     
     if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //就是新的一天创建一个日志，或者日志满了创建一个新日志。
     {
         char new_log[256] = {0};
         fflush(m_fp);//刷新之前的log文件的文件流，保证数据流中的数据都已经全部进入之前的log文件
         fclose(m_fp);
         char tail[16] = {0};
         snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
         //如果是新的一天
         if(m_today != my_tm.tm_mday){
             snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
             m_today = my_tm.tm_mday;
             m_count = 0;
         }
         //如果是因为日志文件写满了
         else{
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);//
         }
         m_fp = fopen(new_log, "a");
     }
     m_mutex.unlock();
     va_list valst;
     va_start(valst, format);
     
     string log_str;
     m_mutex.lock();
     //写入的具体的时间内容格式
     int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s", my_tm.tm_year + 1900, 
                      my_tm.tm_mon + 1, my_tm.tm_mday, my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
     int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
     m_buf[n + m] = '\n';
     m_buf[n + m + 1] = '\0';
     log_str = m_buf;
     m_mutex.unlock();
    //异步就交给工作线程去写到文件中，同步就主线程自己写到文件中去，会慢一点
     if(m_is_async && !m_log_queue->full()){
         printf("执行push\n");
         if(m_log_queue->push(log_str)){
             printf("执行push sucess\n");
         }
     }
     else{
         m_mutex.lock();
         fputs(log_str.c_str(), m_fp);
         //要不然同步也在这里自己fflush，就不用类的方法flush了。
         fflush(m_fp);
         m_mutex.unlock();
     }
     va_end(valst); 
}

void Log::flush(void){
    m_mutex.lock();
    //强制刷新写流入缓冲区
    fflush(m_fp);
    m_mutex.unlock();
    printf("执行flush>>>>>>>>>>>>>>>>>>>>>>\n");
}









