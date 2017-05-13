
#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include "ThreadStructs.h"

#define DEBUG 1
#define CHARBUFFER 32
#define FILEBUFFER 1024
#define MAXCONNECTIONS 5


/*Ping server specified in pingArgs*/
void *Ping(void *pingArgs);
/*Traceroute server specified in traceArgs*/
void *Traceroute(void *traceArgs);
/*Download file and count download time*/
void *DownloadFile(void *downArgs);
/*Sends message to clientSock, returns -1 on error*/
int MessageClient(int clntSock, char *message);
/*Send file to client, returns -1 on error*/
int SendFile(int clientSock, char *fileName);


int main(int argc, char *argv[]) {
    pthread_t *endPingTid = NULL;
    pthread_t *endTraceTid = NULL;
    pthread_t *downTid = NULL;
    pthread_mutex_t lock;
    int err = -1;
    PINGARGS_T endPingArgs = NULL;
    TRACEARGS_T endTraceArgs = NULL;
    DOWNARGS_T downArgs = NULL;
    
    int serverSock = -1;
    int clientSock = -1;
    unsigned short relayPort;
    char *endServerDomain = NULL;
    char *numPing = NULL;
    char *timeout = NULL;
    char *maxHops = NULL;
    char *message = NULL;
    int messageL = 0;
    int bytesRec = 0;
    float endServerAvgRTT = 0.0f;
    int endServerHops = 0;
    char *fileName = NULL;
    char *downloadURL = NULL;
    float downTime;
    
    struct sockaddr_in ServAddr; /* Local address */
    struct sockaddr_in ClntAddr; /* Client address */
    unsigned int clntLen; /* Length of client address data structure */    

    char word[CHARBUFFER] = {'8'};
    char url[5*CHARBUFFER] = {'8'};
    char recvBuf[FILEBUFFER] = {'8'};

    if (argc != 2) /* Test for correct number of arguments */ {
        fprintf(stderr, "Usage:  %s <Server Port>\n", argv[0]);
        exit(-1);
    }

    relayPort = (unsigned int) atoi(argv[1]); /* First arg:  local port */
    /* Create socket for incoming connections */
    if ((serverSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        printf("socket() failed\n");
        exit(-1);
    }

    /* Construct local address structure */
    memset(&ServAddr, 0, sizeof (ServAddr)); /* Zero out structure */
    ServAddr.sin_family = AF_INET; /* Internet address family */
    ServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    ServAddr.sin_port = htons(relayPort); /* Local port */

    /* Bind to the local address */
    if (bind(serverSock, (struct sockaddr *) &ServAddr, sizeof (ServAddr)) < 0) {
        printf("bind() failed\n");
        exit(-1);
    }

    /* Mark the socket so it will listen for incoming connections */
    if (listen(serverSock, MAXCONNECTIONS) < 0) {
        printf("listen() failed\n");
        exit(-1);
    }

    printf("Listening on Port %u for incoming messages\n", relayPort);
    for (;;) { /* Run forever */
        /* Set the size of the in-out parameter */
        clntLen = sizeof (ClntAddr);

        /* Wait for a client to connect */
        if ((clientSock = accept(serverSock, (struct sockaddr *) &ClntAddr, &clntLen)) < 0) {
            printf("accept() failed\n");
            exit(-1);
        }
        
        /* clientSock is connected to a client! */
        printf("Handling client %s\n", inet_ntoa(ClntAddr.sin_addr));
        messageL = 0;
        /* Receive message from client */
        if ((bytesRec = recv(clientSock, recvBuf, (FILEBUFFER) - 1, 0)) < 0) {
            printf("recv() failed\n");
            exit(-1);
        }
        messageL += bytesRec;
        recvBuf[messageL] = '\0'; /* Terminate the string! */
#if DEBUG
        printf("Received: %s\n", recvBuf);
#endif        
        if (recvBuf[0] == 'p') {
#if DEBUG
            printf("recvBuf=%s\n", recvBuf);
#endif
            strtok(recvBuf, " \n");
            strcpy(word, strtok(NULL, " \n"));
            endServerDomain = malloc(strlen(word) * sizeof (char) + 1);
            strcpy(endServerDomain, word);
            strcpy(word, strtok(NULL, " \n"));
            numPing = malloc(strlen(word) * sizeof (char) + 1);
            strcpy(numPing, word);
            strcpy(word, strtok(NULL, " \n"));
            timeout = malloc(strlen(word) * sizeof (char) + 1);
            strcpy(timeout, word);
            strcpy(word, strtok(NULL, " \n"));
            maxHops = malloc(strlen(word) * sizeof (char) + 1);
            strcpy(maxHops, word);
#if DEBUG
            printf("endServerDomain=%s, numPing=%s, timeout=%s, maxHops=%s pinging and tracerouting end_server...\n", endServerDomain, numPing, timeout, maxHops);
#endif
            if (pthread_mutex_init(&lock, NULL) != 0) {
                printf("ping-trace endServer: mutex init failed\n");
                exit(-1);
            }
            /*endServer ping thread*/
            endServerAvgRTT = -1;
            endPingArgs = malloc(sizeof (PINGARGS_S));
            endPingArgs->resAvgRTT = &endServerAvgRTT;
            endPingArgs->address = endServerDomain;
            endPingArgs->numPing = numPing;
            endPingArgs->timeout = timeout;
            endPingTid = malloc(sizeof (pthread_t));
            err = pthread_create(endPingTid, NULL, &Ping, endPingArgs);
            if (err != 0) {
                printf("ping endServer: can't create thread :[%s]\n", strerror(err));
                exit(-1);
            }
            pthread_join(*endPingTid, NULL); /*COMMENT*/

            /*endServer trace thread*/
            endServerHops = -1;
            endTraceArgs = malloc(sizeof (TRACEARGS_S));
            endTraceArgs->resHops = &endServerHops;
            endTraceArgs->address = endServerDomain;
            endTraceArgs->hops = maxHops;
            endTraceArgs->timeout = timeout;
            endTraceTid = malloc(sizeof (pthread_t));
            err = pthread_create(endTraceTid, NULL, &Traceroute, endTraceArgs);
            if (err != 0) {
                printf("trace endServer: can't create thread :[%s]\n", strerror(err));
                exit(-1);
            }
            /*pthread_join(*endTraceTid, NULL);*/
            pthread_join(*endPingTid, NULL);
            pthread_join(*endTraceTid, NULL);
            pthread_mutex_destroy(&lock);
            
            /* Send ping and trace to client*/
            sprintf(word, "%f %d", endServerAvgRTT, endServerHops);
            message = malloc(strlen(word)*sizeof(char) + 1);
            strcpy(message, word);
            MessageClient(clientSock, message);
        } else if (recvBuf[0] == 'd') {
            strtok(recvBuf, " \n");
            strcpy(url, strtok(NULL, " \n"));
            downloadURL = malloc(strlen(url) * sizeof (char) + 1);
            strcpy(downloadURL, url);
            strcpy(url, strtok(NULL, " \n"));
            timeout = malloc(strlen(url) * sizeof (char) + 1);
            strcpy(timeout, url);
#if DEBUG
            printf("downloadURL= %s\n", downloadURL);
#endif
            strcpy(url, (strrchr(downloadURL, '/') + 1));
            fileName = malloc(strlen(url) * sizeof (char) + 1);
            strcpy(fileName, url);
            
            /*Download Locally*/
            downArgs = malloc(sizeof (DOWNARGS_S));
            downArgs->relayIP = NULL;
            downArgs->relayPort = NULL;
            downArgs->url = downloadURL;
            downArgs->timeout = timeout;
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
            
            /*Send file to client*/
            printf("Sending %s to client\n", fileName);
            SendFile(clientSock, fileName);
            printf("%s was succesfully sent\n", fileName);
        } else {
            printf("Received wrong message from client\n");
            MessageClient(clientSock, "ERROR");
        }

        free(endServerDomain); endServerDomain = NULL;
        free(numPing); numPing = NULL;
        free(timeout); timeout = NULL;
        free(maxHops); maxHops = NULL;
        free(endPingArgs); endPingArgs = NULL;
        free(endTraceArgs); endTraceArgs = NULL;
        free(endPingTid); endPingTid = NULL;
        free(endTraceTid); endTraceTid = NULL;
        close(clientSock); /* Close client socket */
    }
    /* NOT REACHED */
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
    char *timeout = args->timeout;
    
    /*Build file name*/
    sprintf(word, "%d", pthread_self());
    strcat(word, "-ping_res");
    file = malloc(strlen(word)*sizeof(char) + 1);
    strcpy(file, word);
    
    /*Build Ping arguments*/
    strcpy(word, "-c ");
    strcat(word, numPing);
    strcat(word, " -W ");
    strcat(word, timeout);
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
    if(strstr(line, "rtt min/avg/max/mdev") == NULL) {
        printf("%s failed\n", command);
        exit(-1);
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
    char line[8*CHARBUFFER] = {'8'};
    char *check1 = NULL; 
    char *check2 = NULL;
    char *ptr = NULL;
    int i = 0;
    
    TRACEARGS_T args = (TRACEARGS_T)traceArgs;
    int *hops = args->resHops;
    char *address = args->address;
    char *maxHops = args->hops;
    char *timeout = args->timeout;
    
    /*Build file name*/
    sprintf(word, "%d", pthread_self());
    strcat(word, "-trace_res");
    file = malloc(strlen(word)*sizeof(char) + 1);
    strcpy(file, word);
    
    /*Build Trace arguments*/
    strcpy(word, "-m ");
    strcat(word, maxHops);
    /*strcat(word, " -w ");
    strcat(word, timeout);*/
    strcat(word, " ");
    cmdArgs = malloc(strlen(word)*sizeof(char) + 1);
    strcpy(cmdArgs, word);
    
    command = malloc((strlen("traceroute ")+strlen(cmdArgs)+strlen(address)+strlen(" >| ")+strlen(file))*sizeof(char) + 1);
    strcpy(command, "traceroute ");
    strcat(command, cmdArgs);
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
    fgets(line, 8*CHARBUFFER, fileTrace);
    check1 = strchr(line, '(');
    check2 = strchr(line, ')');
    i = 0;
    for (ptr = check1; ptr <= check2; ptr++) {
        word[i] = *ptr;
        i++;
    }
    word[i] = '\0';
    while (fgets(line, 8*CHARBUFFER, fileTrace) != NULL) {
    }
    if(strstr(line, word) == NULL) {
        printf("%s failed\n", command);
        exit(-1);
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

/*Download file and count download time*/
void *DownloadFile(void *downArgs) {
    FILE *fileCheck = NULL;
    char *fileName = NULL;
    char *cmdArgs = NULL;
    char *command = NULL;
    clock_t begin;
    clock_t end;
    char line[4*CHARBUFFER] = {'8'};
    
    DOWNARGS_T args = (DOWNARGS_T)downArgs;
    char *url = args->url;
    char *timeout = args->timeout;
    float *downTime = args->downTime;
    
    strcpy(line, (strrchr(url, '/') + 1));
    fileName = malloc(strlen(line) * sizeof (char) + 1);
    strcpy(fileName, line);
#if DEBUG
    printf("FileName=%s\n", fileName);
#endif
    /*Build wget arguments*/
    strcpy(line, "-q -T ");
    strcat(line, timeout);
    strcat(line, " -O ");
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
    if (fileCheck = fopen(fileName, "r")) {
        fclose(fileCheck);
    } else {
        printf("File does not exist, %s failed\n", command);
        exit(-1);
    }
    
    /*Free*/
    free(fileName); fileName = NULL;
    free(cmdArgs); cmdArgs = NULL;
    free(command); command = NULL;
#if DEBUG
    printf("DownloadFile: %s, returns: %f\n", url, *downTime);
#endif
    return NULL;
}

/*Sends message to clientSock, returns -1 on error*/
int MessageClient(int clientSock, char *message) {
    int messageL = strlen(message);

    if (send(clientSock, message, messageL, 0) != messageL) {
        printf("MessageClient: send() failed\n");
        return -1;
    }
#if DEBUG
    printf("MessageClient: Sent to client: %s\n", message);
#endif
    return 0;
}

/*Send file to client, returns -1 on error*/
int SendFile(int clientSock, char *fileName) {
    int fileid;
    off_t offset = 0;
    int bytesLeft = 0;
    int bytesSent = 0;
    char fileSize[256];
    struct stat file_stat;
    
    fileid = open(fileName, O_RDONLY);
    if (fileid == -1) {
        printf("Error opening %s\n", fileName);
        return -1;
    }
    if (fstat(fileid, &file_stat) < 0) {
        printf("Error fstat\n");
        return -1;
    }
    printf("File Size: %d bytes\n", file_stat.st_size);
    sprintf(fileSize, "%d", file_stat.st_size);
    bytesSent = send(clientSock, fileSize, sizeof (fileSize), 0);
    if (bytesSent < 0) {
        printf("Error on sending file Size\n");
        return -1;
    }
#if DEBUG
    printf("Relay_node sent %d bytes for the size\n", bytesSent);
#endif

    offset = 0;
    bytesLeft = file_stat.st_size;
    /* Sending file data */
    while (((bytesSent = sendfile(clientSock, fileid, &offset, FILEBUFFER)) > 0) && (bytesLeft > 0)) {
        bytesLeft -= bytesSent;
        printf("Relay sent %d bytes, offset is now : %d and remaining bytes = %d\n", bytesSent, offset, bytesLeft);
        usleep(5);
    }    
    return 0;
}



