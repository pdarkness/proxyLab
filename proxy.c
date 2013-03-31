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
void log_to_file(char * log_entry);

int open_clientfd_ts(char *hostname, int port) {
    int clientfd;
    struct hostent *hp;
    struct hostent *priv_hp;
    struct sockaddr_in serveraddr;
    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1; /* Check errno for cause of error */
    //P(&mutex);
    /* Fill in the server.s IP address and port */
    if ((hp = gethostbyname(hostname)) == NULL)
        return -2; /* Check h_errno for cause of error */
    //memcpy(&priv_hp,&hp,sizeof(hp));
    //V(&mutex);
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)hp->h_addr_list[0],
          (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
    serveraddr.sin_port = htons(port);
    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
        return -1;
    return clientfd;
    }

void echo(int* arg) {
    int connfd = *arg; 
    free(arg);
    size_t n;
    char* buf = malloc( sizeof(char)*MAXLINE);
    rio_t rio;
    Rio_readinitb( & rio, connfd);
    char* req_header = malloc(sizeof(char) * MAXLINE);
    struct sockaddr_in * target_addr  = malloc(sizeof(char) * MAXLINE);
    int port;
    int hostfound = 0;    
    char path[MAXLINE];
    char address[MAXLINE];
    char log_entry[MAXLINE];

    while ( (n = Rio_readlineb( & rio, buf, MAXLINE)) != 0) {
        strcat(req_header, buf);
        if( strstr(buf, "HTTP/") != NULL ) {
            if( strstr(buf,"GET") == NULL ) {
                printf("FAIL\n");
                break;
            }
            printf(buf);
            sscanf(buf, "%*s %s %*s", address);
            parse_uri(address, target_addr, path, &port);
            }
        else if( strcmp(buf, "\r\n") == 0) {
            rio_t remote_server;
            int clientfd;
            if( (clientfd = open_clientfd_ts(target_addr, port) ) < 0) {
                printf("FAIL\n");
                break;
            }
            Rio_readinitb(&remote_server, clientfd);
            Rio_writen(clientfd, req_header, strlen(req_header));
            int* content_len = malloc( sizeof(int) );
            *content_len = 0;
            do {
                Rio_readlineb(&remote_server, buf, MAXLINE);
                Rio_writen(connfd, buf, strlen(buf));
                sscanf(buf, "Content-Length: %d", content_len);
                printf(buf);
                } while( strcmp(buf, "\r\n") ) ;
            int* read_len = malloc( sizeof(int) );
            if (*content_len > 0) { //not chunked
                while (*content_len > MAXLINE) {
                    *read_len = Rio_readnb(&remote_server, buf, MAXLINE);
                    Rio_writen(connfd, buf, *read_len);
                    *content_len -= MAXLINE;
                    }
                if (*content_len > 0) { //remainder
                    *read_len = Rio_readnb(&remote_server, buf, *content_len);
                    Rio_writen( connfd, buf, *content_len );
                    }
                }
            else { //chunked
                while ((*read_len = Rio_readlineb(&remote_server, buf, MAXLINE)) > 0)
                        {
                        Rio_writen(connfd, buf, *read_len);
                        if( !strcmp(buf,"0\r\n")) break;
                        }
                }
            Close(clientfd);
            format_log_entry(log_entry, target_addr, path, *content_len );
            log_to_file(log_entry);
            break;
            }
        }
        Close(connfd);
        Free(buf);
        //Free(port);
        //Free(req_header);
    }

int main(int argc, char ** argv) {
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
        *connfd = Accept(listenfd, (SA * ) & clientaddr,  &clientlen);
        /* Determine the domain name and IP address of the client */
        hp = Gethostbyaddr((const char * ) & clientaddr.sin_addr.s_addr,
                           sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        haddrp = inet_ntoa(clientaddr.sin_addr);
        //printf("server connected to %s (%s)\n", hp->h_name, haddrp);
        echo(connfd);
	    //Pthread_create(&tid, NULL, (void *)echo, (void *)connfd);
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

void log_to_file(char * log_entry)
{
        FILE *ofp;
        char *mode = "a";
        char outputFilename[] = "proxy.log";

        ofp = fopen(outputFilename, mode);
        if (ofp == NULL)
        {
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
