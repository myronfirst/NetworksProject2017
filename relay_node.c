
#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEBUG 1
#define CHARBUFFER 64
#define SERVERLINES 3
#define RELAYLINES 2
#define ARGSLEN 32
#define AVGRTTOFFSET 30
#define MAXCONNECTIONS 5

struct pingArgs {
    float *resAvgRTT;
    char *address;
    char *numPing;
};
typedef struct pingArgs PINGARGS_S;
typedef struct pingArgs* PINGARGS_T;

struct traceArgs {
    int *resHops;
    char *address;
};
typedef struct traceArgs TRACEARGS_S;
typedef struct traceArgs* TRACEARGS_T;

void *Ping(void *pingArgs);
void *Traceroute(void *traceArgs);
void MessageClient(int clntSock, char *message);

int main(int argc, char *argv[]) {
    int servSock; /* Socket descriptor for server */
    int clntSock; /* Socket descriptor for client */
    struct sockaddr_in ServAddr; /* Local address */
    struct sockaddr_in ClntAddr; /* Client address */
    unsigned short ServPort; /* Server port */
    unsigned int clntLen; /* Length of client address data structure */
    char recvBuffer[CHARBUFFER]; /* Buffer for echo string */
    int recvMsgSize; /* Size of received message */

    char word[CHARBUFFER] = {'8'};
    char *endServerDomain = NULL;
    char *numPing = NULL;
    char *url = NULL;
    float endServerAvgRTT = 0.0f;
    int endServerHops = 0;
    char *message = NULL;
    
    pthread_t pingTid;
    pthread_t traceTid;
    pthread_mutex_t lock;
    int err;
    PINGARGS_T pingArgs = NULL;
    TRACEARGS_T traceArgs = NULL;

    if (argc != 2) /* Test for correct number of arguments */ {
        fprintf(stderr, "Usage:  %s <Server Port>\n", argv[0]);
        exit(-1);
    }

    ServPort = atoi(argv[1]); /* First arg:  local port */

    /* Create socket for incoming connections */
    if ((servSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        printf("socket() failed\n");
        exit(-1);
    }

    /* Construct local address structure */
    memset(&ServAddr, 0, sizeof (ServAddr)); /* Zero out structure */
    ServAddr.sin_family = AF_INET; /* Internet address family */
    ServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    ServAddr.sin_port = htons(ServPort); /* Local port */

    /* Bind to the local address */
    if (bind(servSock, (struct sockaddr *) &ServAddr, sizeof (ServAddr)) < 0) {
        printf("bind() failed\n");
        exit(-1);
    }

    /* Mark the socket so it will listen for incoming connections */
    if (listen(servSock, MAXCONNECTIONS) < 0) {
        printf("listen() failed\n");
        exit(-1);
    }
#if DEBUG
    printf("Entering forever loop\n");
#endif
    for (;;) { /* Run forever */
        /* Set the size of the in-out parameter */
        clntLen = sizeof (ClntAddr);

        /* Wait for a client to connect */
        if ((clntSock = accept(servSock, (struct sockaddr *) &ClntAddr, &clntLen)) < 0) {
            printf("accept() failed\n");
            exit(-1);
        }
        /* clntSock is connected to a client! */

        printf("Handling client %s\n", inet_ntoa(ClntAddr.sin_addr));

        /* Receive message from client */
        if ((recvMsgSize = recv(clntSock, recvBuffer, CHARBUFFER, 0)) < 0) {
            printf("recv() failed\n");
            exit(-1);
        }
#if DEBUG
        printf("Received: %s\n", recvBuffer); // print the received message
#endif        
        if (recvBuffer[0] == 'p') {
            printf("recvBuffer=%s\n", recvBuffer);
            strtok(recvBuffer, " \n");
            strcpy(word, strtok(NULL, " \n"));
            endServerDomain = malloc(strlen(word) * sizeof (char) + 1);
            strcpy(endServerDomain, word);
            strcpy(word, strtok(NULL, " \n"));
            numPing = malloc(strlen(word) * sizeof (char) + 1);
            strcpy(numPing, word);
#if DEBUG
            printf("endServerDomain=%s, numPing=%s, pinging and tracerouting end_server...\n", endServerDomain, numPing);
#endif
            if (pthread_mutex_init(&lock, NULL) != 0) {
                printf("ping-trace endServer: mutex init failed\n");
                exit(-1);
            }
            pingArgs = malloc(sizeof (PINGARGS_S));
            pingArgs->resAvgRTT = &endServerAvgRTT;
            pingArgs->address = endServerDomain;
            pingArgs->numPing = numPing;
            /*start endServer ping thread*/
            err = pthread_create(&(pingTid), NULL, &Ping, pingArgs);
            if (err != 0) {
                printf("ping endServer: can't create thread :[%s]\n", strerror(err));
                exit(-1);
            }

            traceArgs = malloc(sizeof (TRACEARGS_S));
            traceArgs->resHops = &endServerHops;
            traceArgs->address = endServerDomain;
            /*start endServer trace thread*/
            err = pthread_create(&(traceTid), NULL, &Traceroute, traceArgs);
            if (err != 0) {
                printf("trace endServer:can't create thread :[%s]\n", strerror(err));
                exit(-1);
            }
            pthread_join(pingTid, NULL);
            pthread_join(traceTid, NULL);
            pthread_mutex_destroy(&lock); //Destroy --> Free the lock after serving its purpose
            
            /* Send ping and trace to client*/
            sprintf(word, "%f %d", endServerAvgRTT, endServerHops);
            message = malloc(strlen(word) * sizeof (char) + 1);
            strcpy(message, word);
            printf("Message to send to client=%s, Length=%d\n", message, strlen(message));
            MessageClient(clntSock, message);
        } else if (recvBuffer[0] == 'd') {
            puts("Download not yet implemented");
            /*DownloadFile*/
        } else {
            printf("Received wrong message from client :(");
        }

        free(endServerDomain);
        free(numPing);
        free(url);
        close(clntSock); /* Close client socket */
    }
    /* NOT REACHED */
}

void *Ping(void *pingArgs) {
    char *command = NULL;
    char cmdArgs[ARGSLEN] = {'8'};
    char file[ARGSLEN] = {'8'};
    FILE *filePing = NULL;
    int i = 0;
    char line[CHARBUFFER] = {'8'};
    char *ptrLine = line;

    PINGARGS_T args = (PINGARGS_T) pingArgs;
    float *avgRTT = args->resAvgRTT;
    char *address = args->address;
    char *numPing = args->numPing;
    pthread_t tid;
    char id[ARGSLEN] = {'8'};
    tid = pthread_self();
    sprintf(id, "%d", tid);

    strcpy(cmdArgs, "-c ");
    strcat(cmdArgs, numPing);
    strcat(cmdArgs, " ");

    strcpy(file, id);
    strcat(file, "ping_res");

    command = malloc((strlen("ping ") + strlen(cmdArgs) + strlen(address) + strlen(" >| ") + strlen(file)) * sizeof (char) + 1);
    strcpy(command, "ping ");
    strcat(command, cmdArgs);
    strcat(command, address);
    strcat(command, " >| ");
    strcat(command, file);
    printf("%s\n", command);
    system(command);

    filePing = fopen(file, "r+");
    if (filePing == NULL) {
        printf("Error opening %s\n", file);
        exit(-1);
    }
    while (fgets(line, CHARBUFFER, filePing) != NULL) {
    }
    ptrLine = &line[AVGRTTOFFSET];
    *avgRTT = atof(ptrLine);
    printf("Ping: %s, returns: %f\n", address, *avgRTT);

    fclose(filePing);
    if (remove(file) != 0) {
        printf("%s not deleted\n", file);
    }
    free(command);
    command = NULL;
    return NULL;
}

void *Traceroute(void *traceArgs) {
    char *command = NULL;
    char file[ARGSLEN] = {'8'};
    FILE *fileTrace = NULL;
    int i = 0;
    char line[4 * CHARBUFFER] = {'8'};
    char *ptrLine = line;
    pthread_t tid;
    char id[ARGSLEN] = {'8'};
    tid = pthread_self();
    sprintf(id, "%d", tid);

    TRACEARGS_T args = (TRACEARGS_T) traceArgs;
    int *hops = args->resHops;
    char *address = args->address;

    strcpy(file, id);
    strcat(file, "trace_res");

    command = malloc((strlen("traceroute ") + strlen(address) + strlen(" >| ") + strlen(file)) * sizeof (char) + 1);
    strcpy(command, "traceroute ");
    strcat(command, address);
    strcat(command, " >| ");
    strcat(command, file);
    printf("%s\n", command);
    system(command);

    fileTrace = fopen(file, "r+");
    if (fileTrace == NULL) {
        printf("Error opening %s\n", file);
        exit(-1);
    }
    while (fgets(line, 4 * CHARBUFFER, fileTrace) != NULL) {
    }
    *hops = atoi(line);
    printf("Traceroute: %s, returns: %d\n", address, *hops);

    fclose(fileTrace);
    if (remove(file) != 0) {
        printf("%s not deleted\n", file);
    }
    free(command);
    command = NULL;
    return NULL;
}

void MessageClient(int clntSock, char *message) {
    int messageL = strlen(message);

    if (send(clntSock, message, messageL, 0) != messageL) {
        printf("MessageClient: send() failed\n");
        exit(-1);
    }
#if DEBUG
    printf("MessageClient: Sent to client: %s\n", message);
#endif
    return;
}

