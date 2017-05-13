
#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include<fcntl.h>
#include "ThreadStructs.h"


#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m"

#define EXECUTABLE_NAME   "input control.exe"
#define NO_OF_HOPS   30
#define TIMEOUT_DELAY   20

/*Stores the command line arguments*/
struct clArgs {
        int hops;
        int timeout;
        char endServers[65];
		char relayNodes[65];
    };
typedef struct clArgs CMDARGS_S;
typedef struct clArgs* CMDARGS_T;

#define DEBUG 0
#define CHARBUFFER 32
#define FILEBUFFER 1024
/*Reads Command line arguments and stores them in a clArgs(CMDARGS) struct*/
void ReadArgs(int argc, char* argv[], CMDARGS_T args);
/*Prints the correct syntax of the command line arguments*/
void PrintSyntax(){
	printf ("Correct syntax is:\tEXECUTABLE_NAME (-e <end servers filename>) (-r <relay nodes filename>) [-h <Max number of hops>] [-w <timeout delay>]\n");
	printf ("Arguments in \'()\' are mandatory, arguments in \'[]\' are optional.\n");
	printf ("(Default max number of hops: %d. Default timeout delay: %d sec\n", NO_OF_HOPS, TIMEOUT_DELAY);
}
/*Reads end_servers.txt and returns user selected endServerDomain*/
char *ReadServers(char* sourceFile, char *userAlias);
/*Reads relay_nodes.txt and returns number of relay_nodes*/
int ReadRelays(char *sourceFile, char ***relayAlias, char ***relayIP, char ***relayPort);
/*Messages relay to Ping end_Server and created thread to wait for results*/
void *CommunicateRelay(void *comArgs);
/*Ping server specified in pingArgs*/
void *Ping(void *pingArgs);
/*Traceroute server specified in traceArgs*/
void *Traceroute(void *traceArgs);
/*Determine best relayIndex via AvgRTT (-1 if direct route to endServer is better), return 1 if there is tie*/
int BestAvgRTT(int *index, int relaySize, float serverAvgRTT, float relayAvgRTT[]);
/*Determine best relayIndex via Hops (-1 if direct route to endServer is better), return 1 if there is tie*/
int BestHops(int *index, int relaySize, int serverHops, int relayHops[]);
/*Download file and count download time*/
void *DownloadFile(void *downArgs);
/*Download file from relay and count download time*/
void *DownloadRelay(void *downRArgs);

int main(int argc, char* argv[]) {
    pthread_t *comTid = NULL;
    pthread_t *relayPingTid = NULL;
    pthread_t *relayTraceTid = NULL;
    pthread_t *endPingTid = NULL;
    pthread_t *endTraceTid = NULL;
    pthread_t *downTid = NULL;
    pthread_mutex_t lock;
    COMMUNICATIONARGS_T comArgs = NULL;
    PINGARGS_T endPingArgs = NULL;
    TRACEARGS_T endTraceArgs = NULL;
    PINGARGS_T relayPingArgs = NULL;
    TRACEARGS_T relayTraceArgs = NULL;
    DOWNARGS_T downArgs = NULL;
    int err = -1;
    
    char *userAlias = NULL;
    char *numPing = NULL;
    char *criterion = NULL;
    char *endServerAlias = NULL;
    char *endServerDomain = NULL;
    char **relayAlias = NULL;
    char **relayIP = NULL;
    char **relayPort = NULL;
    int relaySize = 0;    
    
    float *transferAvgRTT = NULL;
    int *transferHops = NULL;
    float *relayAvgRTT = NULL;
    int *relayHops = NULL;
    float endServerAvgRTT = 0.0f;
    int endServerHops = 0;
    
    int downloadIndex = -1;
    int avgRTTIndex = -1;
    int hopsIndex = -1;
    int tieAvgRTT = 0;
    int tieHops = 0;
    time_t t;
    
    char *downloadURL = NULL;
    float downTime = 0.0f;
    
    char word[CHARBUFFER] = {'8'};
    char line[4*CHARBUFFER] = {'8'};
    char url[5*CHARBUFFER]  = {'8'};
    int i = 0; int j = 0;
    
    CMDARGS_T args = malloc(sizeof(CMDARGS_S));
	
    /*Read & Check arguments*/
	ReadArgs(argc, argv, args);
    
    /*Take user input*/
    printf("Give input: SERVERALIAS NUMPING CRITERION\n");
    if (fgets(line, 4*CHARBUFFER, stdin) == NULL) {
        printf("Wrong input format\n");
        exit(-1);
    }
    strcpy(word, strtok(line, " \r\n"));
    userAlias = malloc(strlen(word)*sizeof(char) + 1);
    strcpy(userAlias, word);
    strcpy(word, strtok(NULL, " \r\n"));
    numPing = malloc(strlen(word)*sizeof(char) + 1);
    strcpy(numPing, word);
    strcpy(word, strtok(NULL, " \r\n"));
    criterion = malloc(strlen(word)*sizeof(char) + 1);
    strcpy(criterion, word);
#if DEBUG
    printf("%s, %s, %s\n", userAlias, numPing, criterion);
#endif
    /*Save end_Server Domain and Alias*/
    endServerDomain = ReadServers(args->endServers, userAlias);
    endServerAlias = userAlias;
#if DEBUG    
    printf("endServerAlias=%s\n", endServerAlias);
    printf("endServerDomain=%s\n", endServerDomain);
#endif
    
    /*Save relay_Servers Alias, IP, Port*/
    relaySize = ReadRelays(args->relayNodes, &relayAlias, &relayIP, &relayPort);
#if DEBUG
    printf("RelaySize=%d\n", relaySize);

    puts("Relay Nodes");
    for(i=0; i < relaySize; i++) {
        printf("%s L=%d, %s L=%d, %s L=%d\n", relayAlias[i], strlen(relayAlias[i]), relayIP[i], strlen(relayIP[i]), relayPort[i], strlen(relayPort[i]));
    }
#endif
    
    /*Initialize thread lock*/
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("mutex init failed\n");
        exit(-1);
    }
    /*Assign each receiveThread to a socket*/
    transferAvgRTT = malloc(relaySize*sizeof(float));
    transferHops = malloc(relaySize*sizeof(int));
    comArgs = malloc(relaySize*sizeof(COMMUNICATIONARGS_S));
    comTid = malloc(relaySize*sizeof(pthread_t));
    for (i = 0; i < relaySize; i++) {        
        comArgs[i].endServerDomain = endServerDomain;
        comArgs[i].numPing = numPing;
        comArgs[i].relayIP = relayIP[i];
        comArgs[i].relayPort = relayPort[i];
        comArgs[i].relayServerAvgRTT = &(transferAvgRTT[i]);
        comArgs[i].relayServerHops = &(transferHops[i]);
        err = pthread_create(&(comTid[i]), NULL, &CommunicateRelay, &(comArgs[i]));
        if (err != 0) {
            printf("Communicate Relay can't create thread :[%s]\n", strerror(err));
            exit(-1);
        }
        /*pthread_join(comTid[i], NULL);*/
    }    
#if (DEBUG>1)
    for(i=0; i < relaySize; i++) {
        printf("&(comTid[%d])=%d\n", i, &(recTid[%i]));
        printf("&(relayPingTid[%d])=%d\n", i, &(relayPingTid[%i]));
        printf("&(relayTraceTid[%d])=%d\n", i, &(relayTraceTid[%i]));
    }
    printf("endPingTid=%d\n", i, endPingTid);
    printf("endTraceTid=%d\n", i, endTraceTid);
    for (i = 0; i < relaySize; i++) {
        printf("transferAvgRTT[%d]= %f, transferAvgHops[%d]=%d\n", i, transferAvgRTT[i], i, transferHops[i]);
    }
#endif
    
    /*endServer ping thread*/
    endServerAvgRTT = -1;
    endPingArgs = malloc(sizeof(PINGARGS_S));
    endPingArgs->resAvgRTT = &endServerAvgRTT;
    endPingArgs->address = endServerDomain;
    endPingArgs->numPing = numPing;
    endPingTid = malloc(sizeof(pthread_t));
    err = pthread_create(endPingTid, NULL, &Ping, endPingArgs);
    if (err != 0) {
        printf("ping endServer: can't create thread :[%s]\n", strerror(err));
        exit(-1);
    }
    /*pthread_join(*endPingTid, NULL);*/
    
    /*endServer trace thread*/
    endServerHops = -1;
    endTraceArgs = malloc(sizeof(TRACEARGS_S));
    endTraceArgs->resHops = &endServerHops;
    endTraceArgs->address = endServerDomain;
    endTraceTid = malloc(sizeof(pthread_t));
    err = pthread_create(endTraceTid, NULL, &Traceroute, endTraceArgs);
    if (err != 0) {
        printf("trace endServer: can't create thread :[%s]\n", strerror(err));
        exit(-1);
    }
    /*pthread_join(*endTraceTid, NULL);*/
#if (DEBUG>1)
    for (i = 0; i < relaySize; i++) {
        printf("transferAvgRTT[%d]= %f, transferAvgHops[%d]=%d\n", i, transferAvgRTT[i], i, transferHops[i]);
    }
    printf("endServerAvgRTT=%f\n", endServerAvgRTT);
    printf("endServerHops=%d\n", endServerHops);
#endif
    
    relayAvgRTT = malloc(relaySize*sizeof(float));
    relayHops = malloc(relaySize*sizeof(int));
    relayPingArgs = malloc(relaySize*sizeof(PINGARGS_S));
    relayTraceArgs = malloc(relaySize*sizeof(TRACEARGS_S));
    relayPingTid = malloc(relaySize*sizeof(pthread_t));
    relayTraceTid = malloc(relaySize*sizeof(pthread_t));
    for (i = 0; i < relaySize; i++) {
#if DEBUG
        printf("RelayIP[%d]=%s\n", i, relayIP[i]);
#endif
        /*relayServer ping thread*/
        relayPingArgs[i].numPing = numPing;
        relayPingArgs[i].address = relayIP[i];
        relayPingArgs[i].resAvgRTT = &(relayAvgRTT[i]);
        err = pthread_create(&(relayPingTid[i]), NULL, &Ping, &(relayPingArgs[i]));
        if (err != 0) {
            printf("ping relayServer: can't create thread :[%s]\n", strerror(err));
            exit(-1);
        }
        /*pthread_join(relayPingTid[i], NULL);*/
        
        /*relayServer trace thread*/
        relayTraceArgs[i].address = relayIP[i];
        relayTraceArgs[i].resHops = &(relayHops[i]);        
        err = pthread_create(&(relayTraceTid[i]), NULL, &Traceroute, &(relayTraceArgs[i]));
        if (err != 0) {
            printf("trace endServer: can't create thread :[%s]\n", strerror(err));
            exit(-1);
        }
        /*pthread_join(relayTraceTid[i], NULL);*/
    }
    
    /*Wait for threads to finish*/
    printf("Gathering statistical information...\n");
    for (i = 0; i < relaySize; i++) {
        pthread_join(comTid[i], NULL);
    }
    pthread_join(*endPingTid, NULL);
    pthread_join(*endTraceTid, NULL);
    for (i = 0; i < relaySize; i++) {
        pthread_join(relayPingTid[i], NULL);
        pthread_join(relayTraceTid[i], NULL);
    }
    pthread_mutex_destroy(&lock);
#if DEBUG
    for (i = 0; i < relaySize; i++) {
        printf("transferAvgRTT[%d]= %f, transferAvgHops[%d]=%d\n", i, transferAvgRTT[i], i, transferHops[i]);
    }
    printf("endServerAvgRTT=%f\n", endServerAvgRTT);
    printf("endServerHops=%d\n", endServerHops);
    for (i = 0; i < relaySize; i++) {
        printf("relayAvgRTT[%d]= %f, relayAvgHops[%d]=%d\n", i, relayAvgRTT[i], i, relayHops[i]);
    }
#endif
    /*Sum*/
    for (i=0; i < relaySize; i++) {
        relayAvgRTT[i] += transferAvgRTT[i];
        relayHops[i] += transferHops[i];
    }
#if DEBUG
    printf("SUM\n");
    for (i=0; i < relaySize; i++) {
        printf("relayAvgRTT[%d]=%f, relayHops[%d]=%d\n", i, relayAvgRTT[i], i, relayHops[i]);
    }
#endif
    
    /*Select relay index*/
    tieAvgRTT = BestAvgRTT(&avgRTTIndex, relaySize, endServerAvgRTT, relayAvgRTT);
#if DEBUG
    printf("avgRTTIndex= %d, tieAvgRTT=%d\n", avgRTTIndex, tieAvgRTT);
#endif
    tieHops = BestHops(&hopsIndex, relaySize, endServerHops, relayHops);
#if DEBUG
    printf("hopsIndex= %d, tieHops=%d\n", hopsIndex, tieHops);
#endif
    
    /*Select downloadIndex via user choice*/
    if ((strcmp(criterion, "latency") == 0) && (tieAvgRTT == 0)) {
        if (tieHops == 1) printf("TIE on HOPS CRITERION");
        printf("Selecting best route via LATENCY\n");
        downloadIndex = avgRTTIndex;
    } else if ((strcmp(criterion, "hops") == 0) && (tieHops == 0)) {
        if (tieAvgRTT == 1) printf("TIE on LATENCY CRITERION");
        printf("Selecting best route via HOPS\n");
        downloadIndex = hopsIndex;
    } else {
        printf("TIE on BOTH CRITERIA");
        printf("Selecting best route via RANDOM CRITERION\n");
        srand((unsigned) time(&t));
        if ((rand() % 2) == 0) {
            downloadIndex = avgRTTIndex;
            printf("Selecting best route via LATENCY\n");
        } else {
            downloadIndex = hopsIndex;
            printf("Selecting best route via HOPS\n");
        }
    }
    downloadIndex = -1; /*DEBUG*/
#if DEBUG
    printf("downloadIndex=%d\n", downloadIndex);
#endif
    /*download file*/
    printf("Input file URL to download:\n");
    if (fgets(url, 5*CHARBUFFER, stdin) == NULL) {
        printf("Wrong input format\n");
        exit(-1);
    }
    strtok(url, " \r\n");
    downloadURL = malloc(strlen(url)*sizeof(char) + 1);
    strcpy(downloadURL, url);
    if (strstr(downloadURL, endServerAlias)==NULL) {
        printf("File URL not match endServer URL. Exiting\n");
        exit(-1);
    }
#if DEBUG
    printf("downloadURL= %s\n", downloadURL);
#endif
    if (downloadIndex == -1) {
        printf("Direct download from end_server=%s\n", endServerAlias);
        downArgs = malloc(sizeof (DOWNARGS_S));
        downArgs->relayIP = NULL;
        downArgs->relayPort = NULL;
        downArgs->url = downloadURL;
        downArgs->downTime = &downTime;
        downTid = malloc(sizeof (pthread_t));
        err = pthread_create(downTid, NULL, &DownloadFile, downArgs);
        if (err != 0) {
            printf("Download File: can't create thread :[%s]\n", strerror(err));
            exit(-1);
        }
        printf("Waiting for file Download\n");
        pthread_join(*downTid, NULL);
        printf("Download Time: %f seconds\n", downTime);
    }else {
        printf("Download via relay_node=%s, from end_server=%s\n", relayAlias[downloadIndex], endServerAlias);
        downArgs = malloc(sizeof (DOWNARGS_S));
        downArgs->relayIP = relayIP[downloadIndex];
        downArgs->relayPort = relayPort[downloadIndex];
        downArgs->url = downloadURL;
        downArgs->downTime = &downTime;
        downTid = malloc(sizeof (pthread_t));
        err = pthread_create(downTid, NULL, &DownloadRelay, downArgs);
        if (err != 0) {
            printf("Download File From Relay: can't create thread :[%s]\n", strerror(err));
            exit(-1);
        }
        printf("Waiting for file Download\n");
        pthread_join(*downTid, NULL);
        printf("Download Time: %f seconds\n", downTime);
    }
    
    /*Free*/
    free(userAlias); userAlias = NULL;
    free(numPing); numPing = NULL;
    free(criterion); criterion = NULL;
    free(endServerDomain); endServerDomain = NULL;
    free(endServerAlias); endServerAlias = NULL;
    for (i = 0; i < relaySize; i++) {
        free(relayAlias[i]); relayAlias[i] = NULL;
        free(relayIP[i]); relayIP[i] = NULL;
        free(relayPort[i]); relayPort[i] = NULL;
    }
    free(relayAlias);relayAlias = NULL; 
    free(relayIP); relayIP = NULL;
    free(relayPort); relayPort = NULL;
    free(comTid); comTid = NULL;
    free(relayPingTid); relayPingTid = NULL;
    free(relayTraceTid); relayTraceTid = NULL;
    free(endPingTid); endPingTid = NULL;
    free(endTraceTid); endTraceTid = NULL;
    free(downTid); downTid = NULL;
    free(comArgs); comArgs = NULL;
    free(relayPingArgs); relayPingArgs = NULL;
    free(relayTraceArgs); relayTraceArgs = NULL;
    free(endPingArgs); endPingArgs = NULL;
    free(endTraceArgs); endTraceArgs = NULL;
    free(transferAvgRTT); transferAvgRTT = NULL;
    free(transferHops); transferHops = NULL;
    free(relayAvgRTT); relayAvgRTT = NULL;
    free(relayHops); relayHops = NULL;
    free(downArgs); downArgs = NULL;
    
    /*system("PAUSE");*/
    return 0;
}

/*Reads end_servers.txt and returns user selected end_server*/
char *ReadServers(char* sourceFile, char *userAlias) {
    FILE *fileServers = NULL;
    char *endServerDomain = NULL;
    int endServerIndex = -1;
    int serverSize = 0;
    char **serverDomain = NULL;
    char **serverAlias = NULL;
    char line[4*CHARBUFFER] = {'8'};
    char word[CHARBUFFER] = {'8'};
    int i = 0; int j = 0;
    
    /*Open end_servers.txt*/
    fileServers = fopen(sourceFile, "r");
    if (fileServers == NULL) {
        printf("Error opening %s\n", sourceFile);
        exit(-1);
    }
    
    /*Count serverSize*/
    serverSize = 0;
    i = 0;
    while (fgets(line, 4*CHARBUFFER, fileServers) != NULL) {
        serverSize++;
    }
    rewind(fileServers);
#if DEBUG
    printf("serverSize=%d\n", serverSize);
#endif
    
    /*Create server Arrays*/
    serverDomain = malloc(serverSize*sizeof(char*));
    serverAlias = malloc(serverSize*sizeof(char*));
    i = 0;
    while (fgets(line, 4*CHARBUFFER, fileServers) != NULL) {
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
    
    /*Find user selected endServerDomain*/
    i = 0;
    endServerIndex = -1;
    while((i < serverSize) && (endServerIndex == -1)) {
        if (strcmp(serverAlias[i], userAlias) == 0) {
            endServerIndex = i;
        }
        i++;
    }
    if (endServerIndex == -1) {
        printf("%s does not exist in %s\n", userAlias, sourceFile);
        exit(-1);
    }
    strcpy(word, serverDomain[endServerIndex]);
    endServerDomain = malloc(strlen(word)*sizeof(char) + 1);
    strcpy(endServerDomain, word);    
#if (DEBUG>1)
    puts("End Servers");
    for(i=0; i < serverSize; i++) {
        printf("%s L=%d, %s L=%d\n", serverDomain[i], strlen(serverDomain[i]), serverAlias[i], strlen(serverAlias[i]));
        for(j = 0; j < strlen(serverDomain[i]); j++) {
            printf("serverDomain[%d][%d]=%c\n", i, j, serverDomain[i][j]);
        }
        for(j = 0; j < strlen(serverAlias[i]); j++) {
            printf("serverAlias[%d][%d]=%c\n", i, j, serverAlias[i][j]);
        }
    }
#endif    
    /*Free*/
    for (i=0; i < serverSize; i++) {
        free(serverDomain[i]); serverDomain[i] = NULL;
        free(serverAlias[i]); serverAlias[i] = NULL;
    }
    free(serverDomain); serverDomain = NULL;
    free(serverAlias); serverAlias = NULL;
#if DEBUG
    puts("ReadServers Returning");
#endif
    return endServerDomain;
}

/*Reads relay_nodes.txt and returns number of relay_nodes*/
int ReadRelays(char *sourceFile, char ***relayAlias, char ***relayIP, char ***relayPort) {
    FILE *fileRelays = NULL;
    int relaySize = 0;
    char word[CHARBUFFER] = {'8'};
    char line[4*CHARBUFFER] = {'8'};
    int i = 0;    
    
    /*Open relay_nodes.txt*/
    fileRelays = fopen(sourceFile, "r");
    if (fileRelays == NULL) {
        printf("Error opening %s\n", sourceFile);
        exit(-1);
    }
    
    /*Count relaySize*/
    relaySize = 0;
    i = 0;
    while (fgets(line, 4*CHARBUFFER, fileRelays) != NULL) {
        relaySize++;
    }
    rewind(fileRelays);
#if DEBUG
    printf("relaySize=%d\n", relaySize);
#endif
    
    /*Create relay Arrays*/
    *relayAlias = malloc(relaySize*sizeof(char*));
    *relayIP = malloc(relaySize*sizeof(char*));
    *relayPort = malloc(relaySize*sizeof(char*));    
    i = 0;    
    while (fgets(line, 4*CHARBUFFER, fileRelays) != NULL) {
        /*store relayAlias*/
        strcpy(word, strtok(line, ", \r\n"));
        (*relayAlias)[i] = malloc(strlen(word)*sizeof(char) + 1);
        strcpy ((*relayAlias)[i], word);
#if DEBUG
        printf("RAlias[%d]=%s\n", i, (*relayAlias)[i]);
#endif
        /*store relayIP*/
        strcpy(word, strtok(NULL, ", \r\n"));
        (*relayIP)[i] = malloc(strlen(word)*sizeof(char) + 1);
        strcpy ((*relayIP)[i], word);
#if DEBUG
        printf("RIP[%d]=%s\n", i, (*relayIP)[i]);
#endif
        /*store relayPort*/
        strcpy(word, strtok(NULL, ", \r\n"));
        (*relayPort)[i] = malloc(strlen(word)*sizeof(char) + 1);
        strcpy ((*relayPort)[i], word);
#if DEBUG
        printf("RPort[%d]=%s\n", i, (*relayPort)[i]);
#endif
        i++;
    }
    fclose(fileRelays);
#if DEBUG
    puts("Returning ReadRelays");
#endif
    return relaySize;
}

/*Messages relay to Ping end_Server and created thread to wait for results*/
void *CommunicateRelay(void *comArgs) {
    int sock = -1;
    char *message = NULL;
    int messageL = 0;
    int bytesRec = 0;
    char recvBuf[FILEBUFFER] = {'8'};
    struct sockaddr_in ServAddr; /* server address */
     
    COMMUNICATIONARGS_T args = (COMMUNICATIONARGS_T)comArgs;
    char *endServerDomain = args->endServerDomain;
    char *numPing = args->numPing;
    char *relayIP = args->relayIP;
    char *relayPort = args->relayPort;
    float *relayServerAvgRTT = args->relayServerAvgRTT;
    int *relayServerHops = args->relayServerHops;

#if DEBUG
    printf("Args: endServerDomain=%s, numPing=%s, relayIp=%s, relayPort=%s\n", endServerDomain, numPing, relayIP, relayPort);
#endif        
        strcpy(recvBuf, "p ");
        strcat(recvBuf, endServerDomain);
        strcat(recvBuf, " ");
        strcat(recvBuf, numPing);
        messageL = strlen(recvBuf);
        message = malloc(messageL * sizeof (char) + 1); 
        strcpy(message, recvBuf);
#if DEBUG
        printf("relayIP=%s, relayPort=%u, message=%s, messageLength=%d\n", relayIP, (unsigned short)atoi(relayPort), message, messageL);
#endif
        /* Create a reliable, stream socket using TCP */
        if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
            printf("socket() failed\n");
            exit(-1);
        }
#if DEBUG
        printf("Sock=%d Created\n", sock);
#endif
        /* Construct the server address structure */
        memset(&ServAddr, 0, sizeof (ServAddr)); /* Zero out structure */
        ServAddr.sin_family = AF_INET; /* Internet address family */
        ServAddr.sin_addr.s_addr = inet_addr(relayIP); /* Relay_Server IP address */
        ServAddr.sin_port = htons((unsigned short)atoi(relayPort)); /* Relay_Server port */
        /* Establish the connection to the echo server */
        if (connect(sock, (struct sockaddr *) &ServAddr, sizeof (ServAddr)) < 0) {
            printf("Sock=%d connect() failed\n", sock);
            exit(-1);
        }
#if DEBUG
        printf("Sock=%d Connected\n", sock);
#endif
        if (send(sock, message, messageL, 0) != messageL) {
            printf("send() sent a different number of bytes than expected");
        }
#if DEBUG
        printf("Sock=%d Message Sent\n", sock);
#endif
    messageL = 0;
    /* Receive message from client */
    if ((bytesRec = recv(sock, recvBuf, FILEBUFFER - 1, 0)) < 0) {
        printf("(socket=%d) recv() failed\n", sock);
        exit(-1);
    }
    messageL += bytesRec;
    recvBuf[messageL] = '\0'; /* Terminate the string! */
#if DEBUG
        printf("Received: %s\n", recvBuf);
#endif
    *relayServerAvgRTT = atof(strtok(recvBuf, " \n"));
    *relayServerHops = atof(strtok(NULL, " \n"));
    
#if DEBUG
    printf("(socket=%d) ReceiveRelay: sock=%d, *relayServerAvgRTT=%f, *relayServerHops=%d\n", sock, sock, *relayServerAvgRTT, *relayServerHops);
    printf("(socket=%d) Returning\n", sock);
#endif
    close (sock);
    /*Free*/
    free(message);
    return NULL;
}

/*Ping server specified in pingArgs*/
void *Ping(void *pingArgs) {
    int AvgRTTOffset = 30; /*Constant*/
    FILE *filePing = NULL;
    char *file = NULL;
    char *cmdArgs = NULL;
    char *command = NULL;    
    char word[CHARBUFFER] = {'8'};
    char line[4*CHARBUFFER] = {'8'};
    
    PINGARGS_T args = (PINGARGS_T)pingArgs;
    float *avgRTT = args->resAvgRTT;
    char *address = args->address;
    char *numPing = args->numPing;
    
    /*Build file name*/
    sprintf(word, "%d", pthread_self());
    strcat(word, "-ping_res");
    file = malloc(strlen(word)*sizeof(char) + 1);
    strcpy(file, word);
    
    /*Build Ping arguments*/
    strcpy(word, "-c ");
    strcat(word, numPing);
    strcat(word, " ");
    cmdArgs = malloc(strlen(word)*sizeof(char) + 1);
    strcpy(cmdArgs, word);
    
    /*Build Ping command*/
    command = malloc((strlen("ping ")+strlen(cmdArgs)+strlen(address)+strlen(" >| ")+strlen(file))*sizeof(char) + 1);
    strcpy(command, "ping ");
    strcat(command, cmdArgs);
    strcat(command, address);
    strcat(command, " >| ");
    strcat(command, file);
    printf("%s\n", command);
    system(command);
    
    /*Extract avgRTT from file*/
    filePing = fopen(file, "r+");
    if (filePing == NULL) {
        printf("Error opening %s\n", file);
        exit(-1);
    }
    while (fgets(line, 4*CHARBUFFER, filePing) != NULL) {
    }
    *avgRTT = atof(&line[AvgRTTOffset]);
   
    /*Delete file*/
    fclose (filePing);
    if (remove(file) != 0) {
        printf("%s not deleted\n", file);
    }
    /*Free*/
    free(file); file = NULL;
    free(cmdArgs); cmdArgs = NULL;
    free(command); command = NULL;
#if DEBUG
    printf("Ping: %s, returns: %f\n", address, *avgRTT);
#endif
    return NULL;
}

/*Traceroute server specified in traceArgs*/
void *Traceroute(void *traceArgs) {    
    FILE *fileTrace = NULL;
    char *file = NULL;
    char *cmdArgs = NULL;
    char *command = NULL;    
    char word[CHARBUFFER] = {'8'};
    char line[4*CHARBUFFER] = {'8'};
    
    TRACEARGS_T args = (TRACEARGS_T)traceArgs;
    int *hops = args->resHops;
    char *address = args->address;
    
    /*Build file name*/
    sprintf(word, "%d", pthread_self());
    strcat(word, "-trace_res");
    file = malloc(strlen(word)*sizeof(char) + 1);
    strcpy(file, word);
    
    /*Build Trace arguments
    strcpy(word, );
    strcat(word, );
    strcat(word, " ");
    cmdArgs = malloc(strlen(word)*sizeof(char) + 1);
    strcpy(cmdArgs, word);*/
    
    command = malloc((strlen("traceroute ")+/*strlen(cmdArgs)+*/strlen(address)+strlen(" >| ")+strlen(file))*sizeof(char) + 1);
    strcpy(command, "traceroute ");
    /*strcat(command, cmdArgs);*/
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
    
    fclose (fileTrace);
    if (remove(file) != 0) {
        printf("%s not deleted\n", file);
    }
    /*Free*/
    free(file); file = NULL;
    free(cmdArgs); cmdArgs = NULL;
    free(command); command = NULL;
#if DEBUG
    printf("Traceroute: %s, returns: %d\n", address, *hops);
#endif
    return NULL;
}

/*Determine best relayIndex via AvgRTT (-1 if direct route to endServer is better), return 1 if there is tie*/
int BestAvgRTT(int *index, int relaySize, float serverAvgRTT, float relayAvgRTT[]) {
    float min = 0.0f;
    int tie = 0;
    int i = 0;

#if DEBUG
    printf("Args: *index=%d, relaySize=%d, serverRTT=%f, relayRTT[0]=%f\n", *index, relaySize, serverAvgRTT, relayAvgRTT[0]);
#endif
    *index = -1; /*Represents server-direct route*/
    min = serverAvgRTT;
    for (i = 0; i < relaySize; i++) {
        if (relayAvgRTT[i] < min) {
            min = relayAvgRTT[i];
            *index = i;
            tie = 0;
        } else if (relayAvgRTT[i] == min) {
            tie = 1;
        }
    }
    return tie;
}

/*Determine best relayIndex via Hops (-1 if direct route to endServer is better), return 1 if there is tie*/
int BestHops(int *index, int relaySize, int serverHops, int relayHops[]) {
    int min = 0;
    int tie = 0;
    int i = 0;
#if DEBUG
    printf("Args: *index=%d, relaySize=%d, serverHops=%d, relayHops[0]=%d\n", *index, relaySize, serverHops, relayHops[0]);
#endif
    *index = -1; /*Represents server-direct route*/
    min = serverHops;
    for (i = 0; i < relaySize; i++) {
        if (relayHops[i] < min) {
            min = relayHops[i];
            *index = i;
            tie = 0;
        } else if (relayHops[i] == min) {
            tie = 1;
        }
    }
    return tie;
}

/*Download file and count download time*/
void *DownloadFile(void *downArgs) {
    char *fileName = NULL;
    char *cmdArgs = NULL;
    char *command = NULL;
    clock_t begin;
    clock_t end;
    char line[4*CHARBUFFER] = {'8'};
    
    DOWNARGS_T args = (DOWNARGS_T)downArgs;
    char *url = args->url;
    float *downTime = args->downTime;
    
    strcpy(line, (strrchr(url, '/') + 1));
    fileName = malloc(strlen(line) * sizeof (char) + 1);
    strcpy(fileName, line);
#if DEBUG
    printf("FileName=%s\n", fileName);
#endif
    /*Build wget arguments*/
    strcpy(line, "-q -O ");
    strcat(line, fileName);
    strcat(line, " ");
    cmdArgs = malloc(strlen(line)*sizeof(char) + 1);
    strcpy(cmdArgs, line);
    
    /*Build wget command*/
    command = malloc((strlen("wget ")+strlen(cmdArgs)+strlen(url))*sizeof(char) + 1);
    strcpy(command, "wget ");
    strcat(command, cmdArgs);
    strcat(command,url);
    printf("%s\n", command);
    begin = clock();
    system(command);
    end = clock();
    *downTime = (float)(end - begin)/CLOCKS_PER_SEC;
    
    /*Free*/
    free(fileName); fileName = NULL;
    free(cmdArgs); cmdArgs = NULL;
    free(command); command = NULL;
#if DEBUG
    printf("DownloadFile: %s, returns: %f\n", url, *downTime);
#endif
    return NULL;
}

/*Download file from relay and count download time*/
void *DownloadRelay(void *downRArgs) {
    FILE *downFile = NULL;
    char *fileName = NULL;
    int fileSize = 0;
    int bytesRec = 0;
    int totalBytesRec = 0;
    int sock = -1;
    char *message = NULL;
    int messageL = 0;
    clock_t begin;
    clock_t end;
    char buf[3*FILEBUFFER] = {'8'};
    struct sockaddr_in ServAddr; /* server address */
     
    DOWNARGS_T args = (DOWNARGS_T) downRArgs;
    char *relayIP = args->relayIP;
    char *relayPort = args->relayPort;
    char *url = args->url;
    float *downTime = args->downTime;

    strcpy(buf, (strrchr(url, '/') + 1));
    fileName = malloc(strlen(buf) * sizeof (char) + 1);
    strcpy(fileName, buf);
#if DEBUG
    printf("FileName=%s\n", fileName);
#endif
    
    strcpy(buf, "d ");
    strcat(buf, url);
    messageL = strlen(buf);
    message = malloc(messageL * sizeof (char) + 1);
    strcpy(message, buf);
#if DEBUG
    printf("message=%s, messageLength=%d\n", message, messageL);
#endif
    /* Create a reliable, stream socket using TCP */
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        printf("socket() failed\n");
        exit(-1);
    }
#if DEBUG
    printf("Sock=%d Created\n", sock);
#endif
    /* Construct the server address structure */
    memset(&ServAddr, 0, sizeof (ServAddr)); /* Zero out structure */
    ServAddr.sin_family = AF_INET; /* Internet address family */
    ServAddr.sin_addr.s_addr = inet_addr(relayIP); /* Relay_Server IP address */
    ServAddr.sin_port = htons((unsigned short) atoi(relayPort)); /* Relay_Server port */
    /* Establish the connection to the echo server */
    if (connect(sock, (struct sockaddr *) &ServAddr, sizeof (ServAddr)) < 0) {
        printf("Sock=%d connect() failed\n", sock);
        exit(-1);
    }
#if DEBUG
    printf("Sock=%d Connected\n", sock);
#endif
    if (send(sock, message, messageL, 0) != messageL) {
        printf("(socket==%d)send() sent a different number of bytes than expected\n", sock);
    }
#if DEBUG
    printf("Sock=%d Message Sent\n", sock);
#endif
    /* Receive up to the buffer size (minus 1 to leave space for
       a null terminator) bytes from the sender */
    begin = clock();
    if ((bytesRec = recv(sock, buf, FILEBUFFER, 0)) <= 0) {
        printf("(socket=%d) recv() failed or connection closed prematurely\n", sock);
        exit(-1);
    }
    fileSize = atoi(buf);
    printf("FileSize= %d bytes\n", fileSize);
    
    downFile = fopen(fileName, "w");
    if (downFile == NULL) {
        printf("Failed to open %s\n", fileName);
    }
    totalBytesRec = 0;
    while (totalBytesRec < fileSize) {
        
        memset(buf ,0 , FILEBUFFER);  //clear the buffer
        /* Receive up to the buffer size */
        if ((bytesRec = recv(sock, buf, FILEBUFFER, 0)) <= 0) {
            printf("(socket=%d) recv() failed or connection closed prematurely\n", sock);
            fclose(downFile);
            exit(-1);
        }
        fwrite(buf, sizeof (char), bytesRec, downFile);
        totalBytesRec += bytesRec; /* Keep tally of total bytes */
        printf("Received %d / %d bytes\n", totalBytesRec, fileSize);
    }
    
    fclose(downFile);
    end = clock();
    *downTime = (float) (end - begin) / CLOCKS_PER_SEC;
#if DEBUG
    printf("(socket=%d) Returning\n", sock);
#endif
    close(sock);
    /*Free*/
    free(message); message = NULL;
    free(fileName); fileName = NULL;
    return NULL;
}
