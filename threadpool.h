#include "csapp.h"
typedef struct sbuf{
    int* buf;/*socketfd buf*/
    int n;/*capacity*/
    int front;
    int rear;
    sem_t mutex;
    sem_t slot;
    sem_t clientfd;
}sbuf_t;

void sbuf_init(sbuf_t* sp,int n);
void sbuf_deinit(sbuf_t* sp);
void sbuf_insert(sbuf_t* sp,int fd);
int sbuf_receive(sbuf_t *sp);