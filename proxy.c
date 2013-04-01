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
sem_t log_mutex;

/*
 * Function prototypes
 */
int parse_uri(char * uri, char * target_addr, char * path, int * port);
void format_log_entry(char * logstring, struct sockaddr_in * sockaddr, char * uri, int size);
void log_to_file(char * log_entry);


ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n)
{
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0)
    {
        printf("ERROR: Rio_readnb failed!\n");
        return rc;
    }
    return rc;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
    {
       printf("ERROR: Rio_readlineb failed!\n"); 
       return rc;
    }
    return rc;
}

void Rio_writen_w(int fd, void *usrbuf, size_t n)
{
    if (rio_writen(fd, usrbuf, n) != n)
    {
        printf("ERROR: Rio_writen failed!\n"); 
        return;
    }
}


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
    {
        printf("ERROR: open_clientfd failed!\n");
        return -2; /* Check h_errno for cause of error */
    }
    memcpy(&priv_hp,&hp,sizeof(hp));
    V(&mutex);
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)priv_hp->h_addr_list[0],
          (char *)&serveraddr.sin_addr.s_addr, priv_hp->h_length);
    serveraddr.sin_port = htons(port);
    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
        {
            printf("ERROR: open_clientfd failed!\n");
            return -1;
        }
    return clientfd;
    }

void handle_request(int* fd) {
    int connfd = *fd;
    free(fd);
    Pthread_detach(pthread_self());
    size_t n;
    rio_t rio;
    int port;
    char path[MAXLINE];
    char address[MAXLINE];
    char log_entry[MAXLINE];
    char buf[MAXLINE];
    char req_header[MAXLINE];
    char target_addr[MAXLINE];
    strcpy(req_header,"");
    rio_readinitb( & rio, connfd);
    while ( (n = Rio_readlineb_w( & rio, buf, MAXLINE)) != 0) {
        strcat(req_header, buf);
        if( strstr(buf, "HTTP/") != NULL ) {
            if( (strstr(buf, "GET") == NULL) ) //This server only handles GET requests
                break;
            sscanf(buf, "%*s %s %*s", address); 
            parse_uri(address, target_addr, path, &port);
            }
        else if( strcmp(buf, "\r\n") == 0) {
            rio_t remote_rio;
            int remote_fd;
            int content_len = -1;
            int read_len = 0;
            int chunked = 0;
            int total_size = 0;
            if( (remote_fd = open_clientfd_ts(target_addr, port) ) < 0)
                break;
            rio_readinitb(&remote_rio, remote_fd);
            Rio_writen_w(remote_fd, req_header, strlen(req_header));
            do {
                Rio_readlineb_w(&remote_rio, buf, MAXLINE);
                Rio_writen_w(connfd, buf, strlen(buf));
                sscanf(buf, "Content-Length: %d", &content_len);
                if( strstr(buf,"chunked"))
                    chunked  = 1;
                }
            while( strcmp(buf, "\r\n") ) ;
            if(chunked) {
                while ( ( (read_len = Rio_readlineb_w(&remote_rio, buf, MAXLINE)) > 0)
                        && strcmp(buf,"0\r\n")) {
                    Rio_writen_w(connfd, buf, read_len);
                    total_size += read_len;
                    }
                }
            else { //not chunked
                total_size = content_len;
                while (content_len > MAXLINE) {
                    read_len = Rio_readnb_w(&remote_rio, buf, MAXLINE);
                    Rio_writen_w(connfd, buf, read_len);
                    content_len -= MAXLINE;
                    }
                if (content_len > 0) { //remaining content
                    read_len = Rio_readnb_w(&remote_rio, buf, content_len);
                    Rio_writen_w( connfd, buf, content_len );
                    }
                }
            close(remote_fd);
            format_log_entry(log_entry, (struct sockaddr_in *) target_addr, path, total_size);
            break;
            }
        }
	P(&log_mutex);
    	log_to_file(log_entry);
	V(&log_mutex);
	close(connfd);
    }

int main(int argc, char ** argv) {
    Signal(SIGPIPE, SIG_IGN);
    sem_init(&mutex,0,1);
    int listenfd;
    int* connfd;
    int port;
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
        connfd = malloc( sizeof(int));
        clientlen = sizeof(clientaddr);
        *connfd = accept(listenfd, (SA * ) & clientaddr,  &clientlen);
        /* Determine the domain name and IP address of the client */
        hp = gethostbyaddr((const char * ) & clientaddr.sin_addr.s_addr,
                           sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        haddrp = inet_ntoa(clientaddr.sin_addr);
        //printf("server connected to %s (%s)\n", hp->h_name, haddrp);
        //handle_request(connfd);
        Pthread_create(&tid, NULL, (void *)handle_request, (void *)connfd);
        }
    printf("done!\n");
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

void log_to_file(char * log_entry) {
    FILE *ofp;
    char *mode = "a";
    char outputFilename[] = "proxy.log";

    ofp = fopen(outputFilename, mode);
    if (ofp == NULL) {
        fprintf(stderr, "Can't open output file %s!\n", outputFilename);
        exit(1);
        }
    fprintf(ofp, "%s\n", log_entry);
    fclose(ofp);
    }


/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char * logstring, struct sockaddr_in * sockaddr, char * uri, int size) {
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
