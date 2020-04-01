/* time  : 2020 02
 * author: whqee
 * e-mail: whqee@qq.com
*/
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

#define SIZE 1024

static int send_file(int srcfd, int filesize, int sock)
{
    if (filesize > 0) {
        fprintf(stdout, "send_file(): sending file...\n"); // debug msg
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

/* The function assumpts that there's 1024 Byte to store the URI.
 * Dynamic request - return 1
 * Static request - return 0
 * uri error     - return -1
 */
static int parse_http_uri(char *uri, char *filetype)
{
    const char *type[] = {".html",".css",".js",".json",".gif",".png",".jpg",".ico",".webp"};
    const char *http_type_table[] = {"text/html","text/css","application/js","application/json","image/gif","image/png",
            "image/jpeg","image/ico","image/webp","text/plain"};

    fprintf(stdout, "parsing uri[%d]: %s\n", strlen(uri)+1, uri);

    if (!strcmp(uri, "/")) {
        sprintf(uri, "index.html");
        sprintf(filetype, "text/html");
        return 0;
    }
    char *p = uri;
    /* check string ".." for system security */ 
    while(*p++ != '\0') {
        if (*p == '.') {
            if (*p == *(p-1)) {
                perror("String \"..\" is forbidden.\n"); // dbg msg
                return -1;
            }
        }
        // p++;
    }
    /* end security check */

    printf("uri:%s\n", uri);
    sprintf(uri,"%s", uri+1);
    printf("uri:%s\n", uri);

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

    p = rindex(uri, '.');
    if (!p) {
        fprintf(stdout, "file type doesn't exist, return file as text/plain.\n");
        strcpy(filetype, "text/plain");
        return 0;
    }
    // strcpy(filetype, p);
    
    for (size_t i = 0; i < 9; i++) {
        if (strstr(p, type[i])) {
            strcpy(filetype, http_type_table[i]);
            break;
        }
        if (i == 9) strcpy(filetype, "text/plain");
    }
    printf("filetype: %s\n", filetype); // debug msg

    // char *p = strchr(uri, '?');

    return 0;
}

static int dynamic_uri(char *uri)
{
    return 0;
}

static int handle_http_request(int sock, char * msg)
{
    fprintf(stdout, "parsing http request:\n");
    char method[SIZE], uri[SIZE], connection[SIZE], buf[SIZE], filetype[SIZE];
    const char response_ok_head[] = "HTTP/1.0 200 OK\r\nServer: Tiny Web Server\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: *\r\n";

    
    int filesize, fd, ret;

    sscanf(msg, "%s %s HTTP", method, uri);
    if (!strcmp(method, "GET") || !strcmp(method, "OPTIONS")|| !strcmp(method, "HEAD")|| !strcmp(method, "PUT")) {  /* only support GET */
        if ( parse_http_uri(uri, filetype) ) { /* dynamic request */
            return -1;
        }
        else { /* static request */
            fprintf(stdout, "file path=%s\n", uri);
            fd = open(uri, O_RDONLY, S_IREAD);
            if (fd < 0){
                perror("File does not exist or permmition diny.\n"); // debug msg
                return fd;
            }
            else {
                filesize = lseek(fd, 0, SEEK_END);
                bzero(buf, sizeof(buf));
                sprintf(buf, response_ok_head);
                sprintf(buf, "%sContent-type: %s;charset=UTF-8\r\n", buf, filetype);
                sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, filesize);
                if (ret = write(sock, buf, strlen(buf)) < 0) {
                    close(fd);
                    return ret;
                }
                ret = send_file(fd, filesize, sock);
                close(fd);
                fprintf(stdout, "my response:\n%s\n", buf); // debug msg
                return ret;
            }
        }
    }
    sprintf(buf, "HTTP/1.1 405 Method Not Allowed\r\n");
    sprintf(buf, "%sContent-length: 0\r\n", buf);
    sprintf(buf, "%sContent-type: text/html;charset=UTF-8\r\n\r\n", buf);
    sprintf(buf, "%s405 Method Not Allowed\r\n\r\n", buf);
    fprintf(stdout, "\nresponse:\n%s\n", buf);
    return send(sock, buf, strlen(buf), 0);
}

static int handle_message(int fd, char * msg)
{
    fprintf(stdout, "handling message: ");
    char method[SIZE], uri[SIZE], version[SIZE], buf[SIZE];
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
    
    char buf[SIZE];
    int client_sockfd = (int)arg;
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
        fprintf(stderr, "socket port %s failed.\n", argv[1]);
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

    err = listen(server_socket, 5); // set max client n=5 (n <= 128) ;
    if (err) {
        fprintf(stderr, "listen port %s failed.\n", argv[1]);
        exit(1);
    }

    fprintf(stdout, "Waiting for client...\n");

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
        err = pthread_create(&t, NULL, thread_handling_new_client, (void*)client_fd);
        if (err) perror("pthread create failed.\n");
        
        pthread_detach(t);


    }
    close(server_socket);
    return 0;
}

