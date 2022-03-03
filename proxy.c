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


void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
void parseUri(char* uri,struct uriData* u);
void thread();
sbuf_t sbuf;
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
    return 0;
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
        if(strcmp(buf,"\r\n") == 0) {//resp Header read finish
            break;
        }
    }
    while((n = Rio_readnb(&serverRio,buf,MAXLINE)) != 0) {//读服务器返回的object
        Rio_writen(fd,buf,n);
        sendByte += n;
        objByte += n;
    }
    printf("Send to client <%d> byte\n",sendByte);
    Close(serverfd);
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
