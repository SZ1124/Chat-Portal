//Creator: Shao(Shawn) Zhang
//Student ID: 301571321

//Test:
//host 1: ./s-talk 6001 asb9838nu-a08 6060
//host 2: ./s-talk 6060 asb9838nu-e16 6001

#include "list.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

//The length of the message, can be altered
#define MSG_LENGTH 300

//***************Global Variables***************
//Threads initialization
static pthread_t KEYBOARD_THREAD, RECEIVE_THREAD, SCREEN_THREAD, SEND_THREAD;

//Mutex for receive and screen threads which are using the receiving message' list
static pthread_mutex_t receiveMutex=PTHREAD_MUTEX_INITIALIZER;
//Mutex for send and keyboard threads which are using the sending message' list
static pthread_mutex_t sendMutex=PTHREAD_MUTEX_INITIALIZER;
//Lists initialization
static List* receiveList;
static List* sendList;

//Datagram sockets ()
static int receiveSocket;
static int sendSocket;

struct sockaddr_in localAddress;
struct sockaddr_in remoteAddress;

//Boolean varaibles for thread wait
static bool EXIT=false; //shuting down threads
static bool receiving=false; //wait for receiving 
static bool sending=false; //wait for sending
//Condition varaibles for thread wait
//Tried using only receive and send conditions, it was too complicated to manage therefored added condition for all threads
static pthread_cond_t keyboardCond=PTHREAD_COND_INITIALIZER;
static pthread_cond_t receiveCond=PTHREAD_COND_INITIALIZER;
static pthread_cond_t screenCond=PTHREAD_COND_INITIALIZER;
static pthread_cond_t sendCond=PTHREAD_COND_INITIALIZER;



//***************Threads***************
//The keyboard input thread, on receipt of input, adds the input to the list of messages that need to be sent to the remote s-talk client.
void *keyboardThread() 
{
    while(1) 
    {
        //Check exit state
        if(EXIT) 
        {
            break;
        }

        //Lock mutex 
        pthread_mutex_lock(&sendMutex);

        //Wait until send thread is ready
        while(sending) 
        {
            pthread_cond_wait(&sendCond, &sendMutex);
        }

        //Get the message
        char msg[MSG_LENGTH];
        fgets(msg, MSG_LENGTH, stdin);
        char *msgToList=(char*)malloc(strlen(msg)+1);
        strcpy(msgToList, msg);

        //If user enters "!" to end conversation
        if(strcmp(msgToList, "!\n")==0)
        {
            //exit terminal
            socklen_t remoteAddressLength=sizeof(remoteAddress);
            int sendSuccess=sendto(sendSocket, msgToList, strlen(msgToList), 0, (struct sockaddr*)&remoteAddress, remoteAddressLength);

            if(sendSuccess==-1) 
            {
                perror("End Message failed");
                exit(1);
            }

            char* endMsg="******CHAT ENDED BY YOU******\n";
            fputs(endMsg, stdout);

            //set exit state to true
            EXIT=true;

            //shut down threads and set waits to true
            receiving=true;
            pthread_mutex_unlock(&receiveMutex);
            pthread_cond_signal(&receiveCond);
            sending=true;
            pthread_mutex_unlock(&receiveMutex);
            pthread_mutex_unlock(&sendMutex);
            pthread_cond_signal(&keyboardCond);
            pthread_cond_signal(&screenCond);
            pthread_cancel(RECEIVE_THREAD);

            //destroy mutex and condition variables
            pthread_mutex_destroy(&receiveMutex);
            pthread_mutex_destroy(&sendMutex);
            pthread_cond_destroy(&keyboardCond);
            pthread_cond_destroy(&receiveCond);
            pthread_cond_destroy(&screenCond);
            pthread_cond_destroy(&sendCond);
            close(receiveSocket);
            close(sendSocket);

            break;
        }
        //If conversation is live, append the message to the list and ready to be sent
        else
        {
            List_append(sendList, msgToList);
            sending=true;
        }

        //Unlock mutex
        pthread_mutex_unlock(&sendMutex);
        //Signal send thread
        pthread_cond_signal(&keyboardCond);
    }

    return NULL;
}

//The UDP input thread, on receipt of input from the remote s-talk client, will put the message onto the list of messages that need to be printed to the local screen. 
void *receiveThread() {
    while (1) 
    {
        //Check exit state
        if(EXIT)
        {
            break;
        }

        //Lock mutex
        pthread_mutex_lock(&receiveMutex);

        //Wait until receive is over
        while(receiving) 
        {
            pthread_cond_wait(&screenCond, &receiveMutex);
        }

        //Get the message
        char msg[MSG_LENGTH];
        socklen_t localAddressSize=sizeof(localAddress);
        int receiveSuccess=recvfrom(receiveSocket, msg, MSG_LENGTH, 0, (struct sockaddr*)&localAddress, &localAddressSize);

        if (receiveSuccess==-1) 
        {
            perror("Receive Message failed");
            exit(1);
        }

        msg[receiveSuccess]='\0';
        char *msgToList=(char*)malloc(MSG_LENGTH);
        strcpy(msgToList, msg);

        //If client enters "!" to end conversation
        if(strcmp(msg, "!\n")==0) 
        {
            char* endMsg="******CHAT ENDED BY CLIENT******\n";
            fputs(endMsg, stdout);

            //set exit state to true
            EXIT=true;

            //shut down threads and set waits to true
            receiving=true;
            pthread_mutex_unlock(&receiveMutex);
            pthread_cond_signal(&receiveCond);
            sending=true;
            pthread_mutex_unlock(&sendMutex);
            pthread_cond_signal(&keyboardCond);
            pthread_cancel(KEYBOARD_THREAD);
            
            //destroy mutex and condition variables
            pthread_mutex_destroy(&receiveMutex);
            pthread_mutex_destroy(&sendMutex);
            pthread_cond_destroy(&keyboardCond);
            pthread_cond_destroy(&receiveCond);
            pthread_cond_destroy(&screenCond);
            pthread_cond_destroy(&sendCond);
            close(receiveSocket);
            close(sendSocket);

            break;
        }
        //If conversation is live, append the message to the list and ready to receive more
        else
        {
            List_append(receiveList, msgToList);
            receiving=true;
        }

        //Unlock mutex
        pthread_mutex_unlock(&receiveMutex);
        //Signal screen thread
        pthread_cond_signal(&receiveCond);
    }

    return NULL;
}

//The screen output thread will take each message off this list and output it to the screen
void *screenThread() 
{
    while (1) 
    {
        //Check exit state
        if(EXIT) 
        {
            break;
        }

        //Lock mutex
        pthread_mutex_lock(&receiveMutex);

        //Wait until receive thread is ready
        while (!receiving) 
        {
            pthread_cond_wait(&receiveCond, &receiveMutex);
        }

        char* msg;
        //Print all the received message by accessing the output list
        while (List_count(receiveList)!=0) 
        {
            msg=(char*)List_trim(receiveList);

            fputs("Client: ", stdout);
            fputs(msg, stdout);
            free(msg);//memory leak
        }

        //Ready to receive
        receiving=false;
        //Unlock mutes
        pthread_mutex_unlock(&receiveMutex);
        //Signal receive thread
        pthread_cond_signal(&screenCond);
    }

    //No matter print or not cuz screen thread doesn't need to get activated all the time:
    //Ready to receive
    receiving=false;
    //Unlock mutex
    pthread_mutex_unlock(&receiveMutex);
    //Signal receive thread
    pthread_cond_signal(&screenCond);

    return NULL;
}

//The UDP output thread will take each message off this list and send it over the network to the remote client. 
void *sendThread() {
    while (1) 
    {
        //Lock the mutex first before thread accesses the list
        pthread_mutex_lock(&sendMutex);

        //Wait until keyboard thread is ready
        while(!sending) 
        {
            pthread_cond_wait(&keyboardCond, &sendMutex);
        }
        
        //Check exit state
        if (EXIT) 
        {
            break;
        }

        //Get the message
        char* msg=(char*) List_trim(sendList);
        socklen_t remoteAddressLength=sizeof(remoteAddress);
        int sendSuccess = sendto(sendSocket, msg, strlen(msg), 0, (struct sockaddr *) &remoteAddress, remoteAddressLength);

        if (sendSuccess==-1) 
        {
            perror("Send Message failed");
            exit(1);
        }
        free(msg);

        //Ready to send again
        sending=false;
        //Unlock Mutex
        pthread_mutex_unlock(&sendMutex);
        //Signal keyboard thread
        pthread_cond_signal(&sendCond);
    }
    
    return NULL;
}





//***************MAIN***************
int main(int arg, char** argv)
{   
    //check if client enter the right format
    if(arg!=4)
    {
        printf("Invalid Argument\n");
        exit(1);
    }

    //create list
    receiveList=List_create();
    sendList=List_create();

    //get the ports and addresses
    char *convert;
    int localPort=strtol(argv[1], &convert, 10);
    char* checkLocal=argv[1];
    char remoteIP[INET_ADDRSTRLEN];
    char remoteName[100];
    strcpy(remoteName, argv[2]);
    int remotePort=strtol(argv[3], &convert, 10);
    char* checkRemote=argv[3];

    //******LOCAL******
    //Setup host
    struct addrinfo localHints, *localServer;
    memset(&localHints, 0, sizeof(localHints));
    localHints.ai_family=AF_INET;
    localHints.ai_socktype=SOCK_DGRAM;
    localHints.ai_flags=AI_PASSIVE;
    localAddress.sin_family=AF_INET;
    localAddress.sin_port=htons(localPort);
    localAddress.sin_addr.s_addr=htonl(INADDR_ANY);
    
    int connectLocal=getaddrinfo(NULL, checkLocal, &localHints, &localServer);
    if(connectLocal!=0)
    {
        perror("***Local server connection failed***\n");
        exit(1);
    }
    else
    {
        printf("***Local server connection SUCCESS***\n");
    }

    //Set socket to datagram type
    receiveSocket=socket(AF_INET, SOCK_DGRAM, 0);
    if(receiveSocket==-1)
    {
        perror("***Receiving Socket failed to create***\n");
        exit(1);
    }
    else
    {
        printf("***Receiving Socket SUCCESS***\n");
    }

    //Bind sockets
    int checkBind=bind(receiveSocket, localServer->ai_addr, localServer->ai_addrlen);
    if(checkBind==-1)
    {
        perror("***Bind failed***\n");
        exit(1);
    }
    else
    {
        printf("***Bind SUCCESS***\n");
    }


    //******REMOTE******
    //Setup client
    struct addrinfo remoteHints, *remoteServer;
    memset(&remoteHints, 0, sizeof(remoteHints));
    remoteHints.ai_family=AF_INET;
    remoteHints.ai_socktype=SOCK_DGRAM;
    remoteAddress.sin_family=AF_INET;
    remoteAddress.sin_port=htons(remotePort);

    int connectRemote=getaddrinfo(remoteName, checkRemote, &remoteHints, &remoteServer);
    if (connectRemote!=0)
    {
        perror("***Remote server connection failed***\n");
        exit(1);
    }
    else
    {
        printf("***Remote Server connection SUCCESS***\n");
    }

    //Set socket to datagram type
    sendSocket=socket(AF_INET, SOCK_DGRAM, 0);
    if(sendSocket==-1)
    {
        perror("***Sending Socket failed to create***\n");
        exit(1);
    }
    else
    {
        printf("***Sending Socket Success***\n");
    }

    //Get the client's IP so we can establish with sockaddr_in remote address
    struct addrinfo* remoteServerCopy = remoteServer;
    void *temp;
    while (remoteServerCopy!=NULL) {

        temp=&(((struct sockaddr_in*)remoteServerCopy->ai_addr)->sin_addr);
        inet_ntop(remoteServerCopy->ai_family, temp, remoteIP, sizeof(remoteIP));
        remoteServerCopy=remoteServerCopy->ai_next;
    }
    inet_pton(AF_INET, remoteIP, &remoteAddress.sin_addr.s_addr);
    
    printf("\n******SIMPLE TALK ACTIVATED******\n\n");

    //Link up threads
    pthread_create(&KEYBOARD_THREAD, NULL, keyboardThread, NULL);
    pthread_create(&RECEIVE_THREAD, NULL, receiveThread, NULL);
    pthread_create(&SCREEN_THREAD, NULL, screenThread, NULL);
    pthread_create(&SEND_THREAD, NULL, sendThread, NULL);
    pthread_join(KEYBOARD_THREAD, NULL);
    pthread_join(RECEIVE_THREAD, NULL);
    pthread_join(SCREEN_THREAD, NULL);
    pthread_join(SEND_THREAD, NULL);

    //Free variables
    freeaddrinfo(localServer);
    freeaddrinfo(remoteServer);

    List_free(receiveList, NULL);
    List_free(sendList, NULL);

    close(receiveSocket);
    close(sendSocket);

    pthread_mutex_destroy(&receiveMutex);
    pthread_mutex_destroy(&sendMutex);
    pthread_cond_destroy(&keyboardCond);
    pthread_cond_destroy(&receiveCond);
    pthread_cond_destroy(&screenCond);
    pthread_cond_destroy(&sendCond);

    return 0;
}
