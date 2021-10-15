#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Constant req headers */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";

volatile sig_atomic_t exitFlag = 0;

/* Threaded service */
void *run_thread(void *);

/* Thread exit */
void *end_thread(int *, int *);

void sig_handler(int sig){
    exitFlag = 1;
}

int main(int argc, char **argv)
{
    // ignore SIGPIPE
    Signal(SIGPIPE, SIG_IGN);
    // install SIGTERM handler
    // struct sigaction action;
    // action.sa_handler = sig_handler;
    // sigaction(SIGTERM, &action, NULL);

    // proxy cache
    cache_init();

    int port;
    int listenfd;
    int *connfdp;
    struct sockaddr_storage clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    pthread_t tid;

    // check command line
    if(argc != 2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    port = atoi(argv[1]);
    if(port < 0 || port > 65535){
        fprintf(stderr, "Port number out of range\n");
        exit(1);
    }
    listenfd = Open_listenfd(argv[1]);

    // spawn a thread on request
    while(1){
        connfdp = (int *)Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Pthread_create(&tid, NULL, run_thread, connfdp);
        // if(exitFlag){
        //     cache_deinit();
        //     return 0;
        // }
    }
    
    cache_deinit();
    return 0;
}

void *run_thread(void *vargp)
{
    // pick up fd and free heap space
    int clientfd = *((int *)vargp);
    Pthread_detach(Pthread_self());
    free(vargp);
    // not connected to server yet
    int serverfd = -1;

    char req[MAXBUF];
    char req_fwd[MAXBUF];
    char resp[MAXBUF];
    char object[MAX_OBJECT_SIZE];

    char method[8];
    char url[MAXLINE];
    char version[16];
    char protocol[8];
    char hostname[MAXLINE];
    char port[8];
    char resource[MAXLINE];
    char *tmp;

    size_t objectlen, len;
    rio_t rio_client, rio_server;

    // receive req line
    Rio_readinitb(&rio_client, clientfd);
    if(Rio_readlineb(&rio_client, req, MAXBUF) < 0)
        return end_thread(&clientfd, &serverfd);
    // split req line
    if(sscanf(req, "%s %s %s", method, url, version) != 3)
        return end_thread(&clientfd, &serverfd);

    // cache hit
    if((len = cache_lookup(url, object)) > 0){;
        rio_writen(clientfd, object, len);
        return end_thread(&clientfd, &serverfd);
    }
    
    // cache miss; continue
    // ignore other methods than GET
    if(strcasecmp(method, "GET"))
        return end_thread(&clientfd, &serverfd);
    // parse URL
    if(strstr(url, "://")){
        // match off leading "http://"
		if(sscanf(url, "%[^:]://%[^/]%s", protocol, hostname, resource) != 3){
            return end_thread(&clientfd, &serverfd);
        }
    }
	else{
		if(sscanf(url, "%[^/]%s", hostname, resource) != 2){
            return end_thread(&clientfd, &serverfd);
        }
    }
    // pull out port number if present
    tmp = strstr(hostname, ":");
	if (tmp) {
		*tmp = '\0';
		strcpy(port, tmp + 1);
	}
	else
		strcpy(port, "80");

    // build fwd req
    // req line
    strcpy(req_fwd, method);
    strcat(req_fwd, " ");
    strcat(req_fwd, resource);
    strcat(req_fwd, " ");
    strcat(req_fwd, "HTTP/1.0\r\n");
    // Host hdr
    strcat(req_fwd, "Host: ");
    strcat(req_fwd, hostname);
    strcat(req_fwd, ":");
    strcat(req_fwd, port);
    strcat(req_fwd, "\r\n");
    // User-Agent, Connection, Proxy-Connection hdr
    strcat(req_fwd, user_agent_hdr);
    strcat(req_fwd, conn_hdr);
    strcat(req_fwd, proxy_conn_hdr);
    // read following hdrs
    while(Rio_readlineb(&rio_client, req, MAXBUF) > 0){
        // end of req
        if(!strcmp(req, "\r\n")){
            strcat(req_fwd, req);
            break;
        }
        // skip already written hdrs
        else if(strstr(req, "Host:") ||
                strstr(req, "User-Agent:") ||
                strstr(req, "Connection:") ||
                strstr(req, "Proxy-Connection:"))
            continue;
        // fwd other hdrs unchanged
        else
            strcat(req_fwd, req);
    }

    // connect and fwd req to server
    if((serverfd = Open_clientfd(hostname, port)) < 0)
        return end_thread(&clientfd, &serverfd);
    rio_writen(serverfd, req_fwd, strlen(req_fwd));

    // receive and fwd from server to client
    Rio_readinitb(&rio_server, serverfd);
    memset(object, 0, MAX_OBJECT_SIZE);
    objectlen = 0;
    while((len = Rio_readnb(&rio_server, resp, MAXBUF)) > 0){
        rio_writen(clientfd, resp, len);
        if(objectlen + len <= MAX_OBJECT_SIZE){
            memcpy(object+objectlen, resp, len);
            objectlen += len;
        }
    }
    if(objectlen <= MAX_OBJECT_SIZE)
        cache_add(url, object, objectlen);
    return end_thread(&clientfd, &serverfd);
}

void *end_thread(int *clientfd, int *serverfd)
{
    // close open fds
    if(*serverfd >= 0)
        Close(*serverfd);
    Close(*clientfd);
    return NULL;
}