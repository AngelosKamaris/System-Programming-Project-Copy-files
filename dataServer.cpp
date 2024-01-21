/* File: dataServer.cpp */
#include <sys/types.h>                                   /* For sockets */
#include <sys/socket.h>                                  /* For sockets */
#include <netinet/in.h>                         /* For Internet sockets */
#include <netdb.h>                                 /* For gethostbyaddr */
#include <stdio.h>                                           /* For I/O */
#include <stdlib.h>                                         /* For exit */
#include <string.h>                                /* For strlen, bzero */
#include <pthread.h>                                      /* For threads*/
#include <fcntl.h>                                  /*for open, O_RDONLY*/
#include <unistd.h>                                 /*for write and read*/
#include <map>                                                 /*for map*/
#include <dirent.h>                             /*for directory handling*/


/*
*Function that accepts a string (name of a directory) and an int (number of socket)
*Searches the directory for files and other directories and if it finds a directory
*it calls itself using that directory, if it finds a file it adds it to a queue
*so that worker threads can handle it. Returns the amount of files put in queue
*either by the starting queue or by the rest inside of it.
*/
int recurdir(char *,int);

/*
*Communicator Threads are created every time a new connection is made.
*they accept as arguent the number of socket they were created for, they
*read the name of the directory, and pass it to recurdir along with the
*socket number. If the queue is full they wait for a signal from communicators
*else they enter the information, and send a signal to workers that the queue
*is not empty. Then wait until the amount of files dequed for this socket
*is equal to the amount of files entered by the recurdir and close the socket.
*/
void * comunicators(void*);

/*
*Workers are created at the start of the server and constanly check if the queue
*has nodes. If the queue is empty they wait for a signal from comunicators else
*they take the information from the queue, write the filename (along with the path),
*open the file, read it block by block,write each block to the socket and after
*that, they write the size of the file in the socket.
*/
void * workers(void*);

/*
*node to store the information we need from the queue
*
*-filename=path and name of the file (string)
*-socket= number of socket (int)
*/
struct node{
   char filename[256];
   int socket;
};

/*
*queue that stores the nodes
*
*-line=array of nodes (struct node*)
*-front=position of the first node in line
*-rear=position of the last node in line
*-size=size of line
*/
struct queue{
   struct node* line;
   int front;
   int rear;
   int size;
}tqueue;

/*
*Initialize queue with int linesize as the size of line
*/
void queue_init(int linesize){
   tqueue.size=linesize;
   tqueue.line=(struct node*)malloc(sizeof(struct node)*linesize);
   tqueue.front=-1;
   tqueue.rear=-1;
}

/*
*enter bl in the queue.
*returns -1 if queue is full
*returns 0 if bl entered succesfully
*/
int enqueue(struct node bl){
   if (tqueue.rear == tqueue.size - 1){
      return -1;                          //full queue
   }
   else {
       if (tqueue.front == -1){
         tqueue.front = 0;
         }
      tqueue.rear++;
      strcpy(tqueue.line[tqueue.rear].filename,bl.filename);
      tqueue.line[tqueue.rear].socket=bl.socket;
      return 0;                           //entered queue
  }
}

/*
*remove the first node from queue and store it in bl.
*returns -1 if queue is empty
*returns 0 if node was deleted succesfully
*/
int dequeue(struct node *bl){
   if (tqueue.front == - 1){
      return -1;                          //empty queue
   }
   else {
      strcpy(bl->filename,tqueue.line[tqueue.front].filename);
      bl->socket=tqueue.line[tqueue.front].socket;
      tqueue.front++;
      if (tqueue.front >tqueue.rear){
         tqueue.front = tqueue.rear=-1;
         }
      return 0;                           //exited queue
  }
}

/*
*free the memory allocated by queue
*/
void queue_destroy(){
   free(tqueue.line);
}

/*
*mutex used in enqueue, dequeue and write to make sure that data is not used
*by two threads at the same time (race condition)
*/
pthread_mutex_t qmutex = PTHREAD_MUTEX_INITIALIZER;

/*
*a condition that makes enqueue wait until queue is not full to try to enter
*a node
*/
pthread_cond_t fullq = PTHREAD_COND_INITIALIZER;

/*
*a condition that makes workers wait until a node has entered the queue
*/
pthread_cond_t emptyq = PTHREAD_COND_INITIALIZER;

/*
*a map that uses as its key the number of socket and stores as a value the
*amount of files dequeued from that socket
*/
std::map<int, int> wrfiles;

/*
*stores the size of block globally, for easier access (by workers)
*/
int blocksize;



int main(int argc, char *argv[]){         //TCP Internet Stream Socket Server
   int port, sock, newsock; 
   unsigned int serverlen, clientlen;
   struct sockaddr_in server, client;
   struct sockaddr *serverptr, *clientptr;      //variables used to create a socket and link it with clients
   struct hostent *rem;
   int poolnum=atoi(argv[4]);
   blocksize=atoi(argv[8]);
   
   printf("Server's Parameters are:\nport:  %s\nthread_pool_size:  %s\nqueue_size:  %s\nBlock_size: %s\n",argv[2],argv[4],argv[6],argv[8]);

   queue_init(atoi(argv[6]));                   //initialize queue with the correct size
   pthread_t workers_pool[poolnum];                   //initialize worker pool
   for(int i=0;i<poolnum;i++){
      pthread_create(&workers_pool[i],NULL,workers,NULL);   //create workers
   }
   

   if (argc < 2) {            // Check if server's port number is given 
      printf("Please give the port number\n");
      exit(1);
   }

   if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { // Create socket (Using SOCK_STREAM because information is sent in multiple blocks) 
      perror("socket"); exit(1);
   }

   port = atoi(argv[2]);              // Convert port number to integer 

   server.sin_family = AF_INET;                      // Internet domain 
   server.sin_addr.s_addr = htonl(INADDR_ANY);   // My Internet address 
   server.sin_port = htons(port);                     // The given port 

   serverptr = (struct sockaddr *) &server;
   serverlen = sizeof server;

   if (bind(sock, serverptr, serverlen) < 0) {// Bind socket to address
      perror("bind"); exit(1);
   }

   printf("Server was successfully initialized...\n");

   if (listen(sock, 5) < 0) {                 // Listen for connections
      perror("listen"); exit(1); 
   }

   printf("Listening for connections to port %d\n", port);

   while(1) {              //Server repeatedly is looking for connections

      clientptr = (struct sockaddr *) &client;
      clientlen = sizeof client;

      if ((newsock = accept(sock, clientptr, &clientlen)) < 0) {
         perror("accept"); exit(1);
      }              // Accept connection from client

      if ((rem = gethostbyaddr((char *) &client.sin_addr.s_addr, sizeof client.sin_addr.s_addr, client.sin_family)) == NULL) {      // Find client's address
         perror("gethostbyaddr"); exit(1); 
      }

      printf("Accepted connection from %s\n\n", rem -> h_name);

      pthread_t communicator_thread;
      int *p_newsock=(int*)malloc(sizeof(int));
      *p_newsock=newsock;
      pthread_create(&communicator_thread,NULL,comunicators,p_newsock);       //create a new thread now that a connection was made
   }
   queue_destroy();
}



int recurdir(char *dirname, int newsock){
   int res=0;                                //number of files enqued in socket newsock 
   DIR *dir = opendir(dirname);              //open directory given
   if(!dir){
      perror("dir problem"); exit(1); 
   }
   struct node bl;
   struct dirent* df;
   df=readdir(dir);                         //df is the first file\dir inside of the directory given
   while(df!=NULL){
      if (df->d_type == DT_DIR && strcmp(df->d_name, ".") != 0 && strcmp(df->d_name, "..") != 0) {       //if df is a directory and not the current (.) or the previous one (..)
         char newdir[100];
         bzero(newdir, sizeof newdir);
         strcat(newdir,dirname);
         strcat(newdir,"/");
         strcat(newdir,df->d_name);       //connect the name of the current directory with the name of the newly found directory to create a path 
         res+=recurdir(newdir,newsock);   //call this function for the new directory, add the amount of files enqued to the variable that gets returned.
      }
      else if(strcmp(df->d_name, ".") != 0 && strcmp(df->d_name, "..") != 0){                            //since this is not a dir it must be a file, still we dont want . && ..
         char newdir[100];
         bzero(newdir, sizeof dir);             
         strcat(newdir,dirname);
         strcat(newdir,"/");
         strcat(newdir,df->d_name);                               //create and store the filename connected to the path it belongs to
         pthread_mutex_lock(&qmutex);                     //enqueue can be used by one thread each time
         strcpy(bl.filename,newdir);
         bl.socket=newsock;                                 //store information in a node;
         printf("[Thread:  %ld]: Adding file %s to the queue...\n",pthread_self(), newdir);
         int en=enqueue(bl);                              //store node in the queue
         while(en==-1){
            pthread_cond_wait(&fullq,&qmutex);           //if queue is full, wait for a signal to enter
         en=enqueue(bl);
         }
         pthread_cond_signal(&emptyq);                   //signal that queue is not empty
         pthread_mutex_unlock(&qmutex);
         res++;
      }
      df=readdir(dir);                             //get next file
   }
   closedir(dir);                                  //close the directory
   return res;
   
}



void * comunicators(void * p_newsock){
   
   char buf[256];
   int newsock=*((int*)p_newsock);                 //argument is the number of the current socket
   bzero(buf, sizeof buf);             
   if (read(newsock, buf, sizeof buf) < 0) {       //communicator, reads the name of the directory sent via socket by client
      perror("read"); exit(1); 
   }
   printf("[Thread:  %ld]: About to scan directory %s\n",pthread_self(), buf);
   wrfiles.insert(std::pair<int, int>(newsock, 0));            //initialize a key of current socket number in map with value 0.
   int res=recurdir(buf,newsock);                 //pass directory to recurdir to scan and give to workers
   while(wrfiles.find(newsock)->second!=res);      //wait until the amount of files found by recurdir is equal to that of the dequed files for this socket
   wrfiles.find(newsock)->second=0;
   free(p_newsock);                             //free the arguments
   close(newsock);                             // Close socket
   printf("Closed socket\n");
   return NULL;
}



void * workers(void* argu){
   struct node bl;
   int bytes;
   while(1){                                 //repeat as long as server is running
      pthread_mutex_lock(&qmutex);           //only one thread each time can get a value from dequeue
      int res=dequeue(&bl);
      if(res!=-1){                           //if the queue is not empty
         printf("[Thread:  %ld]: Received task: <%s, %d>\n",pthread_self(), bl.filename,bl.socket);
         pthread_cond_signal(&fullq);        //signal enqueue in case the queue was full
         int newsock=bl.socket;
         if (write(newsock, "./", 2) < 0){     //indicator of file
                  perror("write"); exit(1);
         }
         if (write(newsock, bl.filename, strlen(bl.filename)) < 0){     //send filename and path
                  perror("write"); exit(1);
         }
         if (write(newsock, " ", 1) < 0){                //filename ends once "\s" is found
                  perror("write"); exit(1);
         }
         int size=0;
         int file=open(bl.filename,O_RDONLY);            //open the file in current directory
         char word[blocksize];
         bzero(word, sizeof word);
         printf("[Thread:  %ld]: About to read file %s\n",pthread_self(), bl.filename);
         while((bytes=read(file,word,blocksize))>0){     //repeat reading one block of the file
            if (write(newsock, word, blocksize) < 0){    //amd writting it on socket
               perror("write"); exit(1);
            }
            bzero(word, sizeof word);
            size+=bytes;                        //get the size of the file (metadata)
         }
         char sz[100];
         bzero(sz, sizeof sz);
         sprintf(sz,"%d",size);                 //turn string to num
         if (write(newsock, "~.", 2) < 0){      //this way we know when metadata starts
                  perror("write"); exit(1);
         }
         if (write(newsock, sz, strlen(sz)) < 0){     //write size in socket
                  perror("write"); exit(1);
         }
         if (write(newsock, ".~", 2) < 0){      //the end of metadata
                  perror("write"); exit(1);
         }
         bzero(sz, 100);
         wrfiles.find(newsock)->second+=1;      //change value in map with key socket, since a file was successfully dequeued
      }
      else{
         pthread_cond_wait(&emptyq,&qmutex);    //the queue is empty wait for signal from enqueue
      }
      pthread_mutex_unlock(&qmutex);            //threads can work freely again
   }
}