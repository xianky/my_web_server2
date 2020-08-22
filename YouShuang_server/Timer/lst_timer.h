
/*升序定时器链表*/

#ifndef LST_TIMER
#define LST_TIMER


#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>

#define BUFFER_SIZE 64

/*定时器类型*/

class util_timer; /*前向声明*/

/*用户数据结构，客户端的socket地址，socket文件描述符、读缓存、定时器*/

struct client_data{
    struct sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    util_timer *timer;
};

/*定时器类*/
class util_timer{
    public:
        util_timer():prev(NULL), next(NULL){}

    public:
        time_t expire; //任务的超时时间，这里用绝对时间 //typedef    long  time_t

        void (*cb_func)(client_data*); //任务的回调函数
        /*回调函数处理客户数据、由定时器的执行者传递给回调函数*/

        client_data *user_data; 
        util_timer* prev; //指向前一个定时器
        util_timer* next;//指向下一个定时器
};

/*定时器链表，它是一个升序的，双向的带有头节点和尾节点的*/
class sort_timer_list{
    public:
        
        sort_timer_list():head(NULL), tail(NULL){}
        /*链表被销毁时，删除其中的所有定时器*/
        
        ~sort_timer_list(){
            util_timer *tmp = head;
            while(tmp){
                head = tmp->next;
                delete tmp;
                tmp = head;
            }
        }

        /*将目标定时器timer添加到链表中*/
        void add_timer(util_timer *timer){
            if(!timer){
                return;
            }
            if(!head){
                head = tail = timer;
                return;
            }
            /*如果timer的定时时间小于当前链表中的所有定时器（即第一个），则把该定时器插入链表头部
             * 否则调用重载的add_timer函数把它插入链表合适的位置*/
            if(timer->expire < head->expire){
                timer->next = head;
                head->prev = timer;
                head = timer;
                return;
            }
            add_timer(timer, head);
        }
        
        /*当某个任务发生变化时，（即某个连接又从新活动了），
         * 我们就要重新设置该任务的定时器时间，也就要调整其定时器在链表中的位置，即向后移动*/
        void adjust_timer(util_timer* timer){
            if(!timer){
                return;
            }
            util_timer* tmp = timer->next;
            /*如果被调整的目标定时器处在链表的尾部，或者比它下一个定时器的时间要小，就步移动它的位置*/
            if(!tmp || timer->expire <= tmp->expire){
                return;
            }
            /*如果目标定时器是链表的头节点，则将定时器从链表中取出，并从新插入链表*/
            if(timer == head){
                head = head->next;
                head->prev = NULL;
                timer->next = NULL;
                add_timer(timer, head);
            }
            /*如果目标定时器不是链表的头节点，则将该结点取出然后插入原来位置之后的部分（应为调整是时间会增大）的链表中去*/
            else{
                timer->prev->next = timer->next;
                timer->next->prev = timer->prev;
                add_timer(timer, timer->next);
            }
        }

        /*将目标定时器timer从链表中删除*/
        void del_timer(util_timer* timer){
            if(!timer) return;
            /*下面这个条件成立表示链表中只有一个定时器，即目标定时器*/
            if(timer == head && timer == tail){
                delete timer;
                head = NULL;
                tail = NULL;
                return;
            }
            
            /*如果链表中至少有两个定时器，且目标定时器是链表的头节点，则将链表的头节点重置为原头节点的下一个头节点，然后删除目标节点*/
            if(timer == head){
                head = head->next;
                head->prev = NULL;
                delete timer;
                return;
            }

            /*如果链表中至少有两个定时器，且目标定时器是尾节点*/
            if(timer == tail){
                tail = tail->prev;
                tail->next = NULL;
                delete timer;
                return;
            }
            /*如果定时器位于中间*/
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            delete timer;
        }

        /*SIGALRM信号每次触发就在其信号处理函数中执行一次tick()函数，以处理链表上的到期的任务*/
        //定时检查链表
        void tick(){
            //为什么不能输出？？？？？？？？？？？？？？？？？？//
            printf("进入tick()\n");
            if(!head) return;
//            printf("timer tick!\n");
            time_t cur = time(NULL);//使用绝对时间去和链表中的定时器比较
            util_timer* tmp = head;
            /*从头节点开始依次处理每个定时器，直到遇到一个尚未到期的定时器截至，因为升序链表，后面的肯定也没有到期，*/
            while(tmp){
                /*因为每个定时器都使用绝对时间作为超时值，所以可以判断其时间是否超过当前系统时间cur，来判断是否超时*/
                if(cur < tmp->expire) break;
                
                //当前tmp是超时的
                tmp->cb_func(tmp->user_data); //cb_func是应该被赋值的，这是一个函数指针

                /*执行完定时器的回调函数后，就将它冲链表中删除，并重置头节点*/
                head = tmp->next;
                if(head){
                    head->prev = NULL;
                }
                printf("delete tmp\n");
                delete tmp;
                printf("delete tmp end\n");
                tmp = head;
            }
        }
    private:
        /*一个重载的辅助函数add_timer(util_timer* timer, util_timer* 1st_head);
         * 用来将定时器节点插入到升序链表中去*/
        void add_timer(util_timer *timer, util_timer* lst_head){
            util_timer* prev = lst_head;
            util_timer* tmp = prev->next;
            /*遍历lst_head之后的节点，直到找到一个超时间大于目标节点超时时间的节点停止，将目标节点插到该结点之前*/
            while(tmp){
                if(timer->expire < tmp->expire){
                    prev->next = timer;
                    timer->prev = prev;
                    timer->next = tmp;
                    tmp->prev = timer;
                    break;
                }
                prev = tmp;
                tmp = tmp->next;
            }
            if(!tmp){
                prev->next = timer;
                timer->prev = prev;
                timer->next = NULL;
                tail = timer;
            }
        }

    private:
        util_timer* head;
        util_timer* tail;
};



#endif

