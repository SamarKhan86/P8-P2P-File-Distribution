#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>

int main() {
    int sockfd, k;
    char buf[100];
    struct sockaddr_in sa;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    sa.sin_port = htons(6034);

    if (connect(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    strcpy(buf, "HANDSHAKE | Peer_ID: P_CLIENT");

    if (send(sockfd, buf, strlen(buf), 0) < 0) {
        perror("send");
        close(sockfd);
        return 1;
    }

    printf("Sent: %s\n", buf);

    k = recv(sockfd, buf, sizeof(buf) - 1, 0);

    if (k <= 0) {
        printf("No handshake response received\n");
        close(sockfd);
        return 1;
    }

    buf[k] = '\0';
    printf("Received: %s\n", buf);

    close(sockfd);

    return 0;
}

