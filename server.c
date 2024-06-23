#define _WIN32_WINNT 0x0600

#undef UNICODE

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <process.h>
#include <conio.h>

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "3490"

void WINAPI AcceptThread(LPVOID lpData);
void WINAPI ClientThread(LPVOID lpData);

volatile int g_bServerRunning = 1;  // Global flag

int __cdecl main(void) 
{
    WSADATA wsaData;
    int iResult;

    SOCKET ListenSocket = INVALID_SOCKET;

    struct addrinfo *result = NULL;
    struct addrinfo hints;

    int iSendResult;
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    
    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if ( iResult != 0 ) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Print IP address and port
    char ipstr[INET_ADDRSTRLEN];
    struct sockaddr_in *addr = (struct sockaddr_in *)result->ai_addr;
    inet_ntop(AF_INET, &(addr->sin_addr), ipstr, INET_ADDRSTRLEN);
    printf("[server] Listening on %s at port %s\n", ipstr, DEFAULT_PORT);

    // Create a SOCKET for the server to listen for client connections.
    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        printf("socket failed with error: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Setup the TCP listening socket
    iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        printf("bind failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }
    
    freeaddrinfo(result);

    iResult = listen(ListenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        printf("listen failed with error: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    // Accept connections
    HANDLE hAcceptThread = (HANDLE)_beginthread(AcceptThread, 0, &ListenSocket);
    if (hAcceptThread == NULL) {
        printf("Unable to create thread...\n");
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    printf("Press 'q' to shut down the server...\n");
    while (getch() != 'q') { }
    g_bServerRunning = 0;
    printf("[server] Quitting...\n");

    WaitForSingleObject(hAcceptThread, INFINITE);
    // End of the thread

    // cleanup
    printf("[server] Shutting down...\n");
    CloseHandle(hAcceptThread);
    closesocket(ListenSocket);
    WSACleanup();

    printf("[server] Sockets cleaned...\n");

    return 0;
}

void AcceptThread(void *arg) {

    SOCKET ListenSocket = *(SOCKET*)arg;
    SOCKET ClientSocket = INVALID_SOCKET;

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    fd_set readFd;
    
    while(g_bServerRunning) {

        FD_ZERO(&readFd);
        FD_SET(ListenSocket, &readFd);
        int result = select(ListenSocket+1, &readFd, NULL, NULL, &tv);
        if(result == SOCKET_ERROR) {
            printf("Error on select: %d\n", WSAGetLastError());
            break;
        }else if(result == 0) {
            continue;
        }

        struct sockaddr_in clientAddr;
        socklen_t sin_size = sizeof clientAddr;
        ClientSocket = accept(ListenSocket, (struct sockaddr*)&clientAddr, &sin_size);
        printf("Got accepted!\n");
        if (ClientSocket == INVALID_SOCKET) {
            printf("accept failed with error: %d\n", WSAGetLastError());
            continue;
        }

        // Start the client thread
        HANDLE hClientThread = (HANDLE)_beginthread(ClientThread, 0, &ClientSocket);
        if (hClientThread == NULL) {
            printf("Unable to create thread...\n");
            closesocket(ClientSocket);
        }
    }

    _endthread();
    return;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


void WINAPI ClientThread(void *arg) {
    SOCKET fdsocket = *((SOCKET*)arg);
    char buffer[1024];
    int bytes_received;

    struct sockaddr_storage clientInfo;
    int len = sizeof(clientInfo);
    getpeername(fdsocket, (struct sockaddr*)&clientInfo, &len);

    char clientIP[INET6_ADDRSTRLEN];
    inet_ntop(
        clientInfo.ss_family, 
        get_in_addr((struct sockaddr*)&clientInfo), 
        clientIP, sizeof(clientIP)
    );
    printf("[server] Client connected from: %s\n", clientIP);
    
    while ((bytes_received = recv(fdsocket, buffer, sizeof(buffer), 0)) > 0) {
        buffer[bytes_received] = '\0';
        // printf("Bytes received: %d\n", bytes_received);
        printf("%s says: %s\n", clientIP, buffer);
    }

    if(bytes_received == 0) {
        printf("[server] Client %s disconnected\n", clientIP);
    } else {
        printf("[error] Client %s disconnected with error: %d\n", clientIP, WSAGetLastError());
    }
    
    closesocket(fdsocket);
    _endthread();
}
