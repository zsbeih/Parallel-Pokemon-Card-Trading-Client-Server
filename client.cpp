#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
using namespace std;

#define SERVER_PORT 7610
#define MAX_LINE 256


int main(int argc, char * argv[]) {
    FILE *fp;
    struct hostent *hp;
    struct sockaddr_in sin;
    char *host;
    char buf[MAX_LINE];
    int s;
    int len;

    if (argc == 2) {
        host = argv[1];
    } else {
        fprintf(stderr, "usage: client host\n");
        exit(1);
    }

    /* Translate host name into peer's IP address */
    hp = gethostbyname(host);
    if (!hp) {
        fprintf(stderr, "Client: unknown host: %s\n", host);
        exit(1);
    }

    /* Build address data structure */
    bzero((char *)&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    bcopy(hp->h_addr, (char *)&sin.sin_addr, hp->h_length);
    sin.sin_port = htons(SERVER_PORT);

    /* Active open */
    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Client: socket");
        exit(1);
    }

    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("Client: connect");
        close(s);
        exit(1);
    }

    printf("Enter commands (BUY, SELL, LIST, BALANCE, LOGIN, LOGOUT, DEPOSIT, WHO, LOOKUP, SHUTDOWN, QUIT):\n");

    fd_set readfds;
    int maxfd = s + 1;  

    while (1) {
        FD_ZERO(&readfds);        
        FD_SET(0, &readfds);      
        FD_SET(s, &readfds);    

        if (select(maxfd, &readfds, NULL, NULL, NULL) < 0) {
            perror("select");
            exit(1);
        }

        if (FD_ISSET(0, &readfds)) {
            if (!fgets(buf, sizeof(buf), stdin)) {
                break;  // EOF on stdin
            }
            buf[strcspn(buf, "\n")] = 0;

            // Handle QUIT command
            if (strcmp(buf, "QUIT") == 0) {
                send(s, buf, strlen(buf), 0);
                len = recv(s, buf, sizeof(buf), 0);
                if (len > 0) {
                    buf[len] = '\0';
                    printf("%s\n", buf);
                }
                break;
            }


            // Process commands
            if (strcmp(buf, "BUY") == 0 ||
                strcmp(buf, "SELL") == 0 ||
                strcmp(buf, "LIST") == 0 ||
                strcmp(buf, "BALANCE") == 0 ||
                strcmp(buf, "LOGIN") == 0 ||
                strcmp(buf, "LOGOUT") == 0 ||
                strcmp(buf, "DEPOSIT") == 0 ||
                strcmp(buf, "WHO") == 0 ||
                strcmp(buf, "LOOKUP") == 0 ||
                strcmp(buf, "SHUTDOWN") == 0) {
                
                send(s, buf, strlen(buf), 0);
            } else {
                printf("Unknown command. Try BUY, SELL, LIST, BALANCE, LOGIN, LOGOUT, DEPOSIT, WHO, LOOKUP, SHUTDOWN, or QUIT.\n");
            }
        }

        // Check for input from socket
        if (FD_ISSET(s, &readfds)) {
            len = recv(s, buf, sizeof(buf), 0);
            if (len <= 0) {
                printf("Server disconnected\n");
                break;
            }
            buf[len] = '\0';
            printf("%s\n", buf);
        }
    }

    close(s);
    return 0;
}