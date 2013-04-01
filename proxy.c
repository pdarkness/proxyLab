/*(
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Haukur Rosinkranz haukurr11@ru.is
 *     Knutur Oli Magnusson knutur11@ru.is
 *
 * IMPORTANT: This program utilizes multiple threading and sockets
 * to listen as a server for multiple incoming clients.
 * It parses HTTP requests from them,connects to a
 * requested remote server and forwards the clients'
 * requests to the server. It also forwards the response
 * from the server back to the client. It also logs
 * all successful requests from clients and uses semaphores
 * to prevent race conditions between threads.
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


//Wrapper for rio_readnb that doesn't exit the program
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n) {
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0) {
        printf("ERROR: Rio_readnb failed!\n");
        return rc;
        }
    return rc;
    }

//Wrapper for rio_readlineb that doesn't exit the program
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) {
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
        printf("ERROR: Rio_readlineb failed!\n");
        return rc;
        }
    return rc;
    }

//Wrapper for rio_writen that doesn't exit the program
void Rio_writen_w(int fd, void *usrbuf, size_t n) {
    if (rio_writen(fd, usrbuf, n) != n) {
        printf("ERROR: Rio_writen failed!\n");
        return;
        }
    }

//Thread safe version of open_clientfd that also gives 
//us the IP address of the remote server
int open_clientfd_ts(char *hostname, int port, struct sockaddr_in *ip) {
    int clientfd;
    struct hostent *hp;
    struct hostent *priv_hp;
    struct sockaddr_in serveraddr;
    int error = 0;
    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1; /* Check errno for cause of error */
    P(&mutex);
    /* Fill in the server.s IP address and port */
    if ((hp = gethostbyname(hostname)) == NULL) {
        printf("ERROR: open_clientfd, hostname not found: %s!\n", hostname);
        error = -2; /* Check h_errno for cause of error */
        }
    memcpy(&priv_hp, &hp, sizeof(hp));
    V(&mutex);
    if(error != 0)
        return error;
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)priv_hp->h_addr_list[0],
          (char *)&serveraddr.sin_addr.s_addr, priv_hp->h_length);
    serveraddr.sin_port = htons(port);
    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0) {
        printf("ERROR: open_clientfd, could not connect to server: %s!\n", hostname);
        return -1;
        }
    //Gives us the IP address so we can put it in the logfile
    *ip = serveraddr;
    return clientfd;
    }

//Handles all HTTP GET requests from a file descriptor,
//forwards them to requested remote server
//and sends back the response from the server to the client
void handle_request(int* fd) {
    int connfd = *fd;
    Pthread_detach(pthread_self());
    Free(fd);
    size_t n;
    rio_t rio;
    int port;
    char buf[MAXLINE];
    char req_header[MAXLINE];
    char target_addr[MAXLINE];
    char url[MAXLINE];
    char log_entry[MAXLINE];
    struct sockaddr_in ip;
    strcpy(req_header, "");
    rio_readinitb( & rio, connfd);
    while ( (n = Rio_readlineb_w( & rio, buf, MAXLINE)) != 0) {
        if( (strstr(buf, " HTTP/1.1") != NULL) || (strstr(buf, " HTTP/1.0") != NULL)  ) {
            if( (strstr(buf, "GET ") == NULL) ) { //This server only handles GET requests
                Close(connfd);
                return;
                }
            char* req_type = malloc( sizeof(char) * MAXLINE);
            char* http_type = malloc( sizeof(char) * MAXLINE);
            char* path = malloc( sizeof(int) * MAXLINE );
            if( sscanf(buf, "%s %s %s", req_type, url, http_type) != 3) {
                printf("ERROR: malformed request: %s!\n", buf);
                Close(connfd);
                return;
                }
            parse_uri(url, target_addr, path, &port);
            //Construct a new request without the remote URL in the path
            //some servers like visir.is need this
            strcat(req_type, " /");
            strcat(path, " ");
            strcat(http_type, "\n");
            //Add the new request to the header
            strcat(req_header, req_type);
            strcat(req_header, path);
            strcat(req_header, http_type);
            Free(req_type);
            Free(http_type);
            Free(path);
            }
        else {
            strcat(req_header, buf);
            if( strcmp(buf, "\r\n") == 0) {
                rio_t remote_rio;
                int remote_fd;
                int content_len = -1; //Content-Length(if avail)
                int chunked = 0; //is the body in chunks?
                int read_len = 0;
                int total_size = 0;

                if( (remote_fd = open_clientfd_ts(target_addr, port, &ip) ) < 0) {
                    if(remote_fd == -2)
                        strcpy(buf, "ERROR: Host not found: ");
                    else
                        strcpy(buf, "ERROR: Could not connect to server: ");
                    strcat(buf, target_addr);
                    strcat(buf, "\n");
                    Rio_writen_w(connfd, buf, strlen(buf));
                    Close(connfd);
                    return;
                    }
                Rio_readinitb(&remote_rio, remote_fd);
                Rio_writen_w(remote_fd, req_header, strlen(req_header));

                //Response header
                do {
                    Rio_readlineb_w(&remote_rio, buf, MAXLINE);
                    Rio_writen_w(connfd, buf, strlen(buf));
                    sscanf(buf, "Content-Length: %d", &content_len);
                    if( strstr(buf, "chunked"))
                        chunked  = 1;
                    } while( strcmp(buf, "\r\n") ) ;

                if(chunked) {
                    while ( ( (read_len = Rio_readlineb_w(&remote_rio, buf, MAXLINE)) > 0)
                            && strcmp(buf, "0\r\n")) {
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
                Close(remote_fd);
                format_log_entry(log_entry, &ip, url, total_size);
                break; //All done
                }
            }
        }
    //Request completed: Log entry and Disconnect
    P(&log_mutex);
    log_to_file(log_entry);
    V(&log_mutex);
    Close(connfd);
    }


int main(int argc, char ** argv) {
    Signal(SIGPIPE, SIG_IGN); //IGNORE SIGPIPE
    sem_init(&mutex, 0, 1);
    sem_init(&log_mutex, 0, 1);
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
        printf("server connected to %s (%s)\n", hp->h_name, haddrp);
        Pthread_create(&tid, NULL, (void *)handle_request, (void *)connfd);
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
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri, size);
    }
