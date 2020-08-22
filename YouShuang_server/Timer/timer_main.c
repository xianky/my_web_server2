
/*处理非活动连接*/

/*SIGALARM信号的信号处理函数利用管道通知主循环执行链表中定时器的定时任务*/

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <string.h>
#include <errno.h>
#include "lst_timer.h"

#define TIMESLOT 5
#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024

static int pipefd[2];
/*利用升序链表来管理定时器*/
static sort_timer_list timer_lst;
static int epollfd = 0;

int setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    //int new_option = old_option | SOCK_NONBLOCK;//sock_onnblock和o_nonblock好像是一样的
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd){
    epoll_event event;
    event.data.fd = fd;
    /*if(fd == pipefd[0]){
        event.events = EPOLLIN;
    }
    else event.events = EPOLLIN | EPOLLET;*/
    event.events = EPOLLIN | EPOLLET;//管道文件描述符也设置为边沿触发了
    //只要epollwait没有返回过这个文件描述符，即便被信号中断，那么下一次还是会返回
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void sig_handler(int sig){
    //为保证函数的可重入性，保留原来的errno
    printf("sig_handler 捕捉到了信号%d\n", sig);
    int save_errno = errno;
    int msg = sig;
    printf("开始向pipefd[1]发送msg，msg = %d\n", msg);
    //=========================================================下面这个错误要哭了===============
    //send(pipefd[1], (char*)msg, 1, 0);//一个字节0-255，1-63的信号//主机序是小段序低地址放低字节//所以一个字节够了；
    
    //上面这个访问非法地址，相当于传了一个野指针
    
    send(pipefd[1], (char*)&msg, 1, 0);//一个字节0-255，1-63的信号//主机序是小段序低地址放低字节//所以一个字节够了；
    //sleep(2);
    //printf("sleep 2\n");
    errno = save_errno; //??????????为什么要保存errno？？？？？？？？？？？
    
}
 

void addsig(int sig){
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void timer_handler(){
    /*处理定时任务，调用tick*/
    printf("time_handler call tick\n");
    timer_lst.tick();
    printf("time_handler call tick end\n");
    /*因为一次alarm调用只会引起一次SIGALARM信号，所以这个alarm定时器到了之后，
     * 再继续定一个alarm(5),保证下一个固定时间间隔再有SIGALARM信号*/
    //int a = 9 + TIMESLOT;
    //#define TIMESLOT 5
    //alarm(TIMESLOT);
    int temp = TIMESLOT;
    alarm(temp);
}

/*定时器回调函数，它删除非活动连接*/
void cb_func(client_data* user_data){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    printf("close fd %d\n", user_data->sockfd);
}


int main(int argc, char *argv[])
{
    if(argc < 2){
        printf("usage:%s port\n", basename(argv[0]));
        exit(1);
    }
    int port = atoi(argv[1]);
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    if(ret == -1){
        perror("bind error");
        exit(1);
    }
    ret = listen(listenfd, 5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd);

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0]);

    /*设置信号处理函数*/
    addsig(SIGALRM);
    addsig(SIGTERM);
    bool stop_server = false;

    client_data* users = new client_data[FD_LIMIT];
    bool timeout = false;
    alarm(TIMESLOT);

    int i = 1;
    int number;
    /*sigset_t pset;
    sigemptyset(&pset);
    sigaddset(&pset, SIGALRM);
    ret = sigprocmask(SIG_BLOCK, &pset, NULL);
    if(ret == -1){
        perror("sigprocmask error");
        exit(1);
    }*/
    while(!stop_server){
        /*do{
            printf("epoll_wait starting  %d\n", i);
            number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
            printf("epoll_wait ret number = %d\n", number);
            printf("epoll_wait end  %d\n", i++);
        }while(number == -1 && errno == EINTR);*/
        number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        printf("epoll_wait return ret = %d\n", number);
        if(number == -1 && errno != EINTR){
            perror("epoll wait error");
            exit(1);
        }
        for(int i = 0; i < number; i++){
            printf("epoll_wait ret number2 = %d\n", number);
            int sockfd = events[i].data.fd;
            if(sockfd == pipefd[0]){
                printf("pipefd[0]来了\n");
            }
            /*处理新来的连接*/
            if(sockfd == listenfd){
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                addfd(epollfd, connfd);
                users[connfd].address = client_address;
                users[connfd].sockfd = connfd;
                /*创建定时器，设置回调函数与超时间，然后绑定定时器与用户数据，再加入链表*/
                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer;
                timer_lst.add_timer(timer);
                printf("client connet sucessful, cfd = %d\n", connfd);
            }
            else if(sockfd == pipefd[0] && events[i].events & EPOLLIN){
                printf("pipefd[0] 上有事件发生\n");
                int sig;
                char signals[1024] = {0};
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                printf("ret = %d \t recv from pipedfd%d\n", ret, signals[0]);
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
                memset(users[sockfd].buf, '\0', BUFFER_SIZE);
                ret = recv(sockfd, users[sockfd].buf, BUFFER_SIZE - 1, 0);
                printf("get %d bytes of client date %s from %d\n", ret, users[sockfd].buf, sockfd);
                util_timer *timer = users[sockfd].timer;
                if(ret < 0){
                    /*如果发生读错误，则关闭连接，并移除对应的定时器*/
                    if(errno != EAGAIN){
                        cb_func(&users[sockfd]);//关闭了通信的文件描述符
                        if(timer){
                            timer_lst.del_timer(timer);//从定时器链表删除对应的定时器
                        }
                    }
                }
                else if(ret == 0){
                    /*如果对方关闭fd，我们也关闭，并移除对应的timer*/
                    printf("client closed!\n");
                    cb_func(&users[sockfd]);
                    if(timer){
                        timer_lst.del_timer(timer);
                    }

                }
                else{
                    /*有可读数据*/
                    if(timer){
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        printf("adjust timer once\n");
                        timer_lst.add_timer(timer);
                    }
                }
            }
            else{
                    //other 
            }
        }//end for
        if(timeout){
            printf("timeout == 1\n");
            timer_handler();
            timeout = false;
            printf("timeout == 0\n");
        }
    }
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete []users;
    return 0;
}
















