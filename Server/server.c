#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/ftp.h>
#include <dirent.h>
#include <ftw.h>


#define PORT 5555

/**
 * @brief This function will extract tokens i.e. seperate a string at space and new line characters and store these tokens in a pointer array. 
 * 
 * @param input input string
 * @param commands pointer array to store the tokens
 * @return int 
 */
int extractCommands(char *input, char *commands[])
{
    int cmdCount = 0;             
    char *token;                  
    token = strtok(input, " \n"); 
    while (token != NULL)        
    {
        commands[cmdCount] = token;  
        cmdCount++;                 
        token = strtok(NULL, " \n"); 
    }
    return cmdCount; 
}

/**
 * @brief This function handles the LIST command and sends the list of files and folders in the current working directory. 
 * @param newSocket the socket on which the reply has to be send
 * 
 */

int handleListCommand(int newSocket)
{
    char rBuffer[1024];
    char files[1024];
    DIR *d;
    struct dirent *dir;
    d = opendir(".");
    if (d)
    {
        bzero(rBuffer, sizeof(rBuffer));
        bzero(files, sizeof(files));
        while ((dir = readdir(d)) != NULL)
        {
            sprintf(files, "%s\n", dir->d_name);
            strcat(rBuffer, files);
        }
        send(newSocket, rBuffer, strlen(rBuffer), 0);
        closedir(d);
    }
    bzero(rBuffer, sizeof(rBuffer));
}

/**
 * @brief This function is to change working directory.
 * @param dir address of the directory
 */
int changeDirectory(char *dir)
{
    if(chdir(dir) < 0)
    {
        return 1;
    }
    return 0;
}
/**
 * @brief This function is used to unlink files, deletes all the files and subfolders in a folder, this function is passed in the nftw function. 
 */
int unlinkContent(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    int i = remove(fpath);
    if (i)
    {
        perror(fpath);
    }
    return i;
}
/**
 * @brief This function is used to delete a folder and all files in the folder
 * @param path path to folder that is to be deleted
 */
int removeDir(char *path)
{
    return nftw(path, unlinkContent, 64, FTW_DEPTH | FTW_PHYS);
}

/**
 * @brief This is the main function, in this function we create and bind a socket to the server's IP and PORT and start listening to connections.
 * We accept max 10 connections.
 * After accepting a connection we fork the process and start communication with the client.
 * After forking we do not wait for the child process to exit so we can allow connections from multiple clients.
 * Once a client is connected, we allow them to autheticate. 
 * After authetication, client can perform all the implemented FTP commands. 
 * If client tries to execute a command that is not implmented we let them know that the server does not support that command.
 * PORT command opens a data transfer channel for transfer data, the normal channel is used for communicating only, no transfer of data takes place on that channel.
 * STOR, RETR and LIST command require the PORT command to be executed first to open the data transfer channel.
 * In the implementation of our PORT command we get the server ready to connect to a port opened on client side for data transfer.
 * The STOR function recieves the data byte by byte from client and then stores the data to the file opened to write once a new line character is encoutered and continues recieving the data till EOF.
 * The RETR function sends the data to client on the data transfer port, data is sent till EOF is encountered.
 * Rest of the functions are simply executed and appropriate response is send to the client.
 */

int main(int argc, char *argv[])
{
    int sockfd, ret;

    struct sockaddr_in serverAddr;
    int newSocket;
    struct sockaddr_in newAddr;
    socklen_t addr_size;

    char buffer[1024];
    char replyBuffer[1024];
    pid_t childpid;

    if (argc < 2)
    {
        printf("\nCurrent Working Directory Will Be The Server's Working Directory.");
    }
    else
    {
        if(changeDirectory(argv[1]) == 1 )
        {
            printf("\nIncorrect Directory Entered. Defaulted to current directory.");
            changeDirectory(".");
        }
        else
        {
            printf("\nWorking Directory Set to %s", argv[1]);
        }
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        printf("Error in creating server socket.\n");
        exit(1);
    }
    printf("\nServer Socket is created.");

    memset(&serverAddr, '\0', sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    ret = bind(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (ret < 0)
    {
        printf("\nError in binding.");
        exit(1);
    }
    printf("\nBind to port %d", PORT);

    if (listen(sockfd, 10) == 0) // 10 clients max
    {
        printf("\nListening....");
    }
    else
    {
        printf("\nError in listening");
    }

    while (1)
    {
        newSocket = accept(sockfd, (struct sockaddr *)&newAddr, &addr_size);
        if (newSocket < 0)
        {
            exit(1);
        }
        printf("\nsockfd : %d and newSocket : %d Connection accepted from %s : %d\n", sockfd, newSocket, inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));
        if ((childpid = fork()) == 0)
        {
            close(sockfd);
            char *commands[255];
            int authenticated = 0;

            // FOR file transfer
            int transferChannel = 0;
            int dataSocket; // data transfer socket
            struct sockaddr_in remoteAddr;
            char dataRvcBuf[255];
            // END FOR file transfer
            while (1)
            {
                bzero(buffer, sizeof(buffer));
                recv(newSocket, buffer, 1024, 0);

                if (strcasecmp(buffer, "QUIT") == 0)
                {
                    bzero(replyBuffer, sizeof(replyBuffer));
                    printf("\nDisconnected from %s : %d\n", inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));
                    strcpy(replyBuffer, "221");
                    send(newSocket, replyBuffer, strlen(replyBuffer), 0);
                    close(newSocket);
                    break;
                }
                if (authenticated == 1)
                {
                    commands[0] = NULL;
                    extractCommands(buffer, commands);
                    
                    if ((strcasecmp(commands[0], "PORT") == 0) && transferChannel != 1)
                    {
                        transferChannel = 1;
                        sleep(2);

                        dataSocket = socket(AF_INET, SOCK_STREAM, 0);
                        int portDec = atoi(commands[1]);
                        memset(&remoteAddr, '\0', sizeof(remoteAddr));
                        remoteAddr.sin_family = AF_INET;
                        remoteAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
                        remoteAddr.sin_port = htons(portDec);
                        if ((connect(dataSocket, (struct sockaddr *)&remoteAddr, sizeof(remoteAddr))) != 0)
                        {
                            printf("\nconnection to %s %d was unsuccessful\n", inet_ntoa(remoteAddr.sin_addr), ntohs(remoteAddr.sin_port));
                            sprintf(replyBuffer, "425 Something is wrong, can't start the active connection... \r\n");
                            send(newSocket, replyBuffer, strlen(replyBuffer), 0);
                            transferChannel = 0;
                            close(dataSocket);
                        }
                        else
                        {
                            printf(" \nData connection to client created (active connection)");
                            send(newSocket, "200 Command Okay.", strlen("200 Command Okay."), 0);
                        }
                    }

                    else if ((strcasecmp(commands[0], "PORT") == 0) && transferChannel == 1)
                    {
                        send(newSocket, "125 Data connection already open; transfer starting.", strlen("125 Data connection already open; transfer starting."), 0);
                    }                   

                    else if (strcasecmp(commands[0], "LIST") == 0 && transferChannel == 1)
                    {
                        send(newSocket, "150 File status okay; about to open data connection.\n 125 Data connection already open; transfer starting.", strlen("150 File status okay; about to open data connection.\n 125 Data connection already open; transfer starting."), 0);
                        handleListCommand(dataSocket);
                    }

                    else if (strcasecmp(commands[0], "LIST") == 0 && transferChannel != 1)
                    {
                        send(newSocket, "425 Can't open data connection.", strlen("425 Can't open data connection."), 0);
                    }

                    else if ((strcasecmp(commands[0], "STOR") == 0) && transferChannel == 1)
                    {
                        FILE *tempfd = fopen(commands[1], "w");
                        if (tempfd == NULL)
                        {
                            send(newSocket, "450 Requested file action not taken.", strlen("450 Requested file action not taken."), 0);
                            continue;
                        }   
                        fclose(tempfd);
                        bzero(replyBuffer,sizeof(replyBuffer));
                        FILE *fout = fopen(commands[1], "a");
                        sprintf(replyBuffer,"upload %s",commands[1]);
                        send(newSocket, replyBuffer, strlen(replyBuffer), 0);
                        while (1)
                        {
                            int n = 0;
                            int bytes;
                            while (1)
                            {
                                bytes = recv(dataSocket, &dataRvcBuf[n], 1, 0);
                                if (bytes <= 0)
                                {
                                    break;
                                }
                                    
                                if (dataRvcBuf[n] == '\n')
                                { 
                                    dataRvcBuf[n] = '\0';
                                    break;
                                }

                                if (dataRvcBuf[0] != '\r')
                                {
                                    n++;
                                }   
                            }
                            if (bytes <= 0)
                                break;
                            fprintf(fout, "%s\n", dataRvcBuf);
                            printf("File Data : %s\n", dataRvcBuf);
                        }
                        bzero(dataRvcBuf,sizeof(dataRvcBuf));
                        fclose(fout);
                        close(dataSocket);
                        transferChannel = 0;
                        send(newSocket, "226 File transfer completed. Requested file action successful.\n250 Requested file action okay, completed.", strlen("226 File transfer completed. Requested file action successful.\n250 Requested file action okay, completed."), 0);
                        printf("\nData transfer completed");
                    }

                    else if ((strcasecmp(commands[0], "STOR") == 0) && transferChannel != 1)
                    {
                        send(newSocket, "425 Can't open data connection.", strlen("425 Can't open data connection."), 0);
                    }

                    else if ((strcasecmp(commands[0], "RETR") == 0) && transferChannel == 1)
                    {
                        FILE *fd = fopen(commands[1], "r");
                        if(fd == NULL)
                        {
                            printf("\nWoops File Doesnt Exist.");
                            send(newSocket,"450 Requested file action not taken. File unavailable",strlen("450 Requested file action not taken. File unavailable"),0);
                            continue;
                        }
                        else
                        {
                            char temp_buffer[80];
                            sprintf(temp_buffer,"download %s",commands[1]);
                            send(dataSocket, temp_buffer, strlen(temp_buffer), 0);
                            bzero(temp_buffer,80);
                            
                            while (!feof(fd))
                            {
                                fgets(temp_buffer,80,fd);
                                sprintf(buffer,"%s",temp_buffer);
                                send(dataSocket, buffer, strlen(buffer), 0);
                            }
                            fclose(fd);
                            close(dataSocket);
                            transferChannel = 0;
                            send(newSocket, "226 File transfer completed. Requested file action successful.\n250 Requested file action okay, completed.", strlen("226 File transfer completed. Requested file action successful.\n250 Requested file action okay, completed."), 0);
                        }
                    }

                    else if ((strcasecmp(commands[0], "RETR") == 0) && transferChannel != 1)
                    {
                        send(newSocket, "425 Can't open data connection.", strlen("425 Can't open data connection."), 0);
                    }

                    else if(strcasecmp(commands[0], "PWD") == 0)
                    {
                        char cwd[255];
                        sprintf(replyBuffer,"%s",getcwd(cwd,255));
                        send(newSocket,replyBuffer,strlen(replyBuffer),0);
                        bzero(replyBuffer,1024);
                    }

                    else if(strcasecmp(commands[0], "CWD") == 0)
                    {
                        changeDirectory(commands[1]);
                        char cwd[255];
                        sprintf(replyBuffer,"%s",getcwd(cwd,255));
                        send(newSocket,replyBuffer,strlen(replyBuffer),0);
                        bzero(replyBuffer,1024);
                    }

                    else if(strcasecmp(commands[0], "CDUP") == 0)
                    {
                        changeDirectory("..");
                        char cwd[255];
                        sprintf(replyBuffer,"%s",getcwd(cwd,255));
                        send(newSocket,replyBuffer,strlen(replyBuffer),0);
                        bzero(replyBuffer,1024);
                    }

                    else if(strcasecmp(commands[0], "DELE") == 0)
                    {
                        if (remove(commands[1]) == 0)
                        {
                            sprintf(replyBuffer,"250 Requested file action okay, completed.");
                            send(newSocket,replyBuffer,strlen(replyBuffer),0);
                            bzero(replyBuffer,1024);
                        }
                        else
                        {
                            sprintf(replyBuffer,"450 Requested file action not taken. \nFile unavailable");
                            send(newSocket,replyBuffer,strlen(replyBuffer),0);
                            bzero(replyBuffer,1024);
                        }
                    }

                    else if(strcasecmp(commands[0], "MKD") == 0)
                    {
                        if (mkdir(commands[1],0777) == 0)
                        {
                            sprintf(replyBuffer,"257 \"PATHNAME\" created.");
                            send(newSocket,replyBuffer,strlen(replyBuffer),0);
                            bzero(replyBuffer,1024);
                        }
                        else
                        {
                            sprintf(replyBuffer,"550 Requested action not taken.\n File unavailable");
                            send(newSocket,replyBuffer,strlen(replyBuffer),0);
                            bzero(replyBuffer,1024);
                        }
                    }

                    else if(strcasecmp(commands[0], "RMD") == 0)
                    {
                        if (removeDir(commands[1]) == 0)
                        {
                            sprintf(replyBuffer,"250 Requested file action okay, completed.");
                            send(newSocket,replyBuffer,strlen(replyBuffer),0);
                            bzero(replyBuffer,1024);
                        }
                        else
                        {
                            sprintf(replyBuffer,"550 Requested action not taken.\n File unavailable");
                            send(newSocket,replyBuffer,strlen(replyBuffer),0);
                            bzero(replyBuffer,1024);
                        }
                    }

                    else if(strcasecmp(commands[0], "REIN") == 0)
                    {
                        send(newSocket, "220 Service ready for new user.", strlen("220 Service ready for new user."), 0);
                        authenticated = 0;
                        if(transferChannel == 1)
                        {
                            transferChannel = 0;
                            close(dataSocket);
                        }
                    }

                    else if(strcasecmp(commands[0], "NOOP") == 0)
                    {
                        send(newSocket, "200 Command Okay.", strlen("200 Command Okay."), 0);
                    }

                    else
                    {
                        printf("\nClient: %s\n", buffer);
                        send(newSocket, "502 Command not implemented.", strlen("502 Command not implemented."), 0);
                        bzero(buffer, sizeof(buffer));
                    }
                }
                else
                {
                    if (strcasecmp(buffer, "USER") == 0)
                    {
                        authenticated = 1;
                        send(newSocket, "230 User logged in, proceed.", strlen("230 User logged in, proceed."), 0);
                    }
                    else
                    {
                        send(newSocket, "530 Not logged in.", strlen("530 Not logged in."), 0);
                    }
                }
                bzero(buffer, sizeof(buffer));
            }
        }
    }
    return 0;
}