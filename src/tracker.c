#include <stdio.h>
#include <string.h>

#define MAX_PEERS 10

struct Peer
{
    char peer_id[20];
    char ip[50];
    int port;
};

struct Peer peers[MAX_PEERS];
int peer_count = 0;

void register_peer()
{
    if (peer_count >= MAX_PEERS)
    {
        printf("Tracker is full.\n");
        return;
    }

    printf("Enter Peer ID: ");
    scanf("%19s", peers[peer_count].peer_id);

    printf("Enter IP Address: ");
    scanf("%49s", peers[peer_count].ip);

    printf("Enter Port: ");
    scanf("%d", &peers[peer_count].port);

    peer_count++;

    printf("Peer registered successfully.\n");
}

void show_peers()
{
    if (peer_count == 0)
    {
        printf("No peers available.\n");
        return;
    }

    printf("\nAvailable Peers:\n");

    for (int i = 0; i < peer_count; i++)
    {
        printf("----------------------\n");
        printf("Peer ID: %s\n", peers[i].peer_id);
        printf("IP Address: %s\n", peers[i].ip);
        printf("Port: %d\n", peers[i].port);
    }
}

void remove_peer()
{
    char id[20];
    int found = 0;

    printf("Enter Peer ID to remove: ");
    scanf("%19s", id);

    for (int i = 0; i < peer_count; i++)
    {
        if (strcmp(peers[i].peer_id, id) == 0)
        {
            for (int j = i; j < peer_count - 1; j++)
            {
                peers[j] = peers[j + 1];
            }

            peer_count--;
            found = 1;

            printf("Peer removed successfully.\n");
            break;
        }
    }

    if (!found)
    {
        printf("Peer not found.\n");
    }
}

int main()
{
    int choice;

    while (1)
    {
        printf("\n===== P2P TRACKER =====\n");
        printf("1. Register Peer\n");
        printf("2. Show Peers\n");
        printf("3. Remove Peer\n");
        printf("4. Exit\n");
        printf("Enter choice: ");

        scanf("%d", &choice);

        switch (choice)
        {
            case 1:
                register_peer();
                break;

            case 2:
                show_peers();
                break;

            case 3:
                remove_peer();
                break;

            case 4:
                printf("Tracker stopped.\n");
                return 0;

            default:
                printf("Invalid choice.\n");
        }
    }

    return 0;
}