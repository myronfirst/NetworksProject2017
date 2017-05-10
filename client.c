
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

/*Thread struct arguments*/
struct receiveArgs {
        int sock;
        float *relayServerAvgRTT;
        int *relayServerHops;
    };
typedef struct receiveArgs RECEIVEARGS_S;
typedef struct receiveArgs* RECEIVEARGS_T;

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

void ReadServers(char* sourceFile, char *serverDomain[], char *serverAlias[]);
void ReadRelays(char *sourceFile, char *relayAlias[], char *relayIP[], char *relayPort[]);
void *Ping(void *pingArgs);
void *Traceroute(void *traceArgs);
void MessageRelays(char *endServerDomain, char *relayAlias[], char *relayIP[], char *relayPort[], char numPing[], float relayAvgRTT[], int relayHops[]);
void *ReceiveRelay(void *recArgs);
int BestAvgRTT(int *index, float serverAvgRTT, float relayAvgRTT[]);
int BestHops(int *index, int serverHops, int relayHops[]);
/*void FreeWorld();*/

int main(int argc, char* argv[]) {
    int i = 0;
    int j = 0;
    char *serverDomain[SERVERLINES] = {NULL};
    char *serverAlias[SERVERLINES] = {NULL};
    char *relayAlias[RELAYLINES] = {NULL};
    char *relayIP[RELAYLINES] = {NULL};
    char *relayPort[RELAYLINES] = {NULL};    
    char alias[ARGSLEN] = {'8'};
    char numPingBuf[ARGSLEN] = {'8'};
    char crit[ARGSLEN] = {'8'}; 
    char *numPing = NULL;
    
    int endServerIndex = -1;
    char *endServerAlias = NULL;
    char *endServerDomain = NULL;
    float endServerAvgRTT = 0.0f;
    int endServerHops = 0;
    
    float relayAvgRTT[RELAYLINES] = {0.0f};
    int relayHops[RELAYLINES] = {0};
    float transferAvgRTT[RELAYLINES] = {0.0f};
    int transferHops[RELAYLINES] = {0};
    
    int downloadIndex = -1;
    int avgRTTIndex = -1;
    int hopsIndex = -1;
    int tieAvgRTT = 0;
    int tieHops = 0;
    
    char downloadFile[3*CHARBUFFER] = {'8'};
    
    pthread_t pingTid[RELAYLINES];
    pthread_t traceTid[RELAYLINES];
    pthread_mutex_t lock;
    int err;
    PINGARGS_T pingArgs = NULL;
    TRACEARGS_T traceArgs = NULL;
    
    /*Check arguments*/
    if (argc < 2) {
        printf("Not enough arguments\n");
        exit(-1);
    }
    
    
    ReadServers(argv[1], serverDomain, serverAlias);
#if DEBUG
    puts("End Servers");
    for(i=0; i < SERVERLINES; i++) {
        printf("%s L=%d, %s L=%d\n", serverDomain[i], strlen(serverDomain[i]), serverAlias[i], strlen(serverAlias[i]));
        for(j = 0; j < strlen(serverDomain[i]); j++) {
            printf("serverDomain[%d][%d]=%c\n", i, j, serverDomain[i][j]);
        }
        for(j = 0; j < strlen(serverAlias[i]); j++) {
            printf("serverAlias[%d][%d]=%c\n", i, j, serverAlias[i][j]);
        }
    }
#endif
    
    
    ReadRelays(argv[2], relayAlias, relayIP, relayPort);
#if DEBUG
    puts("Relay Nodes");
    for(i=0; i < RELAYLINES; i++) {
        printf("%s L=%d, %s L=%d, %s L=%d\n", relayAlias[i], strlen(relayAlias[i]), relayIP[i], strlen(relayIP[i]), relayPort[i], strlen(relayPort[i]));
    }
#endif
    
    /*Ask user input*/
    printf("Give input: SERVERALIAS NUMPING CRITERION\n");
    scanf(" %s %s %s", alias, numPingBuf, crit);
    numPing = malloc(strlen(numPingBuf)*sizeof(char) + 1);
    strcpy(numPing, numPingBuf);
#if DEBUG
    printf("%s, %s, %s\n", alias, numPing, crit);
#endif
    
    /*Search Domain*/ /*Do Binary*/
    i = 0;
    endServerIndex = -1;
    while((i < SERVERLINES) && (endServerIndex == -1)) {
        if (strcmp(serverAlias[i], alias) == 0) {
            endServerIndex = i;
        }
        i++;
    }
    if (endServerIndex == -1) {
        printf("Server not in file. Exiting\n");
        exit(-1);
    }
    endServerAlias = serverAlias[endServerIndex];
    endServerDomain = serverDomain[endServerIndex];
#if DEBUG
    printf("endServerAlias= %s\n", endServerAlias);
    printf("endServerDomain= %s\n", endServerDomain);
#endif
    /*ping-trace end server*/
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("ping-trace endServer: mutex init failed\n");
        exit(-1);
    }
    pingArgs = malloc(sizeof(PINGARGS_S));
    pingArgs->resAvgRTT = &endServerAvgRTT;
    pingArgs->address = endServerDomain;
    pingArgs->numPing = numPing;
    /*start endServer ping thread*/
    err = pthread_create(&(pingTid[0]), NULL, &Ping, pingArgs);
    if (err != 0) {
        printf("ping endServer: can't create thread :[%s]\n", strerror(err));
        exit(-1);
    }

    traceArgs = malloc(sizeof(TRACEARGS_S));
    traceArgs->resHops = &endServerHops;
    traceArgs->address = endServerDomain;
    /*start endServer trace thread*/
    err = pthread_create(&(traceTid[0]), NULL, &Traceroute, traceArgs);
    if (err != 0) {
        printf("trace endServer:can't create thread :[%s]\n", strerror(err));
        exit(-1);
    }
    pthread_join(pingTid[0], NULL);
    pthread_join(traceTid[0], NULL);
    pthread_mutex_destroy(&lock); //Destroy --> Free the lock after serving its purpose
    
#if DEBUG
    printf("endServerAvgRTT=%f, endServerHops=%d\n", endServerAvgRTT, endServerHops);
#endif
    /*Tell relays to ping-trace and send results*/
    MessageRelays(endServerDomain, relayAlias, relayIP, relayPort, numPing, transferAvgRTT, transferHops);
    /*Threads should have finished filling relayAvgRTT[] and relayHops[]*/
    for (i = 0; i < RELAYLINES; i++) {
        printf("relay-->end_server: transferAvgRTT[%d]=%f, transferHops[%d]=%d\n", i, transferAvgRTT[i], i, transferHops[i]);
    }
    
    /*ping-trace relay_servers*/
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("ping-trace relayServers: mutex init failed\n");
        exit(-1);
    }
    for (i = 0; i < RELAYLINES; i++) {
        pingArgs = malloc(sizeof (PINGARGS_S));
        pingArgs->resAvgRTT = &relayAvgRTT[i];
        pingArgs->address = relayIP[i];
        pingArgs->numPing = numPing;
        /*start relayServer ping thread*/
        err = pthread_create(&(pingTid[i]), NULL, &Ping, pingArgs);
        if (err != 0) {
            printf("ping relayServer: can't create thread :[%s]\n", strerror(err));
            exit(-1);
        }
        pthread_join(pingTid[i], NULL); /*debug*/
        traceArgs = malloc(sizeof (TRACEARGS_S));
        traceArgs->resHops = &relayHops[i];
        traceArgs->address = relayIP[i];
        /*start endServer trace thread*/
        err = pthread_create(&(traceTid[i]), NULL, &Traceroute, traceArgs);
        if (err != 0) {
            printf("trace endServer:can't create thread :[%s]\n", strerror(err));
            exit(-1);
        }
        pthread_join(traceTid[i], NULL); /*debug*/
        printf("client-->relay: relayAvgRTT[%d]=%f, relayHops[%d]=%d\n", i, relayAvgRTT[i], i, relayHops[i]);
    }
    for (i = 0; i < RELAYLINES; i++) {
        pthread_join(pingTid[i], NULL);
        pthread_join(traceTid[i], NULL);
    }
    pthread_mutex_destroy(&lock);
    
    /*Sum*/
    for (i=0; i < RELAYLINES; i++) {
        relayAvgRTT[i] += transferAvgRTT[i];
        relayHops[i] += transferHops[i];
        printf("SUM: relayAvgRTT[%d]=%f, relayHops[%d]=%d\n", i, relayAvgRTT[i], i, relayHops[i]);
    }
    
    /*Select relay index*/
    tieAvgRTT = BestAvgRTT(&avgRTTIndex, endServerAvgRTT, relayAvgRTT);
    printf("avgRTTIndex= %d, tieAvgRTT=%d\n", avgRTTIndex, tieAvgRTT);
    tieHops = BestHops(&hopsIndex, endServerHops, relayHops);
    printf("hopsIndex= %d, tieHops=%d\n", hopsIndex, tieHops);
    
    if ((strcmp(crit, "latency")==0) && (tieAvgRTT==0)) {
        puts("selecting avgRTTIndex");
        downloadIndex = avgRTTIndex;
    }else if ((strcmp(crit, "hops")==0) && (tieHops==0)){
        puts("selecting HopsIndex");
        downloadIndex = hopsIndex;
    }else {
        puts("selecting RandomIndex (avgRTT)");
        downloadIndex = avgRTTIndex; /*Random*/
    }
    printf("downloadIndex=%d\n", downloadIndex);
    if (downloadIndex = -1) {
        printf("Direct download from end_server=%s\n", endServerAlias);
    }else {
        printf("Download via relay_node=%s, from end_server=%s\n", relayAlias[downloadIndex], endServerAlias);
    }
    /*download file*/
    printf("Input file to download:\n");
    scanf(" %s", downloadFile);
    strcpy(downloadFile, strtok(downloadFile, "\r\n"));
    if (strstr(downloadFile, endServerAlias)==NULL) {
        printf("File does not match server. Exiting\n");
        exit(-1);
    }
    printf("downloadFile= %s\n", downloadFile);
    
    
    
    /*Free stuff*/
    for (i=0; i < SERVERLINES; i++) {
        free(serverDomain[i]); serverDomain[i]= NULL;
        free(serverAlias[i]); serverAlias[i] = NULL;
    }
    for (i=0; i < RELAYLINES; i++) {
        free(relayAlias[i]); relayAlias[i]= NULL;
        free(relayIP[i]); relayIP[i]= NULL;
        free(relayPort[i]); relayPort[i]= NULL;
    }
    free(numPing);
    free (pingArgs);
    free (traceArgs);
    /*FreeWorld();*/
    /*system("PAUSE");*/
    return 0;
}

void ReadServers(char* sourceFile, char *serverDomain[], char *serverAlias[]) {
    char line[CHARBUFFER] = {'8'};
    char word[CHARBUFFER] = {'8'};
    int i =0;
    FILE *fileServers = NULL;
    
    /*Open end_servers.txt*/
    fileServers = fopen(sourceFile, "r");
    if (fileServers == NULL) {
        printf("Error opening %s\n", sourceFile);
        exit(-1);
    }
    
    i = 0;
    while (fgets(line, CHARBUFFER, fileServers) != NULL) {
        /*store Domain*/
        strcpy(word, strtok(line, ", \r\n"));
        serverDomain[i] = malloc(strlen(word)*sizeof(char) + 1);
        strcpy (serverDomain[i], word);
#if DEBUG
        printf("SDomain[%d}=%s\n", i, serverDomain[i]);
#endif
        /*store alias*/
        strcpy(word, strtok(NULL, ", \r\n"));
        serverAlias[i] = malloc(strlen(word)*sizeof(char) + 1);
        strcpy (serverAlias[i], word);
#if DEBUG
        printf("SAlias[%d]=%s\n", i, serverAlias[i]);
#endif
        i++;
    }
    fclose(fileServers);
    return;
}

void ReadRelays(char *sourceFile, char *relayAlias[], char *relayIP[], char *relayPort[]) {
    char line[CHARBUFFER] = {'8'};
    char word[CHARBUFFER] = {'8'};
    int i = 0;
    FILE *fileRelays = NULL;
    
    /*Open relay_nodes.txt*/
    fileRelays = fopen(sourceFile, "r");
    if (fileRelays == NULL) {
        printf("Error opening %s\n", sourceFile);
        exit(-1);
    }
    
    while (fgets(line, CHARBUFFER, fileRelays) != NULL) {
        /*store relayAlias*/
        strcpy(word, strtok(line, ", \r\n"));
        relayAlias[i] = malloc(strlen(word)*sizeof(char) + 1);
        strcpy (relayAlias[i], word);
#if DEBUG
        printf("RAlias[%d]=%s\n", i, relayAlias[i]);
#endif
        /*store relayIP*/
        strcpy(word, strtok(NULL, ", \r\n"));
        relayIP[i] = malloc(strlen(word)*sizeof(char) + 1);
        strcpy (relayIP[i], word);
#if DEBUG
        printf("RIP[%d]=%s\n", i, relayIP[i]);
#endif
        /*store relayPort*/
        strcpy(word, strtok(NULL, ", \r\n"));
        relayPort[i] = malloc(strlen(word)*sizeof(char) + 1);
        strcpy (relayPort[i], word);
#if DEBUG
        printf("RPort[%d]=%s\n", i, relayPort[i]);
#endif
        i++;
    }
    fclose(fileRelays);
    return;
}

void *Ping(void *pingArgs) {
    char *command = NULL;
    char cmdArgs[ARGSLEN] = {'8'};
    char file[ARGSLEN] = {'8'};
    FILE *filePing = NULL;
    int i = 0;
    char line[CHARBUFFER] = {'8'};
    char *ptrLine = line;
    
    PINGARGS_T args = (PINGARGS_T)pingArgs;
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
    
    command = malloc((strlen("ping ")+strlen(cmdArgs)+strlen(address)+strlen(" >| ")+strlen(file))*sizeof(char) + 1);
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
    
    fclose (filePing);
    if (remove(file) != 0) {
        printf("%s not deleted\n", file);
    }
    free(command); command = NULL;
    return NULL;
}

void *Traceroute(void *traceArgs) {
    char *command = NULL;
    char file[ARGSLEN] = {'8'};
    FILE *fileTrace = NULL;
    int i = 0;
    char line[4*CHARBUFFER] = {'8'};
    char *ptrLine = line;
    pthread_t tid;
    char id[ARGSLEN] = {'8'};
    tid = pthread_self();
    sprintf(id, "%d", tid);
    
    TRACEARGS_T args = (TRACEARGS_T)traceArgs;
    int *hops = args->resHops;
    char *address = args->address;
    
    strcpy(file, id);
    strcat(file, "trace_res");
    
    command = malloc((strlen("traceroute ")+strlen(address)+strlen(" >| ")+strlen(file))*sizeof(char) + 1);
    strcpy(command, "traceroute ");
    strcat(command, address);
    strcat(command, " >| ");
    strcat(command, file);
    printf("%s\n", command);
    system(command);
    
    fileTrace= fopen(file, "r+");
    if (fileTrace == NULL) {
        printf("Error opening %s\n", file);
        exit(-1);
    }
    while (fgets(line, 4*CHARBUFFER, fileTrace) != NULL) {
    }
    *hops = atoi(line);
    printf("Traceroute: %s, returns: %d\n", address, *hops);
    
    fclose (fileTrace);
    if (remove(file) != 0) {
        printf("%s not deleted\n", file);
    }
    free(command); command = NULL;
    return NULL;
}

void MessageRelays(char *endServerDomain, char *relayAlias[], char *relayIP[], char *relayPort[], char numPing[], float relayAvgRTT[], int relayHops[]) {
    int sock = -1;                        /* Socket descriptor */
    struct sockaddr_in ServAddr;         /* server address */
    char *servAlias = NULL;
    char *servIP = NULL;     
    unsigned short servPort = 0;         /* server port */
    char *message = NULL;           /* message to relayServer */
    int messageL = 0;
    int bytesRcvd = 0;              /* Bytes read in single recv() */
    int totalBytesRcvd = 0;
    char recvBuffer[CHARBUFFER] = {'8'};
    int i = 0;
    
    pthread_t tid[RELAYLINES];
    pthread_mutex_t lock;
    int err;
    RECEIVEARGS_T recArgs = NULL;
    
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("mutex init failed\n");
        exit(-1);
    }

    for (i = 0; i < RELAYLINES; i++) {
        printf("Args: relayAlias=%s, relayIp=%s, relayPort=%s\n", relayAlias[i], relayIP[i], relayPort[i]);
        servAlias = relayAlias[i];
        servIP = relayIP[i];
        servPort = atoi(relayPort[i]);
        message = malloc(strlen("p www.CamenosScientofilos.com 6666") * sizeof (char) + 1);
        strcpy(message, "p ");
        strcat(message, endServerDomain);
        strcat(message, " ");
        strcat(message, numPing);
        messageL = strlen(message);
        printf("servIP=%s, servPort=%u, message=%s, messageLength=%d\n", servIP, servPort, message, messageL);
        /* Create a reliable, stream socket using TCP */
        if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
            printf("socket() failed\n");
            exit(-1);
        }
        printf("Sock=%d Created\n", sock);
        /* Construct the server address structure */
        memset(&ServAddr, 0, sizeof (ServAddr)); /* Zero out structure */
        ServAddr.sin_family = AF_INET; /* Internet address family */
        ServAddr.sin_addr.s_addr = inet_addr(servIP); /* Server IP address */
        ServAddr.sin_port = htons(servPort); /* Server port */
        /* Establish the connection to the echo server */
        if (connect(sock, (struct sockaddr *) &ServAddr, sizeof (ServAddr)) < 0) {
            printf("Sock=%d connect() failed\n", sock);
            exit(-1);
        }
        printf("Sock=%d Connected\n", sock);

        if (send(sock, message, messageL, 0) != messageL) {
            printf("send() sent a different number of bytes than expected");
        }
        printf("Sock=%d Message Sent\n", sock);

        /*threads awating response from relays*/
        /*init thread argument struct*/
        recArgs = malloc(sizeof(RECEIVEARGS_S));
        recArgs->sock = sock;
        recArgs->relayServerAvgRTT = &relayAvgRTT[i];
        recArgs->relayServerHops = &relayHops[i];
#if DEBUG
        printf("recArgs struct before thread:\n sock=%d, *relayServerAvgRTT=%f, *relayServerHops=%d\n", recArgs->sock, *(recArgs->relayServerAvgRTT), *(recArgs->relayServerHops));
#endif
        // pthread_create(&Thread_var, NULL, &Thread_function, &Thread_args);
        // Thread function takes only 1 argument. Want to pass multiple? pass a struct!	
        err = pthread_create(&(tid[i]), NULL, &ReceiveRelay, recArgs);
        if (err != 0) {
            printf("can't create thread :[%s]\n", strerror(err));
            exit(-1);
        }
        /*pthread_join(tid[i], NULL); Debug wait*/
    }
    
    for (i = 0; i < RELAYLINES; i++) {
        pthread_join(tid[i], NULL); //Force MessageRelays to wait for thread tid[i]. Only when thread finishes MessageRelays may continue after this point
    }
    pthread_mutex_destroy(&lock); //Destroy --> Free the lock after serving its purpose
#if DEBUG
    printf("MessageRelay returning\n");
#endif    
    close(sock);
    free(recArgs);
    return;
}

void *ReceiveRelay(void *recArgs) {
    int bytesRcvd = 0; /* Bytes read in single recv() */
    int totalBytesRcvd = 0;
    char recvBuffer[CHARBUFFER] = {'8'};
    RECEIVEARGS_T args = (RECEIVEARGS_T)recArgs;
    int sock = args->sock;
    float *relayServerAvgRTT = args->relayServerAvgRTT;
    int *relayServerHops = args->relayServerHops;
    printf("(socket=%d) Initial struct contents:\nsock=%d, *relayServerAvgRTT =%d, *relayServerHops=%d\n", sock, sock, *relayServerAvgRTT, *relayServerHops);

    totalBytesRcvd = 0;
    /* Receive up to the buffer size (minus 1 to leave space for
       a null terminator) bytes from the sender */
    if ((bytesRcvd = recv(sock, recvBuffer, CHARBUFFER - 1, 0)) <= 0) {
        printf("(socket=%d) recv() failed or connection closed prematurely\n", sock);
        printf("(socket=%d) bytesRcvd=%d\n", sock, bytesRcvd);
        printf("(socket=%d) Received: %s\n", sock, recvBuffer);
        exit(-1);
    }
    totalBytesRcvd += bytesRcvd; /* Keep tally of total bytes */
    recvBuffer[bytesRcvd] = '\0'; /* Terminate the string! */
    printf("%s\n", recvBuffer); /* Print the echo buffer */

    *relayServerAvgRTT = atof(strtok(recvBuffer, " \n"));
    *relayServerHops = atof(strtok(NULL, " \n"));
    
#if DEBUG
    printf("(socket=%d) ReceiveRelay: sock=%d, *relayServerAvgRTT=%f, *relayServerHops=%d\n", sock, sock, *relayServerAvgRTT, *relayServerHops);
#endif
    printf("(socket=%d) Returning\n", sock);
    return NULL;
}

int BestAvgRTT(int *index, float serverAvgRTT, float relayAvgRTT[]) {
    float min = 0.0f;
    int i = 0;
    int tie = 0;
    
    printf("Args: *index= %d, serverRTT= %f, relayRTT[0]=%f\n", *index, serverAvgRTT, relayAvgRTT[0]);    
    *index = -1; /*Represents server-direct route*/
    min = serverAvgRTT;
    i = 0;
    while (i < RELAYLINES) {
        if (relayAvgRTT[i] < min) {
            min = relayAvgRTT[i];
            *index = i;
            tie = 0;
        } else if (relayAvgRTT[i] == min) {
            tie = 1;
        }
        i++;
    }
    return tie;
}

int BestHops(int *index, int serverHops, int relayHops[]) {
    int min = 0;
    int i = 0;
    int tie = 0;
    
    printf("Args: *index= %d, serverHops= %d, relayHops[0]=%d\n", *index, serverHops, relayHops[0]);    
    *index = -1; /*Represents server-direct route*/
    min = serverHops;
    i = 0;
    while (i < RELAYLINES) {
        if (relayHops[i] < min) {
            min = relayHops[i];
            *index = i;
            tie = 0;
        } else if (relayHops[i] == min) {
            tie = 1;
        }
        i++;
    }
    return tie;
}
/*void FreeWorld(){}*/
