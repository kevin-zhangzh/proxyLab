#include <stdio.h>
#include"csapp.h"
#include "threadpool.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREAD 20

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

struct uriData
{
    char host[MAXLINE];
    char path[MAXLINE];
    char port[MAXLINE];
};

typedef struct obj {
    char uri[MAXLINE];
    char respHeader[MAXLINE];
    int hsize;
    char respBody[MAX_OBJECT_SIZE];
    int bsize;
    struct obj* next;
    struct obj* prev;
}obj_t;

typedef struct cache {
    obj_t* head;
    obj_t* tail;
    int cacheSize;
    int nreader;
    sem_t rlock;
    sem_t wlock;
}cache_t;

void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
void parseUri(char* uri,struct uriData* u);
void thread();
obj_t* readCache(char* uri);
void writeCache(obj_t* obj);
sbuf_t sbuf;//线程池的buf
cache_t obj_cache;//obj缓存
int main(int argc ,char** argv)
{
    int listenfd,connfd;
    char hostname[MAXLINE],port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    if(argc != 2) {
        fprintf(stderr,"usage %s <port>\n",argv[0]);
    }
    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf,NTHREAD+10);
    cache_init(&obj_cache);
    for(int i = 0;i < NTHREAD;i++) {
        Pthread_create(&tid,NULL,thread,NULL);
    }
    while(1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        Getnameinfo((SA*)&clientaddr,&clientlen,hostname,MAXLINE,
            port,MAXLINE,0);
        printf("hostnme<%s>,port<%s> fd<%d>\n",hostname,port,connfd);
        sbuf_insert(&sbuf,connfd); 
    }
    sbuf_deinit(&sbuf);
    Free(&obj_cache);
    return 0;
}

void cache_init(cache_t* cache) {
    cache->cacheSize = 0;
    cache->head = Malloc(sizeof(obj_t*));
    cache->tail = Malloc(sizeof(obj_t*));
    cache->head->next = cache->tail;
    cache->tail->prev = cache->head;
    cache->tail->next = NULL;
    strcpy(cache->tail->uri,"null");
    cache->nreader = 0;
    sem_init(&cache->rlock,0,5);
    sem_init(&cache->wlock,0,1);
}
void thread() {
    Pthread_detach(Pthread_self());
    while(1) {
        int fd = sbuf_receive(&sbuf);
        doit(fd);
        Close(fd);
    }
}

void doit(int fd) {
    rio_t rio,serverRio;
    char buf[MAXLINE],method[MAXLINE],uri[MAXLINE],version[MAXLINE];
    char httpData[MAXLINE];
    struct uriData u;
    int serverfd;
    int sendByte = 0;
    int objByte = 0;
    Rio_readinitb(&rio,fd);
    if(!Rio_readlineb(&rio,buf,MAXLINE)) {
        return;
    }
    printf("%s\n",buf);
    sscanf(buf,"%s %s %s",method,uri,version);
    if(strcasecmp("GET",method)) {
        clienterror(fd,method,501,"Not Implement" ,"Don't support this method");
        return;
    }
    obj_t* obj = readCache(uri);
    if(obj != NULL) {
        Rio_writen(fd,obj->respHeader,obj->hsize);
        Rio_writen(fd,obj->respBody,obj->bsize);
        return;
    }
    obj = Malloc(sizeof(*obj));
    strcpy(obj->uri,uri); 
    parseUri(uri,&u);
    changeHttpData(&rio,&u,httpData);
    printf("%s %s %s\n",u.host,u.port,u.path);
    serverfd = Open_clientfd(u.host,u.port);
    Rio_readinitb(&serverRio,serverfd);
    Rio_writen(serverfd,httpData,strlen(httpData));
    size_t n;
    while((n = Rio_readlineb(&serverRio,buf,MAXLINE)) != 0) {
        Rio_writen(fd,buf,n);
        sendByte += n;
        strcat(obj->respHeader,buf);
        if(strcmp(buf,"\r\n") == 0) {//resp Header read finish
            break;
        }
    }
    obj->hsize = sendByte;
    while((n = Rio_readnb(&serverRio,buf,MAXLINE)) != 0) {//读服务器返回的object
        Rio_writen(fd,buf,n);
        strcat(obj->respBody,buf);
        sendByte += n;
        objByte += n;
    }
    obj->bsize = objByte;
    if(objByte > MAX_OBJECT_SIZE) {
        Free(obj);
    } else {
        P(&obj_cache.wlock);
        writeCache(obj);
        printf("writeCache ok uri<%s> objsize<%d>\n",obj->uri,obj->bsize);
        V(&obj_cache.wlock);
    }
    printf("Send to client <%d> byte\n",sendByte);
    Close(serverfd);
}

obj_t* readCache(char* uri) {
    P(&obj_cache.rlock);
    obj_cache.nreader++;
    if(obj_cache.nreader == 1) {
        P(&obj_cache.wlock);
    }
    obj_t* node = obj_cache.head->next;
    while(node != NULL) {
        printf("uri<%s>\n",node->uri);
        if(strcmp(node->uri,uri) == 0) {
            
            break;
        }
        node = node -> next;
    }
    obj_cache.nreader--;
    if(obj_cache.nreader == 0) {
        V(&obj_cache.wlock);
    }
    V(&obj_cache.rlock);
    return node;
}

void writeCache(obj_t* obj) {
    while(obj->bsize + obj_cache.cacheSize > MAX_CACHE_SIZE && obj_cache.head->next != obj_cache.tail) {
        obj_t* node = obj_cache.tail -> prev;
        obj_cache.tail -> prev = node->prev;
        node->prev->next = obj_cache.tail;
        obj_cache.cacheSize -= node->bsize;
        Free(node);
    }
    obj->next = obj_cache.head->next;
    obj_cache.head -> next = obj;
    obj->prev = obj_cache.head;
    obj->next->prev = obj;
    obj_cache.cacheSize += obj->bsize;
}

void parseUri(char* uri,struct uriData* u) {
    char* hostp = strstr(uri,"//");
    if(hostp == NULL) {
        printf("error:Invaild uri <%s>\n",uri);
        return;
    }
    hostp += 2;//hostname的起始位置
    char* pathp = strstr(hostp,"/");
    strcpy(u->path,pathp);
    *pathp = '\0';
    char* portp = strstr(hostp,":");
    if(portp != NULL) {
        strcpy(u->port,portp+1);
        *portp = '\0';
    } else {
        strcpy(u->port,"80");
    }
    strcpy(u->host,hostp);
}

void changeHttpData(rio_t* rio,struct uriData* u,char* httpBuf) {
    char reqLine[MAXLINE],hostHdr[MAXLINE],cHdr[MAXLINE],pcHdr[MAXLINE];
    char dataBuf[MAXLINE];
    sprintf(reqLine,"GET %s HTTP/1.0\r\n",u->path);
    sprintf(hostHdr,"Host: %s\r\n",u->host);
    sprintf(cHdr,"Connection: close\r\n");
    sprintf(pcHdr,"Proxy-connection: close\r\n");
    char buf[MAXLINE];
    Rio_readlineb(rio,buf,MAXLINE);
    strcat(dataBuf,buf);
    while( strcmp(buf,"\r\n") ) {
        Rio_readlineb(rio,buf,MAXLINE);
        strcat(dataBuf,buf);
    }
    sprintf(httpBuf,"%s%s%s%s%s",reqLine,hostHdr,cHdr,pcHdr,dataBuf);
}

void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Proxy Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
