
#include <stdio.h>
/*#include <sys/socket.h>
#include <arpa/inet.h>*/
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHARBUFFER 40
#define SERVERLINES 10
#define RELAYLINES 2

void ReadServers(FILE *fileServers, char serverDomain[][CHARBUFFER], char serverAlias[][CHARBUFFER]) {
    char line[CHARBUFFER] = {3};
    int i =0;
    
    while (fgets(line, CHARBUFFER, fileServers) != NULL) {
        strncpy (serverDomain[i], strtok(line, ", "), CHARBUFFER);
        strncpy (serverAlias[i], strtok(NULL, ", "), CHARBUFFER);
        serverAlias[i][strlen(serverAlias[i])-1] = '\0'; /*Eliminate '\n' at he end*/
        i++;
    }
    return;
}

void ReadRelays(FILE *fileRelays, char relayAlias[][CHARBUFFER], char relayIP[][CHARBUFFER], char relayPort[][CHARBUFFER]) {
    char line[CHARBUFFER] = {3};
    int i = 0;
    
    while (fgets(line, CHARBUFFER, fileRelays) != NULL) {
        strncpy (relayAlias[i], strtok(line, ", "), CHARBUFFER);
        strncpy (relayIP[i], strtok(NULL, ", "), CHARBUFFER);
        strncpy (relayPort[i], strtok(NULL, ", "), CHARBUFFER);
        relayPort[i][strlen(relayPort[i])-1] = '\0'; /*Eliminate '\n' at he end*/
        i++;
    }
    return;
}

int main(int argc, char *argv[]) {
    int i = 0;
    FILE *fileServers = NULL;
    FILE *fileRelays = NULL;
    char serverDomain[SERVERLINES][CHARBUFFER] = {3};
    char serverAlias[SERVERLINES][CHARBUFFER] = {3};
    char relayAlias[SERVERLINES][CHARBUFFER] = {3};
    char relayIP[SERVERLINES][CHARBUFFER] = {3};
    char relayPort[SERVERLINES][CHARBUFFER] = {3};
    
    /*Check arguments*/
    if (argc < 2) {
        printf("Not enough arguments\n");
        exit(1);
    }
    
    /*Open end_servers.txt*/
    fileServers = fopen(argv[1], "r");
    if (fileServers == NULL) {
        printf("Error opening argv[1]\n");
        exit(1);
    }    
    ReadServers(fileServers, serverDomain, serverAlias);    
    puts("End Servers");
    for(i=0; i < SERVERLINES; i++) {
        printf("%s, %s\n", serverDomain[i], serverAlias[i]);
    }
    
    /*Open relay_nodes.txt*/
    fileRelays = fopen(argv[2], "r");
    if (fileRelays == NULL) {
        printf("Error opening argv[2]\n");
        exit(1);
    }
    ReadRelays(fileRelays, relayAlias, relayIP, relayPort);
    puts("Relay Nodes");
    for(i=0; i < RELAYLINES; i++) {
        printf("%s, %s, %s\n", relayAlias[i], relayIP[i], relayPort[i]);
    }
    
    fclose(fileServers);
    fclose(fileRelays);
    /*system("PAUSE");*/
    return 0;
}