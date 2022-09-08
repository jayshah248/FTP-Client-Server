#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/ftp.h>
#include <pthread.h>

#define PORT 5555

/**
 * @brief Our Client is multithreaded.
 * 
 */

/**
 * @brief This is a thread function. This thread is called when the PORT command is sent to server.
 * This thread is for receiving data for the data transfer connection opened by the port command.
 * If the server has send a download reply this means that the client had requested the RETR command and server is ready to send the file. 
 * Client recieves the data byte by byte from client and then stores the data to the file opened to write once a new line character is encoutered and continues recieving the data till EOF.
 * Therefore client must create and open the file to write the incoming data into the file.
 * If the reply was anything but download then simply print the reply on standard output of the client.
 */

void *listenThread(void *p)
{
    int *socket;
    socket = p;
    char buffer[1024];
    while (1)
    {
        bzero(buffer, 1024);
        recv(*socket, buffer, 1024, 0);
        if (strncmp(buffer, "download", 8) == 0)
        {
            char *tokenOne, *tokenTwo;
            tokenOne = strtok(buffer, " \n");
            tokenTwo = strtok(NULL, " \n");
            FILE *fout = fopen(tokenTwo, "a");
            if(fout == NULL)
            {
                printf("\nCouldnt create file");
                fclose(fout);
                close(*socket);
                pthread_exit(NULL);
            }
            while (1)
            {
                int n = 0;
                int bytes;
                while (1)
                {
                    bytes = recv(*socket, &buffer[n], 1, 0);
                    if (bytes <= 0)
                    {
                        break;
                    }
                        
                    if (buffer[n] == '\n')
                    { 
                        buffer[n] = '\0';
                        break;
                    }

                    if (buffer[0] != '\r')
                    {
                        n++;
                    } 
                }
                if (bytes <= 0)
                    break;
                fprintf(fout, "%s\n", buffer);
            }
            bzero(buffer,sizeof(buffer));
            fclose(fout);                            
            close(*socket);
            *socket = -1;
            pthread_exit(NULL);
        }
        else
        {
           printf("\n%s", buffer);
        }
    }
}

/**
 * @brief This function will extract tokens i.e. seperate a string at space and new line characters and store these tokens in a pointer array. 
 * 
 * @param input input string
 * @param commands pointer array to store the tokens
 * @return int 
 */

int extractCommands(char *input, char *commands[])
{
    int cmdCount = 0;             // counter
    char *token;                  // varaible to store token
    token = strtok(input, " \n"); // seperate tokens based on delimiter ";"
    while (token != NULL)         // loop to extract all tokens
    {
        commands[cmdCount] = token;  // updating the commands array to store every command
        cmdCount++;                  // increment counter
        token = strtok(NULL, " \n"); // continue extracting tokens
    }
    return cmdCount; // returning total commands found
}

/**
 * @brief This is the main function.
 * Client has the know the IP and PORT of the server it needs to connect to and the server should already be open otherwise the connection will fail.
 * For the client side, we simply send the command to the server and print the response from the server on standard output.
 * However, there are a few special cases on our client implementation.
 * While sending the commands we check if the command sent was a PORT command. If it was then we open the port number send as argument in the PORT command.
 * We start listening for connections on this port from a seperate thread. Once the server connects to this port we start to receive data or send data.
 * If the server sends a reply "upload", we open, read and send the requested file using a while loop till end of file is encountered.
 * If the reply is 221 we close the connection and exit program.
 * If the server sends 220 reply we close the data connection. We also always close the data connection if a data transfer is finished.
 * Other that that, we simply print the reply on standard output.
 */

int main(int argc, char const *argv[])
{

    int clientSocket, ret;
    struct sockaddr_in serverAddr;
    char buffer[1024];
    char recieverBuffer[1024];

    int dataTransferDescriptor, dataTransferSocket;
    struct sockaddr_in dataTransferAddr, newAddr;
    socklen_t addr_size;
    int dataPortOpened = 0;
    pthread_t tid;

    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0)
    {
        printf("Error in creating client socket.\n");
        exit(1);
    }

    printf("Client Socket is created.\n");

    memset(&serverAddr, '\0', sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    ret = connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));

    if (ret < 0)
    {
        printf("Error in connection.\n");
        exit(1);
    }
    printf("Connected to Server.\n");

    char *commands[255];
    while (1)
    {
        sleep(1);
        printf("\nftp> ");
        fgets(buffer, sizeof(buffer), stdin);
        send(clientSocket, buffer, strlen(buffer) - 1, 0);
        char *portmsgidentifier;
        char *tokenOne, *tokenTwo;
        portmsgidentifier = buffer;

        if(dataTransferSocket < 0)
        {
            dataPortOpened = 0;
        }

        if (dataPortOpened == 0)
        {
            tokenOne = strtok(portmsgidentifier, " \n");
            tokenTwo = strtok(NULL, " \n");
            if (strcasecmp(tokenOne, "PORT") == 0)
            {
                int dport = atoi(tokenTwo);
                dataPortOpened = 1;
                dataTransferDescriptor = socket(AF_INET, SOCK_STREAM, 0);

                if (dataTransferDescriptor < 0)
                {
                    printf("Error in creating data transfer socket.\n");
                }
                else
                {
                    memset(&dataTransferAddr, '\0', sizeof(dataTransferAddr));
                    dataTransferAddr.sin_family = AF_INET;
                    dataTransferAddr.sin_port = htons(dport);
                    dataTransferAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

                    bind(dataTransferDescriptor, (struct sockaddr *)&dataTransferAddr, sizeof(dataTransferAddr));
                    listen(dataTransferDescriptor, 1);
                    dataTransferSocket = accept(dataTransferDescriptor, (struct sockaddr *)&newAddr, &addr_size);

                    pthread_create(&tid, NULL, listenThread, &dataTransferSocket);
                }
                tokenOne = NULL;
                tokenTwo = NULL;
            }
        }

        bzero(recieverBuffer, sizeof(recieverBuffer));
        if (recv(clientSocket, recieverBuffer, 1024, 0) < 0)
        {
            printf("\nError in Recieving message.");
        }
        else
        {

            commands[0] = NULL;
            char message[255];
            strcpy(message, recieverBuffer);
            extractCommands(recieverBuffer, commands);
            if (strcasecmp(commands[0], "221") == 0)
            {
                printf("\n221 Service closing control connection. Logged out if appropriate.\n");
                close(clientSocket);
                exit(1);
            }

            else if(strcasecmp(commands[0], "220") == 0)
            {
                printf("\n%s",message);
                pthread_cancel(tid);
                close(dataTransferSocket);
            }

            else if (strcasecmp(commands[0], "upload") == 0)
            {
                printf("\n150 File status okay; about to open data connection. %s", commands[1]);
                if (fopen(commands[1], "r") == NULL)
                {
                    printf("\nFile Doesnt Exist.");
                }
                else
                {
                    FILE *fin = fopen(commands[1], "r"); 
                    char temp_buffer[80];
                    while (!feof(fin))
                    {
                        fgets(temp_buffer, 80, fin);
                        sprintf(buffer, "%s", temp_buffer);
                        send(dataTransferSocket, buffer, strlen(buffer), 0);
                    }
                    close(dataTransferSocket);                                    
                    pthread_cancel(tid);
                    dataPortOpened = 0;
                    fclose(fin);
                    recv(clientSocket, recieverBuffer, 1024, 0);
                    printf("\n%s", recieverBuffer);
                }
            }
            else
            {
                printf("\n%s\n", message);
            }
            bzero(recieverBuffer, sizeof(recieverBuffer));
        }
    }
    close(clientSocket);
    return 0;
}