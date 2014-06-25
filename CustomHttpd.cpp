/*
 * CustomHttpd.cpp
 *
 *  Created on: Oct 22, 2013
 *      Author: subhranil
 */

#include<stdio.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<arpa/inet.h>
#include<cstdlib>
#include<netinet/in.h>
#include <ifaddrs.h>
#include<cstring>
#include<unistd.h>
#include<fcntl.h>
#include<pthread.h>
#include<errno.h>
#include <netdb.h>
#include <sys/time.h>
#include<semaphore.h>
#include<time.h>
#include<dirent.h>
#include"Queue.h"



bool gRunInDebug=false;
char gUserLogFile[1024]="";
char gSystemLogFile[1024]="sys.log";
int gListeningPort=8080;
char gDirectoryRoot[1024]="";
int gNumberOfThreads=4;
int gQueingTime=60;
char gSchedulingAlgo[5]="FCFS";
int gListeningSocketId;

static pthread_mutex_t taskListUpdateLock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t freeListUpdateLock=PTHREAD_MUTEX_INITIALIZER;

typedef struct sockaddr_in SocketAddressInfo;

typedef struct task
{
	char fileName[1024];
	long long int fileSize;
	int requestingConnectionId;
	char remoteIP[32];
	char queingTime[100];
	char executionTime[100];
	char request[100];
	char reqType[6];
	int status;
} Task;

typedef struct WorkerThread
{
	int threadId;
	void *task;
	void* delegatePool;
	pthread_t thread;
//	sem_t assignmentSem;
//	sem_t completionSem;
}WorkerThread;

typedef struct threadPool
 {
	WorkerThread **mThreadList; // array of worker threads(ptrs)
	Queue* avaliableThreads; // shared b/w worker threads and scheduler thread/
	Queue* taskList; // could be priority queue as well handle accordingly.Shared b/w queuing thread and scheduler thread
	int numberOfThreads;
	int schType;
	sem_t taskCompletion;
	sem_t* taskAssignment;

} CustomThreadPool;


//Necessary function declarations
void* workerRoutine(void *ptr);
void taskDidComplete(WorkerThread* ptr , CustomThreadPool* pool); // called on worker thread
char* getLocalIP();
long long int getFileSize(char* fileName);
char * getCurrentGMTTime();
char *replaceTildeInPath(char* path);



char* generatePermissionErrorHTML(char *file,int port)
{
	char baseHTML[600]="<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\"><html><head><title>403 Forbidden</title></head><body><h1>Forbidden</h1><p>You don't have permission to access this URL on this server.</p><hr><address>Myhttpd service at Port %d</address></body></html>";
	char newHTML[650];
	sprintf(newHTML,baseHTML,port);
	return newHTML;
}
char *generateMissingErrorHTML(char *file, int port)
{
	char baseHTML[600]="<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\"><html><head><title>404 Not Found</title></head><body><h1>Not Found</h1><p>The requested URL was not found on this server.</p><hr><address>Myhttpd service at Port %d</address></body></html>";
	char newHTML[650];
	sprintf(newHTML,baseHTML,port);
	return newHTML;
}


static pthread_mutex_t writeLock=PTHREAD_MUTEX_INITIALIZER;
static void* writeLog(char* str,char* outputFile)
{

	pthread_mutex_lock(&writeLock);
	//return;
	char *finalStr;
	if(strcmp(outputFile,gUserLogFile)==0)
	{
		finalStr=(char*)malloc(strlen(str));
		strcpy(finalStr,str);


	}else
	{
		finalStr=(char*)malloc(strlen(str)+30);
		time_t ltime;
		ltime=time(0); //current time
		char timestampStr[50];
		struct tm currTime;
		localtime_r(&ltime,&currTime);
		strftime(timestampStr,50,"%Y-%m-%d %H:%M:%S",(const struct tm*)&currTime);
		sprintf(finalStr,"[%s]:%s",timestampStr,str);
	}
	int writefd=open(outputFile,O_WRONLY|O_CREAT| O_EXCL, S_IRUSR | S_IWUSR | S_IROTH);
	if(writefd<0)
	{
		if(errno==EEXIST)
		{
			//printf("file already exists");
			writefd=open(outputFile,O_WRONLY);
			//printf("error in open%d",errno);
		}

	}
	lseek(writefd,0,SEEK_END);
	write(writefd,finalStr,strlen(finalStr));
	close(writefd);
	//if(finalStr)
	//{	free(finalStr);
	//	finalStr=NULL;
	//}
	pthread_mutex_unlock(&writeLock);
}


WorkerThread* initializeWorker(int id,CustomThreadPool* del) // same logic as class init
{
	WorkerThread* w=(WorkerThread*)malloc(sizeof(WorkerThread));
	w->threadId=id;
	w->task=NULL;
	//w->completionSem=*Completionsem;
	//w->assignmentSem=*assignSem;
	w->delegatePool=del;
	pthread_create(&w->thread,NULL,workerRoutine,(void*)w);
	return w;
}

char* getContentType(char *filename)
{
	 const char *dot = strrchr(filename, '.');
	 dot++;
	 
	 char type[30]="";
	 if(strcmp(dot,"jpg")==0||strcmp(dot,"jpeg")==0||strcmp(dot,"gif")==0||strcmp(dot,"png")==0)
		sprintf(type,"image/%s",dot);
	 else if(strcmp(dot,"css")==0||strcmp(dot,"html")==0)
		 sprintf(type,"text/%s",dot);
		 
		 //printf("%s-->%s\n",filename,type);
	 return type;
}



void executeTask(void *task)
{
	Task *t=(Task*)task;
	char statusStr[10];
	struct stat st;

	char *fileBuffer=NULL;
	char serverName[50]="myHttpd";
	char cType[30]="text/html";
	char modTimeHeader[100]="";

	int retValue=stat(t->fileName,&st);
	long long int dataSize=0;
	if(retValue==-1)
	{
		if(errno==ENOENT) //file does  not exist
		{
			t->status=404;
			strcpy(statusStr,"Not Found");
			char *mssg=generateMissingErrorHTML(t->fileName,gListeningPort);
			fileBuffer=(char*)malloc(strlen(mssg));
			strcpy(fileBuffer,mssg);
			dataSize=strlen(fileBuffer);
		}
	}
	else //stat works fine
	{
		if(S_ISDIR(st.st_mode)) // handle for dictionary
		{
			//check if it has index.html
			char newpath[100];
			sprintf(newpath,"%sindex.html",t->fileName);
			if(access(newpath,F_OK)==0)
			{
				strcpy(t->fileName,newpath);
				executeTask(t);
				return;
			}

			DIR *dir;
			fileBuffer=(char*)malloc(5000);
			strcpy(fileBuffer,"<table border=\"1\"><tr><td>FileName</td><td>Mod. Date</td><td>Size</td></tr>");
			//char mssg[5000];
			struct dirent *enumerator;
			if ((dir = opendir (t->fileName))!= NULL)
			{
				while ((enumerator = readdir (dir)) != NULL)
				{
					if(enumerator->d_name[0]=='.')
						continue;
					char path[100];
					struct stat s;
					sprintf(path,"%s%s",t->fileName,enumerator->d_name);
					int ret=stat(path,&s);
					if(ret==0)
					{
						//time_t modTimeValue=(time_t)s.st_mtim;
						char modTime[50];

						struct tm *ltime=localtime((time_t*)&(s.st_mtim));
						strftime(modTime,50,"%Y-%m-%d %H:%M:%S",ltime);
						sprintf(fileBuffer,"%s<tr><td><a href=\"%s\">%s</a></td><td>%s</td><td>%ldK</td></tr>\n",fileBuffer,enumerator->d_name,enumerator->d_name,modTime,(s.st_size/1024));
					}

				}
				closedir (dir);
				sprintf(fileBuffer,"%s</table>\n",fileBuffer);
				dataSize=strlen(fileBuffer);
				t->status=200;
				strcpy(statusStr,"OK");
			}
			else
			{
				t->status=403;
				strcpy(statusStr,"Forbidden");
				char *mssg=generatePermissionErrorHTML(t->fileName,gListeningPort);//PERMISSION_ERROR(t->fileName,serverName,gListeningPort);
				fileBuffer=(char*)malloc(strlen(mssg));
				strcpy(fileBuffer,mssg);
				dataSize=strlen(fileBuffer);
			}

		}
		else   // file .. rest of the code comes here
		{
			int fileDesc=open(t->fileName,O_RDONLY);
			if(fileDesc<0)
			{
				if(errno ==EACCES) //403 case
				{
					t->status=403;
					strcpy(statusStr,"Forbidden");
					char *mssg=generatePermissionErrorHTML(t->fileName,gListeningPort);
					fileBuffer=(char*)malloc(strlen(mssg));
					strcpy(fileBuffer,mssg);
					dataSize=strlen(fileBuffer);
				}
			}
			else // all is well !! add to buffer
			{
				strcpy(cType,getContentType(t->fileName));
				
				if(strcmp(t->reqType,"GET")==0)
				{
					fileBuffer=(char*)malloc(t->fileSize);
					dataSize=read(fileDesc,fileBuffer,t->fileSize);
					if(dataSize<=0) // this should not happen.
					{
						//printf("Unable to read");

					}
				}
				else
					dataSize=getFileSize(t->fileName);
				t->status=200;
				strcpy(statusStr,"OK");
				// fetch the mod time for the header
				
				struct tm ltime;
				gmtime_r((time_t*)&(st.st_mtim),&ltime);
				strftime(modTimeHeader,50,"%a, %e %b %Y %H:%M:%S %Z",&ltime);
			}
		}
	}
	strcpy(t->executionTime,getCurrentGMTTime());
	
	// create the date header value in the apache format 
	char dateHeader[100]="";
	time_t currTime;
	time(&currTime);
	struct tm ltime;
	gmtime_r(&currTime,&ltime);
	strftime(dateHeader,100,"%a, %e %b %Y %H:%M:%S %Z",&ltime);
	//convertIntoGMT()
	try
	{
	
	//handle get and head requests accordingly
	
	char *response=NULL;
	if(strcmp(t->reqType,"GET")==0)
	{
		response=(char*)malloc(dataSize+5000);
		sprintf(response,"HTTP/1.0 %d %s\nDate:%s\nServer:%s\nLast-Modified:%s\nContent-Type:%s\nContent-Length:%lld\n\n",t->status,statusStr,dateHeader,serverName,modTimeHeader,cType,dataSize);
		int headerLen=strlen(response);
		memcpy(response+headerLen,fileBuffer,dataSize);
		send(t->requestingConnectionId,response,(headerLen+dataSize),0);
	}
	else
	{
		response=(char*)malloc(5000);
		sprintf(response,"HTTP/1.0 %d %s\nDate:%s\nServer:%s\nLast-Modified:%s\nContent-Type:%s\nContent-Length:%lld\n\n",t->status,statusStr,dateHeader,serverName,modTimeHeader,cType,dataSize);
		send(t->requestingConnectionId,response,strlen(response),0);
	
	}
	close(t->requestingConnectionId);


	//Log into user logfile

	char buffer[500];
	sprintf(buffer,"%s - [%s] [%s] \"%s\" %d %lld\n",t->remoteIP,t->queingTime,t->executionTime,t->request,t->status,dataSize);
	if(gRunInDebug)
		printf("%s",buffer);
	else
	{
		if(strlen(gUserLogFile)!=0)
			writeLog(buffer,gUserLogFile);
	}
//	if(fileBuffer)
//		free(fileBuffer);
//	if(response)
		//free(response);
	}
	catch(...)
	{
		printf("exception caught.\n");
	}
}


void* workerRoutine(void *ptr) // static method , ptr will contain instance of WorkerThread*
{
	WorkerThread *obj=(WorkerThread*)ptr;
	while(1)
	{
		//pthread_mutex_lock(&obj->assignmentLock);

		sem_wait(&(((CustomThreadPool*)(obj->delegatePool))->taskAssignment[obj->threadId] ));
		executeTask((Task*)(obj->task));



		taskDidComplete(obj,(CustomThreadPool*)obj->delegatePool);
		//((CustomThreadPool*)(obj->delegate))->taskDidComplete(obj);

		sem_post(&(((CustomThreadPool*)(obj->delegatePool))->taskCompletion));
		//pthread_mutex_unlock(&obj->completionLock);
	}

}

void assignTask(WorkerThread *worker,Task* t)
{
	//if(worker->task)
		//free(worker->task);

	worker->task=t;
}

// this is hard coded only for task type data.. can be extended by generalization
void priorityEnQueue(Queue* q,void *task) // will be used only for SJF
{
	//Task * data=(Task*)task;
	Node* temp=(Node*)malloc(sizeof(Node));
	temp->data=task;
	if(q->mHead==NULL) // empty list
	{
		q->mHead=temp;
		q->mTail=temp;
		q->mHead->next=NULL;
		q->mNumberOfElements++;

	}
	else if(((Task*)(temp->data))->fileSize<((Task*)(q->mHead->data))->fileSize) // add at the beginning
	{
		temp->next=q->mHead;
		q->mHead=temp;
		q->mNumberOfElements++;

	}
	else
	{
		Node* ptr=q->mHead;
		while(ptr->next)
		{
			// insert node at location where data>ptr->data->filesize &&data<ptr->data->filesize
			if(((Task*)(temp->data))->fileSize>=((Task*)(ptr->data))->fileSize&&((Task*)(temp->data))->fileSize<((Task*)(ptr->data))->fileSize) //insert here
			{

				temp->next=ptr->next;
				ptr->next=temp;
				q->mNumberOfElements++;
				return;
			}
			else
				ptr=ptr->next;
		}
		//append at the end
		ptr->next=temp;
		temp->next=NULL;
		q->mTail=temp;
		q->mNumberOfElements++;

	}
}

CustomThreadPool* initializeThreadPool(int numThreads,int sch) // same logic as class init
{
	CustomThreadPool *pool=(CustomThreadPool*)malloc(sizeof(CustomThreadPool));
	pool->mThreadList=(WorkerThread**)malloc(sizeof(WorkerThread*)*numThreads);
	pool->taskAssignment=(sem_t*)malloc(sizeof(sem_t)*numThreads);
	pool->schType=sch;
	pool->avaliableThreads=initialiseQueue();
	sem_init(&pool->taskCompletion,0,numThreads);
	//ADD ALL THE THREADS TO THE AVAILABLE Q
	for(int i=0;i<numThreads;i++)
	{
		sem_init(&pool->taskAssignment[i],0,0);
		pool->mThreadList[i]= initializeWorker(i,pool);
		enQueue(pool->avaliableThreads,pool->mThreadList[i]);

	}
	pool->taskList=initialiseQueue();
	//if(sch==0)//FCFS
		//pool->taskList=new Queue();
	//else //SJF
		//pool->taskList=new PriorityQueue();

	return pool;
}

void addTask(CustomThreadPool*pool,Task* t) //called on the queuing thread that receives http requests
{
	pthread_mutex_lock(&taskListUpdateLock); //mutual exclusion
	if(pool->schType==0)
		enQueue(pool->taskList,t);
	else
		priorityEnQueue(pool->taskList,t);
	//printf("task added to %u\n",pool->taskList);
	fflush(stdout);
	writeLog("Task added\n",gSystemLogFile);
	pthread_mutex_unlock(&taskListUpdateLock);

}

static void* scheduler(void* ptr) // the STATIC function must run an infinite loop waiting for a semaphore that is dec everytime a thread is allocated and inc when the thread gets free
{
	CustomThreadPool *obj=(CustomThreadPool*)ptr;
	//printf("Starting wait on scheduler thread\n");
	writeLog("Starting wait on scheduler thread\n",gSystemLogFile);
	sleep(gQueingTime);
	//printf("Wait over. starting with scheduling\n");
	writeLog("Wait over. starting with scheduling\n",gSystemLogFile);
	while(1)
	{

		pthread_mutex_lock(&taskListUpdateLock);

		// Loop over the qu to display
		if(obj->taskList->mHead==NULL)
		{
			pthread_mutex_unlock(&taskListUpdateLock);
			continue;
			//printf("No tasks in Q.\n");
			//writeLog("No tasks in Q.\n",gSystemLogFile);
		}
		else
		{
			writeLog("Tasks queue details:****************************************\n",gSystemLogFile);
			char mssg[5000]="";
			for(Node* n=obj->taskList->mHead;n!=NULL;n=n->next)
			{
				Task* itTask=(Task*)(n->data);
				sprintf(mssg,"%s%s--->%lld--->%s--->%s ------>%s---->%s--->%d\n",mssg,itTask->fileName,itTask->fileSize,itTask->remoteIP,itTask->queingTime,itTask->executionTime,itTask->request,itTask->status);
			}
			sprintf(mssg,"%s******************************************************\n",mssg);
			writeLog(mssg,gSystemLogFile);
		}
        // End of log writing
		Node *n=deQueue(obj->taskList);
		Task* t=NULL;
		if(n!=NULL)
		{
			t=(Task*)(n->data);
			n->data=NULL;
			n->next=NULL;
			//t=(Task*)malloc(sizeof(Task));
			//memcpy(t,n->data,sizeof(Task));

			free(n);
			n=NULL;
		}


		pthread_mutex_unlock(&taskListUpdateLock);

		if(t==NULL)
			continue;
		//assign this task to the next available thread
//		pthread_mutex_lock(&freeListUpdateLock);
//		if(avaliableThreads->isEmpty())
//		{
//			pthread_mutex_unlock(&freeListUpdateLock);
//			pthread_mutex_lock(&taskCompletionLock);
//		}

		//Logger::writeLog("Waiting on task completion.\n",gSystemLogFile);
		sem_wait(&(obj->taskCompletion));

		pthread_mutex_lock(&freeListUpdateLock);

		// print the contents of threadQ

		writeLog("Available thread details:\n",gSystemLogFile);
		char mssg[5000];
		for(Node* n=obj->avaliableThreads->mHead;n!=NULL;n=n->next)
		{
			WorkerThread *w=(WorkerThread*)(n->data);
			sprintf(mssg,"%s%d---->",mssg,w->threadId);
		}
		sprintf(mssg,"%s\n",mssg);
		writeLog(mssg,gSystemLogFile);
		// end pf logging

		n=deQueue(obj->avaliableThreads);
		WorkerThread* worker=NULL;
		if(n!=NULL)
		{
			//worker=(WorkerThread*)malloc(sizeof(WorkerThread));
			//memcpy(worker,n->data,sizeof(WorkerThread));
			worker=(WorkerThread*)(n->data);
			n->data=NULL;
			n->next=NULL;
			free(n);
			n=NULL;
		}

		//WorkerThread* worker=(WorkerThread*)(deQueue(obj->avaliableThreads)->data);
		assignTask(worker,t);

		sprintf(mssg,"Assigning task:%s--->%lld to thread id:%d\n\n",t->fileName,t->fileSize,worker->threadId);
		writeLog(mssg,gSystemLogFile);

		pthread_mutex_unlock(&freeListUpdateLock);
		//pthread_mutex_unlock(&taskAssignmentLock[worker->threadId]);
		sem_post(&(obj->taskAssignment[worker->threadId]));
	}
}

void taskDidComplete(WorkerThread* ptr , CustomThreadPool* pool) // called on worker thread
{
	pthread_mutex_lock(&freeListUpdateLock);
	free(ptr->task);
	ptr->task=NULL;
	enQueue(pool->avaliableThreads,ptr);
	pthread_mutex_unlock(&freeListUpdateLock);
}


void displayHelp()
{
	int fd=open("Help",O_RDONLY);
	char buffer[2000];
	int n=read(fd,buffer,2000);
	buffer[n]=0;
	printf("%s",buffer);
	close(fd);
}

void parseShellArguments(int argc,char *argv[])
{
	int index=1;
	try
	{
	while(index<argc)
	{
		if(strcmp(argv[index],"-p")==0)
		{
			gListeningPort=atoi(argv[++index]);
			index++;
		}
		else if(strcmp(argv[index],"-r")==0)
		{
			//change
			strcpy(gDirectoryRoot,argv[++index]);
			index++;
		}
		else if(strcmp(argv[index],"-d")==0)
		{
			gRunInDebug=true;
			index++;
		}
		else if(strcmp(argv[index],"-h")==0)
		{
			displayHelp();
			exit(0);
		}
		else if(strcmp(argv[index],"-l")==0)
		{

			strcpy(gUserLogFile,argv[++index]);
			index++;
		}
		else if(strcmp(argv[index],"-t")==0)
		{
			gQueingTime=atoi(argv[++index]);
			index++;
		}
		else if(strcmp(argv[index],"-n")==0)
		{
			gNumberOfThreads=atoi(argv[++index]);
			index++;
		}
		else if(strcmp(argv[index],"-s")==0)
		{
			strcpy(gSchedulingAlgo,argv[++index]);
			if(!(strcmp(gSchedulingAlgo,"FCFS")==0||strcmp(gSchedulingAlgo,"SJF")==0))
			{
				printf("Invalid usage");
				exit(0);
			}
			index++;
		}
		else
		{
			printf("Unknown option");
			displayHelp();
			exit(0);
		}
	}
	}
	catch(...)
	{
		printf("Invalid usage\n");
		displayHelp();
		exit(0);
	}
}

void parseHTTPRequest(char* request,char** reqType,char** path)
{
	*reqType=strtok(request," ");
	*path=strtok(NULL," ");
}


char * getCurrentGMTTime()
{
	time_t currTime;
	time(&currTime);
	struct tm *gmTime=localtime(&currTime);
	char gmtTime[100]="";
	strftime(gmtTime,100,"%d/%b/%Y : %H:%M:%S %z",gmTime);
	//sprintf(gmtTime,"%d/%d/%d:%d:%d:%d",gmTime->tm_mday,gmTime->tm_mon,(gmTime->tm_year+1900),gmTime->tm_hour,gmTime->tm_min,gmTime->tm_sec);
	return gmtTime;
}


long long int getFileSize(char* fileName)
{
	//change to add further errorhandling otpions
	struct stat fileDetails;
	int rValue=stat(fileName,&fileDetails);
	return (rValue==0)?fileDetails.st_size:0;
}

char *replaceTildeInPath(char* path)
{
	if(path[0]=='/'&&path[1]=='~') // valid case
	{
		path+=2;
		char username[32];
		int index=0;
		while(*path!='/')
		{
			username[index]=*path;
			index++;
			path++;
		}
		username[index]=0;
		char newPath[100];
		sprintf(newPath,"/home/%s/myhttpd%s",username,path);
		return newPath;
	}
	else
		return path;
}

char* getLocalIP()
{
	SocketAddressInfo *socketAddress; // this structure basically stores info about the socket address
	socketAddress=(SocketAddressInfo*)malloc(sizeof(SocketAddressInfo));
	socketAddress->sin_family=AF_INET;
	socketAddress->sin_port=htons(53);
	inet_pton(socketAddress->sin_family,"8.8.8.8",&(socketAddress->sin_addr));

	//create a socket for connecting to server
	int socketId=socket(AF_INET,SOCK_DGRAM,0);
	if(socketId==-1)
	{
		printf("Error in creating socket");
		return "";
	}
	int retConnect=connect(socketId,(struct sockaddr*)socketAddress,sizeof(SocketAddressInfo));
	if(retConnect<0)
	{
		printf("connection with peer failed with error : %d",retConnect);
		return "";
	}

	SocketAddressInfo localAddressInfo;
	socklen_t len=sizeof(localAddressInfo);
	getsockname(socketId,(struct sockaddr*)&localAddressInfo,&len);
	char buffer[32];
	inet_ntop(AF_INET,&localAddressInfo.sin_addr,buffer,sizeof(buffer));
	return buffer;
}


static void* handleSocketBehaviour(void* pool)
{
			//printf("Waiting for incoming requests.\n");
			writeLog("Waiting for incoming requests.\n",gSystemLogFile);
			while(1)
			{
				SocketAddressInfo peerInfo;
				socklen_t addrSize=(socklen_t)sizeof(peerInfo);
				int confd=accept(gListeningSocketId,(struct sockaddr*)&peerInfo,&addrSize);
				getpeername(confd,(struct sockaddr*)&peerInfo,&addrSize);
				char clientIP[32];
				strcpy(clientIP,inet_ntoa(peerInfo.sin_addr));
				char request[5002];
				read(confd,&request,5000);
				//printf("%s",request);
				//fflush(stdout);
				char *reqType;//=(char*)malloc(5);
				char* path;//=(char*)malloc(5000);

				// Generate the task from the request info
				Task* newTask=(Task*)malloc(sizeof(Task));

				//Patch logic to fetch the frst line and store in newTask->request
				int i=0;
				while(1)
				{
					if(request[i]=='\n')
						break;
					newTask->request[i]=request[i];
					i++;
				}
				newTask->request[i-1]=0;


				//sprintf(newTask->request,"%s",request);
				//strcpy(newTask->request,request);
				parseHTTPRequest(request,&reqType,&path);
				if(path==NULL)
						continue;

				if(!strcmp(reqType,"GET"))
				{
					char fullPath[5000];
					strcpy(fullPath,gDirectoryRoot);
					if(path) // for HEAD request
						sprintf(fullPath,"%s%s",fullPath,replaceTildeInPath(path));
					long long int fileSize=getFileSize(fullPath);
					//if(fileSize>0)
					//{
						strcpy(newTask->fileName,fullPath);
						newTask->fileSize=fileSize;
						newTask->requestingConnectionId=confd;
						//strcpy(newTask->request,request);
						strcpy(newTask->remoteIP,clientIP);
						strcpy(newTask->queingTime,getCurrentGMTTime());
						strcpy(newTask->reqType,"GET");
					//}
					//change handle for errors

				}
				else if(!strcmp(reqType,"HEAD"))
				{
				
					char fullPath[5000];
					strcpy(fullPath,gDirectoryRoot);
					if(path) // for HEAD request
						sprintf(fullPath,"%s%s",fullPath,replaceTildeInPath(path));
						
					strcpy(newTask->fileName,fullPath);
					newTask->fileSize=0;
					newTask->requestingConnectionId=confd;
					strcpy(newTask->remoteIP,clientIP);
					strcpy(newTask->queingTime,getCurrentGMTTime());
					strcpy(newTask->reqType,"HEAD");

				}
				//sync
				if(gRunInDebug)
					executeTask(newTask);
				else
					addTask((CustomThreadPool*)pool,newTask);
				//executeTask(newTask);
				//free(reqType);
				//free(path);
			}

}

void openSocket()
{
	char destIP[32];
	strcpy(destIP,getLocalIP());
	//int mainSocket;
	// defining the address for the main socket
	SocketAddressInfo *socketAddress; // this structure basically stores info about the socket address
	socketAddress=(SocketAddressInfo*)malloc(sizeof(SocketAddressInfo));
	socketAddress->sin_family=AF_INET;
	socketAddress->sin_addr.s_addr=htons(INADDR_ANY);
	//inet_pton(socketAddress->sin_family,destIP,&(socketAddress->sin_addr));
	socketAddress->sin_port=htons(gListeningPort);

	//create a socket for accepting incoming connections
	gListeningSocketId=socket(AF_INET,SOCK_STREAM,0);
	if(gListeningSocketId==-1)
	{
		printf("Error in creating socket");
		exit(0);
	}
	// bind the spcket to the addressinfo
	if(bind(gListeningSocketId,(struct sockaddr*)socketAddress,sizeof(SocketAddressInfo))==-1)
	{
		printf("%d",errno);
		printf("Error in binding socket IP address");
		exit(0);
	}

	// listen for incoming connections
	if(listen(gListeningSocketId,100)==-1)
	{
		printf("Error in listening");
		exit(0);
	}
}

int main(int argc,char *argv[])
{

	getcwd(gDirectoryRoot,1024);
	parseShellArguments(argc,argv);

	remove(gUserLogFile);
	remove(gSystemLogFile);
	openSocket();
	if(gRunInDebug)
	{
		handleSocketBehaviour(NULL);
		exit(0);
	}
	else
	{
	        pid_t pid;

        	pid = fork();
        	if(pid<0)
        	        exit(EXIT_FAILURE);
        	        
        	if(pid>0) // kill the parent process
                	exit(0);
		else
		{
			close(STDIN_FILENO);
        		close(STDOUT_FILENO);
		        close(STDERR_FILENO);
		}
	
	}
	
	
	
	pthread_t queuingThread,schedulingThread;
	static CustomThreadPool* threadPool=initializeThreadPool(gNumberOfThreads,(strcmp(gSchedulingAlgo,"FCFS")==0?0:1));
	//CustomThreadPool threadPool(gNumberOfThreads,(strcmp(gSchedulingAlgo,"FCFS")==0?0:1));
	pthread_create(&queuingThread,NULL,&handleSocketBehaviour,threadPool);
	pthread_create(&schedulingThread,NULL,&scheduler,threadPool);
	//printf("thread id:%u",schedulingThread);

	//void* val1,*val2;
	//while(1)

	pthread_join(queuingThread,NULL);
	pthread_join(schedulingThread,NULL);

}
