/*(
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Haukur Rosinkranz haukurr11@ru.is
 *     Knutur Oli Magnusson knutur11@ru.is
 *
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */

# include "csapp.h"
sem_t mutex;
/*
 * Function prototypes
 */
int parse_uri(char * uri, char * target_addr, char * path, int * port);
void format_log_entry(char * logstring, struct sockaddr_in * sockaddr, char * uri, int size);

int open_clientfd_ts(char *hostname, int port) {
    int clientfd;
    struct hostent *hp;
    struct hostent *priv_hp;
    struct sockaddr_in serveraddr;
    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1; /* Check errno for cause of error */
    P(&mutex);
    /* Fill in the server.s IP address and port */
    if ((hp = gethostbyname(hostname)) == NULL)
        return -2; /* Check h_errno for cause of error */
    memcpy(&priv_hp,&hp,sizeof(hp));
    V(&mutex);
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)priv_hp->h_addr_list[0],
          (char *)&serveraddr.sin_addr.s_addr, priv_hp->h_length);
    serveraddr.sin_port = htons(port);
    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
        return -1;
    return clientfd;
    }

void echo(int connfd) {
    size_t n;
    char buf[MAXLINE];
    rio_t rio;
    Rio_readinitb( & rio, connfd);
    char req_header[MAXLINE];
    char target_addr[MAXLINE];
    int port;
    while ((n = Rio_readlineb( & rio, buf, MAXLINE)) != 0) {
        strcat(req_header, buf);
        sscanf(buf, "Host: %s", target_addr);
        if( strstr(buf, "HTTP/") != NULL) {
            printf(buf);
            char address[MAXLINE];
            sscanf(buf, "%*s %s %*s", address);
            char path[MAXLINE];
            parse_uri(address, target_addr, path, &port);
            }
        else if( strcmp(buf, "\r\n") == 0) {
            rio_t remote_server;
            int clientfd = open_clientfd_ts(target_addr, port);
            rio_readinitb(&remote_server, clientfd);
            rio_writen(clientfd, req_header, strlen(req_header));
            int content_len = 0;
            do {
                rio_readlineb(&remote_server, buf, MAXLINE);
                Rio_writen(connfd, buf, strlen(buf));
                sscanf(buf, "Content-Length: %d", &content_len);
                }
            while( strcmp(buf, "\r\n") ) ;
            int read_len;
            if (content_len > 0) { //not chunked
                while (content_len > MAXLINE) {
                    read_len = rio_readnb(&remote_server, buf, MAXLINE);
                    rio_writen(connfd, buf, read_len);
                    content_len -= MAXLINE;
                    }
                if (content_len > 0) { //remainder
                    read_len = rio_readnb(&remote_server, buf, content_len);
                    rio_writen( connfd, buf, content_len );
                    }
                }
            else { //chunked
                while ((read_len = Rio_readnb(&remote_server, buf, MAXLINE)) > 0)
                    rio_writen(connfd, buf, read_len);
                }
            close(clientfd);
            }
        }
        close(connfd);
    }

int main(int argc, char ** argv) {
    sem_init(&mutex,0,1);
    int listenfd,
        connfd,
        port;
    unsigned int  clientlen;
    struct sockaddr_in clientaddr;
    struct hostent * hp;
    char * haddrp;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
        }
    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA * ) & clientaddr,  &clientlen);
        /* Determine the domain name and IP address of the client */
        hp = Gethostbyaddr((const char * ) & clientaddr.sin_addr.s_addr,
                           sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        haddrp = inet_ntoa(clientaddr.sin_addr);
        //printf("server connected to %s (%s)\n", hp->h_name, haddrp);
	Pthread_create(&tid, NULL, (void *)echo, (void *)connfd);
        //Close(connfd);
        }
    exit(0);
    }

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char * uri, char * hostname, char * pathname, int * port) {
    char * hostbegin;
    char * hostend;
    char * pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
        }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    * port = 80;
    /* default */
    if ( * hostend == ':')
        * port = atoi(hostend + 1);

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
        }
    else {
        pathbegin++;
        strcpy(pathname, pathbegin);
        }

    return 0;
    }

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char * logstring, struct sockaddr_in * sockaddr,
                      char * uri, int size) {
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a,
             b,
             c,
             d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime( & now));

    /*
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s", time_str, a, b, c, d, uri);
    }
