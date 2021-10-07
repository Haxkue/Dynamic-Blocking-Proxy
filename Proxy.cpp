// FILE NAME: Proxy.cpp
// AUTHOR TYLER TRAN (30088007)
// CLASS: CPSC 441 - Computer Networks
// SUBMISSION DATE: OCTOBER 1, 2021

/**
 * ASSIGNMENT 1
 * INSTRUCTIONS ON HOW TO USE THIS PROXY CAN BE FOUND IN Assignment 1.pdf
 * ENABLE CONTENT BLOCKING BELOW
 */

// LIBRARIES USED
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <unordered_set>

//=============================================
//CONTENT BLOCKING | 1 TO ENABLE, 0 TO DISABLE
//=============================================
#define BLOCK_CONTENT 0


// MACROS
#define MYPORT 6666
#define BACKLOG 5
#define MAX_SEND_SIZE 2048
#define MAX_BLOCK_SIZE 4096
#define CHUNK_SIZE 2048

// NAMESPACE DECLARATION
using std::string;
using std::cout;
using std::endl;
using std::unordered_set;

// FUNCTION HEADERS
void checkError(int, const char*);
void redirectToError(int &, const char*);
int isBlocked(const string&);
void updateBlockListFile();
void readFromBlockListFile();
string receiveAsString(int);

// GLOBAL BLOCKLIST SET
unordered_set<string> blockList;

// FILE POINTER FOR EXTERNAL BLOCKLIST FILE
FILE *blockListFile;

// PROGRAM MAIN
int main(int argc, char* argv[])
{

    // SERVERSIDE SERVERSIDE SETUP
    sockaddr_in myAddress, incomingAddress;
    int mySocketDesc, incomingSocketDesc;

    // ESTABLISHING PROXY SOCKET
    checkError(mySocketDesc = socket(PF_INET, SOCK_STREAM, 0),"proxy socket initialization");
    puts("proxy socket setup success");

    // SETTING UP MY ADDRESS (PREP FOR BIND)
    myAddress.sin_addr.s_addr = INADDR_ANY;
    myAddress.sin_family = AF_INET;
    myAddress.sin_port = htons(MYPORT);
    memset(&myAddress.sin_zero,'\0',8);

    // SET THE SOCKET OPTION SO THAT THE ADDRESS AND PORT IS REUSABLE
    int temp = 1;
    checkError(setsockopt(mySocketDesc,SOL_SOCKET,SO_REUSEADDR,&temp,sizeof(int)),"setsockopt");

    // BINDING SOCKET SO IT IS ASSOCIATED WITH PORT ON LOCAL MACHINE
    checkError(bind(mySocketDesc, (sockaddr *)&myAddress, sizeof(sockaddr)),"bind");
    puts("proxy socket bind success");

    // LISTENING TO INCOMING SOCKET CONNECTIONS
    checkError(listen(mySocketDesc, BACKLOG)==-1,"listen");
    puts("proxy listening...");
    
    while(1) // PRIMARY ACCEPT LOOP
    {
        // ACCEPTING EXTERNAL SOCKET CONNECTION
        socklen_t socketAddressSize = sizeof(sockaddr_in);
        checkError(incomingSocketDesc = accept(mySocketDesc, (sockaddr *)&incomingAddress, &socketAddressSize),"accept");
        cout << "Connection accepted from: " << inet_ntoa(incomingAddress.sin_addr) << endl;

        if(!fork()) // CHILD PROCESS TO HANDLE REQUEST
        {
            close(mySocketDesc); // CHILD SOCKET DOES NOT NEED TO LISTEN TO FURTHER REQUEST (HANDLED BY PARENT)
        
            // SETTING UP HOST ADDRESS
            int hostSocketDesc;
            sockaddr_in hostAddress;
            char sendData[MAX_SEND_SIZE] = {};

            // INITIALIZING HOST SOCKET
            checkError(hostSocketDesc = socket(PF_INET,SOCK_STREAM,0),"host socket initialization");

            // RECEIVING INPUT FROM USER
            checkError(recv(incomingSocketDesc,sendData,MAX_SEND_SIZE,0),"receive");

            // CONVERTS sendData TO STRING OBJECT
            string sendString(sendData);

            // UPDATES blockList ACCORDING TO BLOCKLIST FILE
            readFromBlockListFile();

            // DYNAMIC BLOCKING LOGIC (TELNET INTO PROXY TO BLOCK/UNBLOCK)
            bool unblocking;
            if(sendString.find("http")==string::npos && ((unblocking = sendString.find("unblock ")!=string::npos) || sendString.find("block ")!=string::npos))
            {
                sendString = sendString.substr(sendString.find_first_of(' ')+1);
                size_t cutoff;

                // PARSES EACH WORD OF FOLLOWING BLOCK/UNBLOCK COMMAND
                while((cutoff = sendString.find_first_of("\r "))!=string::npos)
                {
                    string blockedWord = sendString.substr(0,cutoff);
                    if(unblocking) // UNBLOCKS WORDS
                    {
                        if(blockedWord.compare("ALL")==0)
                            blockList.clear();
                        else
                            blockList.erase(blockedWord);
                    }
                    else // BLOCKS WORDS
                        blockList.insert(blockedWord);
                    sendString = sendString.substr(cutoff+1);
                }

                // DISPLAY UPDATED BLOCKLIST TO TERMINAL
                cout << "====UPDATED BLOCKLIST====" << endl;
                for(string blockedWord : blockList)
                {
                    cout << blockedWord << '\t';
                }
                cout << endl;

                // UPLOAD NEW BLOCKLIST TO BlockList.txt FILE
                updateBlockListFile();
                exit(0);
            }

            // PARSING HOST NAME
            int start;
            if((start = sendString.find_first_of("http://")+7)<=6)
            {
                puts("Error detecting URL");
                exit(1);
            }
            string hostName = sendString.substr(start,sendString.find_first_of('/',start)-start);

            // OUTPUTTING REQUEST MESSAGE TO TERMINAL
            cout << "\n===REQUEST MESSAGE===\n" << sendString << endl;

            // GETTING HOST BY NAME
            hostent *he;
            if((he = gethostbyname(hostName.c_str())) == NULL)
            {
                herror("gethostbyname");
                exit(1);
            }

            // INITIALZING VALUES OF SOCKET ADDRESS STRUCTURE
            hostAddress.sin_addr = *((in_addr*)he->h_addr);
            hostAddress.sin_family = AF_INET;
            hostAddress.sin_port = htons(80);
            memset(&hostAddress.sin_zero,'\0',8);

            // CONNECT TO HOST
            checkError(connect(hostSocketDesc,(sockaddr *)&hostAddress,sizeof(sockaddr)),"connecting to host");
            cout << "Connecting to: " << inet_ntoa(hostAddress.sin_addr) << endl;

            // SENDING MESSAGE TO HOST
            checkError(send(hostSocketDesc,sendData,MAX_SEND_SIZE,0),"sending to host");

            // RECEIVING HTML FROM HOST SOCKET
            string received = receiveAsString(hostSocketDesc);

            // BLOCKS URL
            if(isBlocked(sendString))
            {
                redirectToError(incomingSocketDesc,"ErrorPageURL.html");
            }

            // BLOCKS CONTENT (OPTIONAL)
            #if BLOCK_CONTENT
            if(received.find("Content-Type: text/html;")!=string::npos && isBlocked(received))
            {
                redirectToError(incomingSocketDesc,"ErrorPageContent.html");
            }
            #endif

            // RELAYING HTML BACK TO INCOMING SOCKET
            checkError(send(incomingSocketDesc,received.c_str(),received.length(),0),"sending back to client");

            cout << "===RELAYED WEBSERVER RESPONSE | CLOSING SOCKET ===" << endl;
            exit(0); // CHILD SOCKET ENDS
        }
        close(incomingSocketDesc); // CLOSING READ SOCKET FROM PARENT SOCKET
    }
    return 0;
}

// CHECKS IF INPUT IS -1, PRINTS ERROR MESSAGE TO TERMINAL AND ENDS PROCCESS
void checkError(int a, const char* s)
{
    if(a==-1)
    {
        perror(s);
        exit(1);
    }
}

// RECEIVES FROM PORT AS STRING OBJECT
string receiveAsString(int hostSockDesc)
{
    string builder;
    char chunk[CHUNK_SIZE];
    int bytesReceived;
    while(1)
    {
        memset(chunk, 0, CHUNK_SIZE);
        bytesReceived = recv(hostSockDesc, chunk, CHUNK_SIZE,0);
        if(bytesReceived == 0)
        {
            break;
        }
        else if(bytesReceived == -1)
        {
            perror("Receiving HTML");
            exit(1);
        }
        else
        {
            builder.append(chunk,bytesReceived);
        }
    }
    return builder;
}

// CHECKS IF CONTENT IS BLOCKED BY MATCHING WITH GLOBAL BLOCKLIST
int isBlocked(const string &s)
{
    for(string blocked : blockList)
    {
        if(s.find(blocked)!=string::npos)
        {
            cout << "Request BLOCKED due to containing: " << blocked << endl;
            return 1;
        }
    }
    return 0;
}

// REDIRECTS TO ERRORPAGE (URL OR CONTENT SPECIFIED)
void redirectToError(int &incSocDesc, const char* HTML)
{
    int fd = open(HTML, O_RDONLY), bytesRead;
    string html;
    char chunk[CHUNK_SIZE];
    while((bytesRead = read(fd,chunk,CHUNK_SIZE))>0)
    {
        html.append(chunk,bytesRead);
    }
    
    send(incSocDesc, html.c_str(), html.length(),0);
    exit(0);
}

// UPDATES BLOCK LIST FILE TO MATCH BLOCK LIST SET IN PROGRAM
void updateBlockListFile()
{
    blockListFile = fopen("BlockList.txt", "w");
    for(string blocked : blockList)
    {
        fputs((blocked+'\n').c_str(),blockListFile);
    }
    fclose(blockListFile);
}

// UPDATES BLOCK LIST SET TO MATCH BLOCK LIST FILE IN PROGRAM
void readFromBlockListFile()
{
    blockList.clear();
    blockListFile = fopen("BlockList.txt","r");
    char chunk[CHUNK_SIZE];
    while(fgets(chunk, CHUNK_SIZE, blockListFile)!=NULL)
    {
        chunk[strcspn(chunk,"\n")] = '\0';
        blockList.insert(string(chunk));
    }
    fclose(blockListFile);
}