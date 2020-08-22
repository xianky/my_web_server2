#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "threadpool.h"

#if 1
//debug
//0 --> 1000 task ----> printf
//任务队列节点，连表串起来
struct NJOB{
    void *process();
    void *user_data;
    struct NJOB *prev;
    struct NJOB *next;
};
void*  NJOB::process(){
    printf("%d\t", *(int*)user_data);
    fflush(stdout);
}
int main(){
    threadpool<struct NJOB> *pool = new threadpool<struct NJOB>;
    
    printf("for start\n");
    for(int i = 0; i < 1000; i ++){
        struct NJOB *job = (struct NJOB *)malloc(sizeof(struct NJOB));
        int *data = (int*)malloc(sizeof(int));
        *data = i;
        job->user_data = data;
        //LL_ADD(job, pool->jobs);
        pool->append(job);
        //printf("for %d end\n", i);
    }
    //睡一秒，不要在线程工作还没完就去销毁线程池，不然就是工作没做完，并且任务队列没有被释放
    sleep(1);
    printf("\n");
    delete(pool);
    return 0;
}
#endif
