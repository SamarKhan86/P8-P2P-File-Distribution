#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>

int main() {
    int sockfd, fd1, length, k;
    char buf[100];
    struct sockaddr_in sa, ca;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    sa.sin_port = htons(6034);

    if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    printf("Peer server started on port 6034\n");

    if (listen(sockfd, 5) < 0) {
        perror("listen");
        close(sockfd);
        return 1;
    }

    length = sizeof(ca);
    fd1 = accept(sockfd, (struct sockaddr *)&ca,
                 (socklen_t *)&length);

    if (fd1 < 0) {
        perror("accept");
        close(sockfd);
        return 1;
    }

    k = recv(fd1, buf, sizeof(buf) - 1, 0);

    if (k <= 0) {
        printf("No handshake received\n");
        close(fd1);
        close(sockfd);
        return 1;
    }

    buf[k] = '\0';
    printf("Received: %s\n", buf);

    strcpy(buf, "HANDSHAKE | Peer_ID: P_SERVER");

    if (send(fd1, buf, strlen(buf), 0) < 0) {
        perror("send");
    } else {
        printf("Sent: %s\n", buf);
    }

    close(fd1);
    close(sockfd);

    return 0;
}

