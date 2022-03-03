#include "threadpool.h"
/*线程池实现*/
void sbuf_init(sbuf_t*sp,int n) {
    sp->buf = calloc(n,sizeof(int));
    sp->front = 0;
    sp->rear = 0;
    sp->n = n;
    sem_init(&sp->mutex,0,1);
    sem_init(&sp->slot,0,n);
    sem_init(&sp->clientfd,0,0);
}
void sbuf_deinit(sbuf_t* sp) {
    Free(sp);
}

void sbuf_insert(sbuf_t* sp,int fd) {
    P(&sp->slot);
    P(&sp->mutex);
    //printf("into critical zone\n");
    sp->buf[(++sp->rear) % (sp->n)] = fd;
    //printf("out critical zone\n");
    V(&sp->mutex);
    V(&sp->clientfd);
}

int sbuf_receive(sbuf_t* sp) {
    int fd;
    P(&sp->clientfd);
    P(&sp->mutex);
    fd = sp->buf[(++sp->front) % (sp->n)];
    V(&sp->mutex);
    V(&sp->slot);
    return fd;
}

