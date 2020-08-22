
/*main.c*/

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "lst_timer.h"
#include "log.h"
#include <cassert>

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define TIMESLOT 5

#define ASYNCLOG //异步写日志
//#define SYNCLOG  //同步写日志

extern int setnonblocking(int fd);
extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);

//设置定时器相关参数
static int pipefd[2];
/*利用升序链表来管理定时器*/
static sort_timer_list timer_lst;
static int epollfd = 0;

//sigalrm和sigterm信号的处理函数
void sig_handler(int sig){
    //为保证函数的可重入性，保留原来的errno
    printf("sig_handler start 就是捕捉到了SIGALRM\n");
    int save_errno = errno;
    int msg = sig;
    //printf("开始向pipefd[1]发送msg，msg = %d\n", msg);
    //=========================================================下面这个错误要哭了===============
    //send(pipefd[1], (char*)msg, 1, 0);//一个字节0-255，1-63的信号//主机序是小段序低地址放低字节//所以一个字节够了；

    //上面这个访问非法地址，相当于传了一个野指针

    send(pipefd[1], (char*)&msg, 1, 0);//一个字节0-255，1-63的信号//主机序是小段序低地址放低字节//所以一个字节够了；
    //sleep(2);
    //printf("sleep 2\n");
    errno = save_errno; //??????????为什么要保存errno？？？？？？？？？？？
}

//设置信号的函数
//为什么不是void(*handler)(int)
void addsig(int sig, void(*handler)(int) = sig_handler, bool restart = true){
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = handler;
    if(restart){
        sa.sa_flags |= SA_RESTART;//被信号中断的程序是否重启
    }
    sigfillset(&sa.sa_mask);//执行信号捕捉函数时，屏蔽信号集，函数执行完后，还原原有的阻塞信号集
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时检测升序链表
void timer_handler(){
    /*处理定时任务，调用tick*/
    //printf("time_handler call tick\n");
    timer_lst.tick();
    //printf("time_handler call tick end\n");
    /*因为一次alarm调用只会引起一次SIGALARM信号，所以这个alarm定时器到了之后，
    /* 再继续定一个alarm(5),保证下一个固定时间间隔再有SIGALARM信号*/
    //int a = 9 + TIMESLOT;
    //#define TIMESLOT 5
    alarm(TIMESLOT);
    /*int temp = TIMESLOT;
      alarm(temp);*/
}
/*定时器回调函数，它删除非活动连接*/                                                                                  
void cb_func(client_data* user_data){
    printf("超时关闭 client's  fd  = %d-----------------------------\n", user_data->sockfd);
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    /*记录线程池中的连接的数量*/
    http_conn::m_user_count --;
    LOG_INFO("close fd %d", user_data->sockfd);
    //Log::get_instance()->flush();//刷新fputs
    /*不用了，因为不管是同步还是异步的时候，都会在fputs之后直接进行fflush(),
     * 这样可以避免异步的时候，有可能fputs有点慢，就错过了flush，
     * 每次的写入都要等下一次flush才会写入,这样就永远差一次flush*/
    printf("stream中应该有 close fd \n");
}
/*error*/
void show_error(int connfd, const char* info){
    printf("%s\n", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

/*mian*/
int main(int argc, char *argv[])
{
#ifdef ASYNCLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 8);//异步，设置了阻塞队列的
#endif
#ifdef SYNCLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 0);//同步没设置阻塞队列长度
#endif
    if(argc < 2){
        //printf("usage:%s ip_address port_number\n", basename(argv[0]));
        printf("usage:%s port_number\n", basename(argv[0]));
        return 1;
    }

    //const char* ip = argv[1];
    int port = atoi(argv[1]);

    /*忽略SIGPIPE*/
    addsig(SIGPIPE, SIG_IGN);
    
    /*创建连接池*/
    connection_pool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "root", "123456", "tinywebdb", 3306, 8);


    /*创建线程池*/
    threadpool<http_conn> *pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }
    catch(...){
        return 1;
    }

    /*预先为每一个连接客户分配一个http_conn对象*/
    http_conn* users = new http_conn[MAX_FD];//MAX_FD = 65536;
    assert(users);
    //int user_count = 0;
    
    //初始化数据库的读取表
    users->initmysql_result(connPool);//数据库中有8个连接
    
    
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    //struct linger 包含在头文件#include <arpa/inet.h>
    struct linger tmp = {1, 0};//设置监听的listenfd的tcp断开连接的方式
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    /*没加端口复用*/ 
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    //inet_pton(AF_INET, ip, &address.sin_addr.s_addr);
    int flag = 1;                                                         
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    /*bind*/
    ret = bind(listenfd, (struct sockaddr*)&address, (socklen_t)sizeof(address));
    assert(ret >= 0);

    /*listen*/
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];//也就是epoll树上最多返回数组大小的活动的fd//MAX_EVENT_NUMBER = 10000；

    epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);//监听的fd就不要设置为one_shot了

    http_conn::m_epollfd = epollfd;//所有任务类共享的，加到一颗epoll树上
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);                                                                                                
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false); 
    /*设置信号处理函数*/
    addsig(SIGALRM, sig_handler, false);//感觉这里传true也可以，帮助被信号打断的进程从新启动
    addsig(SIGTERM, sig_handler, false);
    bool stop_server = false;
    /*依然用一个结构体数组来存储用户数据*/
    //感觉这个可以放到http_conn类中去,让每个任务自己存储自己的定时器*///===========================？？？？？？？？？？？这样更麻烦，不符合低耦合概念
    client_data* users_timer = new client_data[MAX_FD];
    bool timeout = false;
    alarm(TIMESLOT);

    while(!stop_server){
        printf("epoll_wait start\n");
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        printf("number%d\n", number);
        if(number < 0 && (errno != EINTR)){
            printf("epoll failure\n"); //思考：给信号设置了RESTART后阻塞是否会返回，返回什么？？？？？
            LOG_ERROR("%s", "epoll_wait failure");
            break;
        }
        
        for(int i = 0; i < number; i++){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                printf("来了一个连接请求\n");
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                //accept每次接收一个请求连接，下一个要等epoll_wait的下一轮
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                if(connfd < 0){
                    printf("errno is:%d\n", errno);
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                if(http_conn::m_user_count >= MAX_FD){
                    //连接太多了，要等一些任务结束
                    show_error(connfd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;//处理下一个活动的fd。
                }
                /*初始化客户连接*/
                char ipbuf[64];
                printf("client ip:%s************* port:%d***************************\n", inet_ntop(AF_INET, &client_address.sin_addr.s_addr, ipbuf, sizeof(ipbuf)),
                       ntohs(client_address.sin_port));
                users[connfd].init(connfd, client_address);
                //创建定时器，设置回调函数与超时时间，然后绑定定时器与用户数据，再加入链表
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;

                util_timer* timer = new util_timer;
                /*=======================????????????下面这句话没加出现段错误？？？？？？？？？？？？================*/
                timer->user_data = &users_timer[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                timer_lst.add_timer(timer);
                printf("client connect successful,cfd = %d\n", connfd);
            }
            else if(events[i].events & (EPOLLRDHUP)){
                /*如果有异常，直接关闭cfd*/
                /*这里可以用cb_func(),也可以用http_conn类的close_conn()方法*/
                /*都可以，都会将http_conn::m_user_count--, 以及从树上删除并close*/
                printf("fd  ==  %d   发生EPOLLRDHUP\n", sockfd);
                perror("epoll wait");
                users[sockfd].close_conn();
                util_timer *timer = users_timer[sockfd].timer;
                //timer->cb_func(&users_timer[sockfd]);

                //if应该可以不用判断
                if(timer) timer_lst.del_timer(timer);

            }
            else if(sockfd == pipefd[0] && (events[i].events & EPOLLIN)){
                //printf("pipefd[0] 上有事件发生\n");
                int sig;
                char signals[1024] = {0};
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                //printf("ret = %d \t recv from pipedfd%d\n", ret, signals[0]);
                if(ret == -1){
                    //handle the error
                    continue;
                }
                if(ret == 0){
                    continue;//为什么pipefd[1]会关闭？？？？？？？？？？？？？？？？
                }
                else{
                    for(int i = 0; i < ret; ++i){
                        switch(signals[i]){
                        case SIGALRM:
                            {
                                /*用timeout变量标记有定时任务要处理，但是不立即处理定时任务，
                                 * 这是因为定时任务优先级不高，我们先处理其其他的任务*/
                                timeout = true;
                                break;
                            }
                        case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            else if(events[i].events & EPOLLIN){
                printf("读事件来了\n");
                util_timer *timer = users_timer[sockfd].timer;
                //主线程模拟内核，将数据监听到cfd发来数据后，直接就把数据读到用户缓冲区中，
                //然后就让处理这个cfd的线程开始执行process_read();(相当于通知线程来处理了)
                //这就是同步IO模拟的proactor
                /*根据读的结果，决定是将任务添加到线程池区中的任务队列，然线程去处理，还是关闭连接*/
                if(users[sockfd].read()){
                    LOG_INFO("deal with the client(%s), port%d", inet_ntoa(users[sockfd].get_address()->sin_addr), ntohs(users[sockfd].get_address()->sin_port));
                    //Log::get_instance()->flush();
                    
                    //printf("stream 中应该有deal with ...\n");
                    if(timer){
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        //printf("adjust timer once\n");
                        LOG_INFO("%s", "read adjust timer once");
                        //Log::get_instance()->flush();
                        //printf("stream 中应该有read adjust timer ...\n");
                        timer_lst.add_timer(timer);
                    }
                    else{
                        printf("read timer == NULL\n");
                    }
                    //printf("加入线程池\n");
                    pool->append(users + sockfd);
                }
                else{
                    users[sockfd].close_conn();
                    //timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
            else if(events[i].events & EPOLLOUT){
                //主线程模拟内核，将用户写缓冲区中的数据发送出去。
                //而不是让每个线程单独去发送出去。
                /*根据写的结果，决定是否关闭连接*/
                printf("写事件来了\n");
                util_timer *timer = users_timer[sockfd].timer;
                //主线程模拟内核，将数据监听到cfd发来数据后，直接就把数据读到用户缓冲区中，
                //然后就让处理这个cfd的线程开始执行process_read();(相当于通知线程来处理了)
                //这就是同步IO模拟的proactor
                /*根据读的结果，决定是将任务添加到线程池区中的任务队列，然线程去处理，还是关闭连接*/
                if(users[sockfd].write()){
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    //Log::get_instance()->flush();
                    //printf("stream 中应该有send data to the client...\n");
                        
                    if(timer){
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "write adjust timer once");
                        //Log::get_instance()->flush();
                        //printf("stream 中应该有write adjust timer ...\n");
                        //printf("adjust timer once\n");
                        timer_lst.add_timer(timer);
                    }
                    else{
                        printf("write timer == NULL\n");
                    }
                }
                else{
                    users[sockfd].close_conn();
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
            else{}
        }//for
        if(timeout){
            printf("timeout == 1\n");
            timer_handler();
            timeout = false;
            printf("timeout == 0\n");
        }
    }//while

    close(epollfd);
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete []users;
    delete []users_timer;
    delete pool;
    return 0;
}





















