#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/time.h>
#include <semaphore.h>
#include <time.h>
#include <ctime>


#include <iostream>
#include <iomanip>
#include "wordcount.h"

using namespace std;

const char PORT[]= "3490";  // the port users will be connecting to
const int BACKLOG=1000;     // how many pending connections queue will hold
const int MAXTHREADNUM=10;
const int BUFFERSIZE=4096;
long serviceCount=0;	//total number client that had been served
double serviceTime=0;

pthread_mutex_t lock; //counter and timer lock
sem_t thread_sem; //thread number semaphore


void* serve_it(void*);
void* stat(void*);
void *get_in_addr(struct sockaddr *sa);

int main(void)
{
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    int yes=1;
    int rv;
    char client_ip[INET6_ADDRSTRLEN];

    sem_init(&thread_sem, 0, MAXTHREADNUM);	//initialize the semaphore to 10

    int thread_n=0; //serve_it thread iterator
    pthread_t t_id[MAXTHREADNUM]; //serve_it thread "pool"
    pthread_t t_stat;	//thread for statistics
    pthread_attr_t attr;	//shared thread attributes
    pthread_attr_init(&attr); // Creating thread attributes
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO); // FIFO scheduling for threads
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); // Don't want threads  waiting on each other

    pthread_create(&t_stat, &attr, &stat, NULL);

    //initialize socket
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
    	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
        	cerr<<"error in creating socket."<<endl;
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
        	cerr<<"error in setsockopt."<<endl;
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            cerr<<"error in biding."<<endl;
            continue;
        }

        break;
    }

    if (p == NULL)  {
        cerr<<"server: failed to bind"<<endl;
        return 2;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (listen(sockfd, BACKLOG) == -1) {
        cerr<<"error in listening"<<endl;
        exit(1);
    }




    while(1) {  // main accept() loop
    	 cout<<"Server: waiting for new connections..."<<endl;

        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd < 0) {
        	cerr<<"error in accept"<<endl;
            continue;
        }
        inet_ntop(their_addr.ss_family,get_in_addr((struct sockaddr *)&their_addr),client_ip, sizeof client_ip);
    	cout<<"Server: connection from "<<client_ip<<" accepted."<<endl;

        sem_wait(&thread_sem); //wait the thread semaphore
        pthread_create(&t_id[thread_n], &attr, &serve_it,(void*)new_fd); //create a new thread to handle the connection
        thread_n=(thread_n+1)%MAXTHREADNUM; //increment the thread iterator
        cout<<"Server: new thread 'Service no."<<serviceCount+1<<"' is created to serve "<<client_ip<<endl;

        sleep(0); // Giving threads some CPU time

    }//while
    
    return 0;
}


void* serve_it(void* arg) //serving thread routine
{

    pthread_mutex_lock (&lock);
	serviceCount++;
	pthread_mutex_unlock (&lock);

	int csocket_df = (int) (arg);
	int service_no=serviceCount;
	int sent_total=0;
	string end_mark="$END$";
	string receive;
	string report;
 	wordCount wc=wordCount();
    char buf[BUFFERSIZE];
 	bzero(buf,BUFFERSIZE);
 	const clock_t begin_time = clock();
 	double timeDiff;



	//check if df is passed properly
	if (csocket_df < 0)
	{ 
		perror("ERROR: bad df in thread.");
		sem_post(&thread_sem);
		pthread_exit(0);
	}

	//read input from an accepted client
	while(1){
		bzero(buf,sizeof(buf));
		int bytes_receive = recv(csocket_df,buf,BUFFERSIZE,0);
		if (bytes_receive == 0)
		{
			cout<<"0 byte."<<endl;
			break;
		}else if (bytes_receive < 0)
		{
			perror("ERROR: receive failed in thread.");
			close(csocket_df);
			sem_post(&thread_sem);
			pthread_exit(0);
		}

		cout<<"Service no."<<service_no<<": "<<bytes_receive<<"  bytes received"<<endl;
		receive+=buf; //append content from buffer to string variable

		unsigned found = receive.rfind(end_mark);
		if (found!=string::npos){//stop looping after end mark is detected
		    receive.erase(found,end_mark.length()); //remove the end mark
		    break;
		}

	}

	if(receive.length()==0){ //no string was received from this connection.
		cout<<"Empty input."<<endl;
		close(csocket_df);
		sem_post(&thread_sem);
		pthread_exit(0);
	}

	cout<<"Service no."<<service_no<<": A total of "<<receive.length()<<" bytes have been received. Start to process..."<<endl;

	//process the input
	wc.setStr(receive);
	report=wc.getSummary();
	cout<<"Service no."<<service_no<<": "<<"Processing complete. Length of report: "<<report.length()<<endl;
	//cout<<report;

	//sent result back
	while (report.length()>0){
		bzero(buf,BUFFERSIZE);
		string tmp=report.substr(0,(report.length()>BUFFERSIZE)?BUFFERSIZE:report.length());
		report.erase(0,tmp.length());//reduce the report to unsent part
		strcpy(buf,tmp.c_str());

		int bytes_sent = send(csocket_df,buf,BUFFERSIZE,0);
		sent_total+=bytes_sent;
		cout<<"Service no."<<service_no<<": "<<bytes_sent<<" bytes sent"<<endl;
		if (bytes_sent <= 0)
		{
			perror("ERROR writing to socket");
			close(csocket_df);
			sem_post(&thread_sem);
			pthread_exit(0);
		}
	}

	cout<<"Service no."<<service_no<<": A total of "<<sent_total<<" bytes have been sent."<<endl;

	//cout<<"Service no."<<service_no<<": "<<"before clock()"<<endl;
	timeDiff = float( clock () - begin_time ) /  CLOCKS_PER_SEC;
	//cout<<"Service no."<<service_no<<": "<<"after clock()"<<endl;

	cout<<"Service no."<<service_no<<": "<<"Service finished. Time usage of this service: "
			<<timeDiff<<"s."<<endl;

    pthread_mutex_lock (&lock);
    serviceTime+=timeDiff;
	pthread_mutex_unlock (&lock);

	close(csocket_df); //close the client socket file description
	sem_post(&thread_sem);
	pthread_exit(0);

}

void* stat(void* arg) //statistics thread routine
{
	int sem_value,thread_count;
	time_t origin,now;
	char timebuff[20];
	double totalTime=0;
	time(&origin);
	while(1){
		cout<<"Stat thread is standing by. Enter any charactor to see stats"<<endl<<endl;
		cin.ignore();
		cin.get();
		sem_getvalue(&thread_sem, &sem_value);
		thread_count=MAXTHREADNUM-sem_value;
		time(&now);
		strftime(timebuff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
		totalTime=difftime(now,origin);
		cout<<"=========================================================="<<endl;
		cout<<"Stat information as of "<<timebuff<<endl;
		cout<<"Number of serving thread: "<<thread_count<<endl;
		cout<<"Number of clients have been served: "<<serviceCount<<endl;
		cout<<"Time used in serving: "<<serviceTime<<"s"<<endl;
		cout<<"Time since server started: "<<totalTime<<"s"<<endl;
		cout<<"=========================================================="<<endl;
	}
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
