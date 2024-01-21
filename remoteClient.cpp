/* File: remoteClient.cpp */
#include <sys/types.h>                                   /* For sockets */
#include <sys/socket.h>                                  /* For sockets */
#include <netinet/in.h>                         /* For Internet sockets */
#include <netdb.h>                                 /* For gethostbyname */
#include <stdio.h>                                           /* For I/O */
#include <stdlib.h>                                         /* For exit */
#include <string.h>                         /* For strlen, bzero, bcopy */
#include <fcntl.h>                                           /*for creat*/
#include <unistd.h>                                /*for write and close*/
#include <libgen.h>                                        /*for dirname*/
#include <sys/stat.h>                                        /*for mkdir*/

/*
*A function to recursivelly create all the directories contained in the path of directory dirnam
*/
void dirmake(char* dirnam){
    char temp[100];
    bzero(temp,100);
    strcpy(temp,dirnam);
    if(strcmp(dirnam,".")!=0){  //if we are not on local directory
    char path[100];
    bzero(path,100);
    strcpy(path,dirname(dirnam));   //get directory path
    dirmake(path);                  //make directory pat
    mkdir(temp,0777);               //make current directory
    }
}



int main(int argc, char *argv[]){     //TCP Internet Stream Socket Client
    int port, sock; char buf[256];
    unsigned int serverlen;
    struct sockaddr_in server;
    struct sockaddr *serverptr;      //variables for client and server socket
    struct hostent *rem;

    printf("Client's parameters are:\nserverIP:  %s\nport:  %s\ndirectory:%s\n",argv[2],argv[4],argv[6]);

    if (argc < 3) {     // Are server's host name and port number given?
        printf("Please give host name and port number\n"); exit(1); 
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { // Create socket
        perror("socket"); exit(1); 
    }

    if ((rem = gethostbyname(argv[2])) == NULL) { // Find server address
        perror("gethostbyname"); exit(1); 
    }

    port = atoi(argv[4]);              // Convert port number to integer

    server.sin_family = AF_INET;                      // Internet domain

    bcopy((char *) rem -> h_addr, (char *) &server.sin_addr,rem -> h_length);

    server.sin_port = htons(port); // Server's Internet address and port
    serverptr = (struct sockaddr *) &server;
    serverlen = sizeof server;

    if (connect(sock, serverptr, serverlen) < 0) { // Request connection 
        perror("connect"); exit(1); 
    }

    printf("Connecting to %s on port %d\n", argv[2], port);

    bzero(buf, sizeof buf);
    strcpy(buf,argv[6]);
    buf[strlen(buf)] = '\0';
    if (write(sock, buf, sizeof buf) < 0) {           // Send directory name 
        perror("write"); exit(1); 
    }
    bzero(buf, sizeof buf);
    int bytes;
    char buffer[10000];                               //buffer variable will store anything returned from server
    bzero(buffer, sizeof buffer);
    char filename[100];                               //this variable will store the name of the file received by server
    bzero(filename, sizeof filename);
    char meta[100];                                  //meta variable stores the meta data of the file, given by the server
    bzero(meta, sizeof meta);
    int fileflag=0;
    int metaflag=0;                                 //flags to indicate the current situation (if we are getting a filename, metadata or text)
    int j=0;
    int file;                                       //this variable holds the number of the file created                                   
    
    while ((bytes=read(sock, buffer, sizeof buffer)) > 0) {         //while the server writes, repeat
        for(int i=0; i<bytes; i++){
            if(buffer[i]!='\0'){                                //skip empty chars, since we are sending blocks they might also be sent
                if(buffer[i]=='.'&&buffer[i+1]=='/'){           // ./indicates that a file is given, raise the fileflag
                    fileflag=1;
                    j=0;
                }
                else if(buffer[i]=='~'&&buffer[i+1]=='.'){      //~. indicates that the metadata is given, raise the metaflag
                    metaflag=1;
                    j=0;
                }

                if(fileflag==1){                                //if we are getting a file
                    if(buffer[i]==' '){                         //filename stop at \s the rest is the file
                        fileflag=0;
                        filename[j]='\0';
                        j=0;
                        printf("\nReceived: %s\ntext:\n",filename+2);
                        char path[100];
                        char temp[100];
                        bzero(temp,100);
                        bzero(path,100);
                        strcpy(temp,filename);
                        strcpy(path,dirname(temp));          //get the path given 
                        dirmake(path);                       //create the path to the filename
                        if( access(filename, F_OK ) != -1)
                        {
                            remove(filename);               //if file already exists, remove it
                        }
                        if((file=creat(filename,0666))<0){
                            perror("creat"); exit(1);       //create the file given by the server
                        }
                    }
                    else{
                        filename[j]=buffer[i];             //copy the name of the file from the buffer

                    }
                }
                else if(metaflag==1){                       //if we are getting metadata
                    if(buffer[i]=='.'&&buffer[i+1]=='~'){       //metadata stops at .~ the rest is either file or nothing
                        metaflag=0;
                        meta[j]='\0';
                        j=0;
                        printf("\nmetadata: Size of file %s\n",meta);
                        i++;
                        close(file);                //since metadata is the last part of the file, close the file
                    }
                    else{
                        if(j>1){
                            meta[j-2]=buffer[i];        //copy metadata, skipping ~.
                        }
                    }
                }
                else{
                    printf("%c",buffer[i]);             //print the text
                    char temp[1];
                    temp[0]=buffer[i];
                    if (write(file, temp, 1) < 0){     //write the text, word by word to the file
                        perror("write"); exit(1);
                    }
                }
                j++;
            }
        }
        bzero(buffer, 10000);
    }
    printf("\nclose sock\n");
    close(sock);
    exit(0);
}                     // Close socket and exit
