/* -----------------------
 * update time: Apr.02 2020
 * author: whqee
 * e-mail: whqee@qq.com
 * -----------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>
#include <sys/syscall.h>

#define STRING_SIZE 1024
#define MAX_LISTEN  128  // n <= 128

#define MULTI_CNAME_SERVICE

static const char response_ok_head[] = "\
HTTP/1.0 200 OK\r\n\
Server: Tiny Web Server\r\n\
Connection: keep-alive\r\n\
Access-Control-Allow-Origin: *\r\n\
Access-Control-Allow-Methods: *\r\n\
";

static int send_file(int srcfd, int filesize, int sock)
{
    if (filesize > 0) {
        fprintf(stdout, "send_file(): sending file...\n"); // dbg msg
        /* map a mem addr to the file, return the mem pointer of the start address */
        void *fp = mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); 
        if (fp == MAP_FAILED) {
            perror("map mem address failed in send_file()\n");
            return -1;
        }
        /* send the file */
        int nleft = filesize;
        ssize_t nwritten;
        char *bufp = fp;
        while (nleft > 0) {
            nwritten = write(sock, bufp, nleft);
            if (nwritten <= 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EBUSY)
                    nwritten = 0;
                else {
                    munmap(fp, filesize);
                    return -1;
                }
            }
            nleft -= nwritten;
            bufp += nwritten; 
        }
        /* end of sending file */
        munmap(fp, filesize);
        return filesize;
    }
    return -1;
}

#ifdef MULTI_CNAME_SERVICE
static char g_host_binding_list[STRING_SIZE] = {0};

static void filter_comment(char *src, int src_size, char *buf, int buf_size)
{
    char *p1 = src , *p2 = buf, *p2max = p2 + buf_size;
    int i, flag_comment = 0;
    while(src_size) {
        if (*p1 == '#')
            flag_comment = 1; // comment after
        if (flag_comment) {
            if (*p1 == '\n')
                flag_comment = 0;
        } else {
            *p2++ = *p1;
            if (p2 == p2max) {
                fprintf(stdout, "buffer is full\n"); //dbg msg
                fprintf(stdout, "buffer:\n%s\n", buf); //dbg msg
                break;
            }
        }
        p1++;
        src_size--;
    }
    return;
}

static int parse_conf_from_file(int fd, char *buf, int size)
{
    /* map a mem addr to the file, return the mem pointer of the start address */
    int filesize = lseek(fd, 0, SEEK_END);
    void *fp = mmap(0, filesize, PROT_READ, MAP_PRIVATE, fd, 0); 
    if (fp == MAP_FAILED) {
        perror("map mem address failed in host_list_init() for file host.conf\n");
        return -1;
    }
    filter_comment(fp, filesize, buf, size);
    munmap(fp, filesize);
}

static void host_list_init()
{
    int fd = open("/etc/tiny_server/host.conf", O_RDONLY, S_IREAD);
    if (fd < 0) {
        perror("/etc/tiny_server/host.conf");
        fd = open("host.conf", O_RDONLY, S_IREAD);
    }
    if (fd < 0) {
        perror("./host.conf");
        return;
    }
    if (parse_conf_from_file(fd, g_host_binding_list, sizeof(g_host_binding_list)) < 0) {
        perror("host_list_init(): failed. run default.\n");
    }
    close(fd);
}

static void parse_host(char *uri, char *host)
{
    fprintf(stdout, "request host:%s\n", host);
    char *s, path[STRING_SIZE];
    s = strstr(g_host_binding_list, host);
    if (s == NULL) {
        sprintf(path, "_site%s", uri);
        sprintf(uri, "%s", path);
        fprintf(stdout, "domain not matching, run default response\n"); // dbg msg
        return;
    }
    sscanf(s, "%*[^ ] %s\n", path);
    strcat(path, uri);
    sprintf(uri, "%s", path);
    fprintf(stdout, "path:%s\n", path); // dbg msg
}
#endif // MULTI_CNAME_SERVICE

static int security_check(char *p)
{
    // char *p = uri;
    /* check string ".." for security */ 
    while(*p++ != '\0') {
        if (*p == '.' && *p == *(p-1)) {
            perror("String \"..\" is forbidden.\n"); // dbg msg
            return -1;
        }
    }
    /* end security check */
    return 0;
}

/* The function assumpts that there's 1024 Byte to store the URI.
 * Dynamic request - return 1
 * Static request - return 0
 * uri error     - return -1
 */
static int parse_http_uri(char *uri, char *filetype)
{
    const char *type[] = {".html",".css",".js",".json",".gif",".png",".jpg",".ico",".webp"};
    const char *http_type_table[] = {"text/html","text/css","application/js","application/json",
        "image/gif","image/png","image/jpeg","image/ico","image/webp","text/plain"};
        
    if (!strcmp(uri, "/")) {
        sprintf(uri, "index.html");
        sprintf(filetype, "text/html");
        return 0;
    }
    fprintf(stdout, "parsing uri<%d>: %s\n", strlen(uri)+1, uri); // dbg msg
    if (security_check(uri) < 0) {
        perror("Unsafe uri request.\n");
        return -1;
    }
#ifdef MULTI_CNAME_SERVICE
    if (uri[strlen(uri) - 1] == '/')
        uri[strlen(uri) - 1] = '\0';
#else
    sprintf(uri,"%s", uri+1);
#endif // MULTI_CNAME_SERVICE
    struct stat buf;
    if (lstat(uri, &buf) < 0)
        return -1;
    if (S_ISDIR(buf.st_mode)) {  // if dir, append "/index.html" to uri
        sprintf(uri, "%s/index.html", uri);
        sprintf(filetype, "text/html");
        return 0;
    } 
    if (!S_ISREG(buf.st_mode)) {
        perror("File does not exist or not allowed to read. Only normal files is readable.");
        return -1;
    }
    char *p = rindex(uri, '.');
    if (!p) {
        fprintf(stdout, "file type doesn't exist, return file as text/plain.\n");
        strcpy(filetype, "text/plain");
        return 0;
    }
    for (size_t i = 0; i < 9; i++) {
        if (strstr(p, type[i])) {
            strcpy(filetype, http_type_table[i]);
            break;
        }
        if (i == 9) strcpy(filetype, "text/plain");
    }
    printf("filetype: %s\n", filetype); // dbg msg
    return 0;
}

static int dynamic_uri(char *uri)
{
    return 0;
}

static int static_server(int sock, char *uri, char *filetype)
{
    fprintf(stdout, "static request:%s\n", uri); // dbg msg
    int ret, fd, filesize;
    char response_head[STRING_SIZE];
    fd = open(uri, O_RDONLY, S_IREAD);
    if (fd < 0){
        perror("File does not exist or permmition diny.\n"); // dbg msg
        return fd;
    }
    else {
        filesize = lseek(fd, 0, SEEK_END);
        bzero(response_head, sizeof(response_head));
        sprintf(response_head, response_ok_head);
        sprintf(response_head, "%sContent-type: %s;charset=UTF-8\r\n", response_head, filetype);
        sprintf(response_head, "%sContent-length: %d\r\n\r\n", response_head, filesize);
        if (ret = write(sock, response_head, strlen(response_head)) < 0) {
            close(fd);
            return ret;
        }
        ret = send_file(fd, filesize, sock);
        close(fd);
        fprintf(stdout, "my response:\n%s\n", response_head); // dbg msg
        return ret;
    }
}

static int handle_http_request(int sock, char * msg)
{
    fprintf(stdout, "parsing http request:\n");
    char method[STRING_SIZE], uri[STRING_SIZE], host[128], 
        connection[STRING_SIZE], response_head[STRING_SIZE], filetype[STRING_SIZE];

    sscanf(msg, "%s %s HTTP%*[^H]Host: %s\n", method, uri, host);
    // printf("method:%s\nuri:%s\nhost:%s\n", method, uri, host); // dbg msg
    if (!strcmp(method, "GET") || !strcmp(method, "OPTIONS")|| !strcmp(method, "HEAD")|| !strcmp(method, "PUT")) {  /* only support GET */
#ifdef MULTI_CNAME_SERVICE
        parse_host(uri, host);
#endif // MULTI_CNAME_SERVICE
        if ( parse_http_uri(uri, filetype) ) { /* dynamic request */
            return -1;
        }
        else { /* static request */
            return static_server(sock, uri, filetype);
        }
    }
    sprintf(response_head, "HTTP/1.1 405 Method Not Allowed\r\n");
    sprintf(response_head, "%sContent-length: 0\r\n", response_head);
    sprintf(response_head, "%sContent-type: text/html;charset=UTF-8\r\n\r\n", response_head);
    sprintf(response_head, "%s405 Method Not Allowed\r\n\r\n", response_head);
    fprintf(stdout, "\nresponse:\n%s\n", response_head);
    return send(sock, response_head, strlen(response_head), 0);
}

static int handle_message(int fd, char * msg)
{
    fprintf(stdout, "handling message: ");
    char method[STRING_SIZE], uri[STRING_SIZE], version[STRING_SIZE], buf[STRING_SIZE];
    sscanf(msg, "%s %s %s", method, uri, version);
    printf("%s %s %s...\n", method, uri, version);
    if (!strcmp(version, "HTTP/1.1"))
        return handle_http_request(fd, msg);
    return -1;
}

static void * thread_handling_new_client(void * arg)
{
    fprintf(stdout, "New thread started, PID %d, TID %d\n", getpid(),
        (pid_t)syscall(SYS_gettid));
    char buf[STRING_SIZE];
    
    int client_sockfd = *(int*)arg;
    while (1)
    {
        memset(&buf, 0, sizeof(buf));
        int nbyte = recv(client_sockfd, buf, sizeof(buf)-1, 0);
        if (nbyte == -1) {
            perror("read error.\n");
            close(client_sockfd);
            break;
        }
        else if (nbyte == 0) {
            fprintf(stderr, "client closed, fd=%d\n", client_sockfd);
            close(client_sockfd);
            break;
        }
        else {
            fprintf(stdout, "fd=%d recieved %d bytes data:\n%s\n", client_sockfd, nbyte, buf);

            nbyte = handle_message(client_sockfd, buf);
            if (nbyte == -1) {
                fprintf(stderr, "fd=%d send error.\n", client_sockfd);
                close(client_sockfd);
                break;
            }
        }
    }
    fprintf(stdout, "thread exit, PID %d, TID %d\n", getpid(),
        (pid_t)syscall(SYS_gettid));
    pthread_exit(PTHREAD_CANCELED);
}

int main(int argc, char const *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    printf("argv[1]: %s\n", argv[1]);
    // int port = atoi(argv[1]);
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        fprintf(stderr, "port %s busy.\n", argv[1]);
        exit(1);
    }
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(atoi(argv[1]));

    int reuse = 1, err;
    err = setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (err == -1) {
        fprintf(stderr, "bind port %s failed.\n", argv[1]);
        exit(1);
    }
    err = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err == -1) {
        fprintf(stderr, "bind port %s failed.\n", argv[1]);
        exit(1);
    }
    err = listen(server_socket, MAX_LISTEN); // set max client (n <= 128) ;
    if (err) {
        fprintf(stderr, "listen port %s failed.\n", argv[1]);
        exit(1);
    }
    fprintf(stdout, "Waiting for client...\n");
#ifdef MULTI_CNAME_SERVICE
    host_list_init();
    fprintf(stdout, "g_host_binding_list[]:\n%s\n", g_host_binding_list); // dbg msg
#endif // MULTI_CNAME_SERVICE
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd < 0) 
            fprintf(stderr, "accept an error.\n");
        else
            fprintf(stdout, "accept a new client: %s:%d\n", 
                inet_ntoa(client_addr.sin_addr),client_addr.sin_port);
        pthread_t t;
        err = pthread_create(&t, NULL, thread_handling_new_client, &client_fd);
        if (err) perror("pthread create failed.\n");
        pthread_detach(t);
    }
    close(server_socket);
    return 0;
}

