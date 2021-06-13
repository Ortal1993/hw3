#include "segel.h"
#include "request.h"

// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// HW3: Parse the new arguments too
void getargs(int *port, int argc, char *argv[])
{
    if (argc < 2) {
	fprintf(stderr, "Usage: %s <port>\n", argv[0]);
	exit(1);
    }
    *port = atoi(argv[1]);
}

///############################   mutex and conds  #################################
int threads_inside, master_inside, waiting_requests, handled_requests;
pthread_cond_t read_allowed;
pthread_cond_t write_allowed;
pthread_mutex_t global_lock;

void readers_writers_init() {
    threads_inside = 0;
    master_inside = 0;///will be 0 or 1 always
    waiting_requests = 0;
    handled_requests = 0;
    pthread_cond_init(&read_allowed, NULL);
    pthread_cond_init(&write_allowed, NULL);
    pthread_mutex_init(&global_lock, NULL);
}

void thread_lock() {
    printf("thread trying to lock1\n");
    pthread_mutex_lock(&global_lock);
    printf("thread trying to lock2\n");
    while (master_inside > 0 || threads_inside > 0 || waiting_requests == 0 ){
        printf("thread %ld is sleeping now zzz\n",pthread_self());
        pthread_cond_wait(&read_allowed, &global_lock);
    }
    printf("I woke up\n");
    threads_inside++;
    pthread_mutex_unlock(&global_lock);
}

void thread_unlock() {
    pthread_mutex_lock(&global_lock);
    threads_inside--;
    if (threads_inside == 0 || waiting_requests == 0) {///maybe waiting_request condition is needless?
        pthread_cond_signal(&write_allowed);
        pthread_cond_broadcast(&read_allowed);
    }
    pthread_mutex_unlock(&global_lock);
}

void master_lock(int maxRequests) {
    pthread_mutex_lock(&global_lock);
    while (threads_inside > 0 || waiting_requests + handled_requests >= maxRequests)///second conditions is doing block
        pthread_cond_wait(&write_allowed, &global_lock);
    master_inside++;
    pthread_mutex_unlock(&global_lock);
}

void master_unlock() {
    pthread_mutex_lock(&global_lock);
    master_inside--;
    printf("waiting request is %d\n",waiting_requests);
    if (master_inside == 0 && waiting_requests > 0) {
        pthread_cond_broadcast(&read_allowed);
        printf("I'm going to wake up threads\n");
        //pthread_cond_signal(&write_allowed);///not sure
    }
    pthread_mutex_unlock(&global_lock);
}

///#################  thread functions  ##############################
struct thread_struct {
    pthread_t pidT;
    int index;//the id of thread in stas
    int handle_counter;
    int static_counter;
    int dynamic_counter;
};

typedef struct thread_struct Thread;

///#################  buffer/queue functions  ##############################
struct request_struct{
    int requestID;
    struct timeval arrival_time;
    struct timeval dispatch_time;
    struct request_struct * next;
    struct request_struct * prev;
};

typedef struct request_struct Request;

struct buffer_struct{
    int sizeAllowed;
    Request * head;
    Request * tail;
    int numOfItems;
    Request * iterator;
};

typedef struct buffer_struct Buffer;

int buffer_init(Buffer * bf, int len){
    Request * head = malloc(sizeof(Request));
    if (!head){
        return -1;
    }
    head->requestID = -1;
    head->next = NULL;
    head->prev = NULL;

    bf->head = head;
    bf->tail = head;
    bf->numOfItems = 0;
    bf->sizeAllowed = len;
    bf->iterator = NULL;
    return 0;
}

void destroyRequests(Buffer * buffer){
    Request * curr = buffer->head;
    while(curr != NULL){
        Request * next = curr->next;
        free(curr);
        buffer->numOfItems--;
        curr = next;
    }
    free(buffer->head);
}

int isEmpty(Buffer buffer){
    if (buffer.numOfItems == 0) {
        return 1;
    }else{
        return 0;
    }
}

Request* pop_front(Buffer * buffer){
    //thread_lock();///
    if (!isEmpty(*buffer)){
        printf("in pop_front\n");
        if (buffer->head->next) {
            printf("in pop_front...1. number of threads inside is %d\n", threads_inside);
            if(buffer->tail == NULL)
                printf("WHYYYYYYYYYYYYYYYYYYYYY\n");
            if (buffer->head->next->requestID == buffer->tail->requestID) {
                printf("in pop_front...1.5\n");
                buffer->tail = buffer->head;
            }
        }
        printf("in pop_front 1.8\n");
        Request * poppedRequest = buffer->head->next;
        printf("in pop_front 1.9\n");
        if(poppedRequest->next)
            printf("WHYWHY2\n");
        else{
            printf("what\n");
        }
        buffer->head->next = poppedRequest->next;
        printf("in pop_front...2\n");
        if (poppedRequest->next != NULL){
            printf("in pop_front...3\n");
            poppedRequest->next->prev = buffer->head;
        }
        poppedRequest->next = NULL;
        poppedRequest->prev = NULL;
        buffer->numOfItems--;
        //thread_unlock();///
        printf("in pop_front...4\n");
        return poppedRequest;
    } else {
        //thread_unlock();///
        printf("in pop_front...NULL\n");
        return NULL;
    }
}

void pop_back(Buffer * buffer){
    if (!isEmpty(*buffer)){
        Request* poppedRequest = buffer->tail;
        buffer->tail = poppedRequest->prev;
        buffer->tail->next = NULL;
        buffer->numOfItems--;
        free(poppedRequest);
    } else {
        return;
    }
}

int push_back(Buffer * buffer, Request * request){
    //master_lock(buffer->size);///
    if (buffer->sizeAllowed != buffer->numOfItems){
        //waiting_requests++;
        request->prev = buffer->tail;
        if (buffer->head->next == NULL){//it's the first item in the list
            buffer->head->next = request;
        }
        buffer->tail->next = request;
        buffer->tail = request;
        buffer->tail->next = NULL;
        buffer->numOfItems++;
        printf("pushed to buffer\n");
        //master_unlock();
        return 1;
    }
    else {
        //master_unlock();///
        return -1;
    }
}

void pop_byReqID(Buffer * buffer, Request * request) {
    if (isEmpty(*buffer) != 0) {
        Request *iterator = buffer->head;
        while (iterator != NULL) {
            if (iterator->requestID == request->requestID) {
                Request *next = iterator->next;
                Request *prev = iterator->prev;
                iterator->prev->next = next;
                if (iterator->next)
                    iterator->next->prev = prev;
                iterator->next = NULL;
                iterator->prev = NULL;
                free(iterator);
                buffer->numOfItems--;
                break;
            } else {
                iterator = iterator->next;
            }

        }
    }
}

///###########################################################3##############################
struct args {
    Buffer * waiting;
    Buffer * handled;
    Thread * threads;
    int threadArraySize;
};

typedef struct args Args;
///###########################################################3##############################
void* threadFunction(void * argsV){
    printf("thread here, waiting...\n");
    thread_lock();
    pthread_t threadID2 = pthread_self();
    printf("thread number %ld is here!\n",threadID2);
    Args * args = (Args *)argsV;
    waiting_requests--;
    Request * request = pop_front(args->waiting);
    push_back(args->handled, request);
    handled_requests++;
    gettimeofday(&request->dispatch_time, NULL);
    request->dispatch_time.tv_sec -= request->arrival_time.tv_sec;
    request->dispatch_time.tv_usec -= request->arrival_time.tv_usec;
    handled_requests = args->handled->numOfItems;
    waiting_requests = args->waiting->numOfItems;
    printf("thread is about to unlock!\n");
    thread_unlock();

    int is_static = requestHandle(request->requestID);
    printf("handle!\n");

    thread_lock();
    printf("lock again!\n");
    pop_byReqID(args->handled,request);
    handled_requests--;
    printf("unlock again!\n");
    thread_unlock();

    ///stats of thread for request
    pthread_t threadID = pthread_self();
    for (int i=0; i<args->threadArraySize; i++) {
        printf("STATS\n");
        if (args->threads[i].pidT == threadID){
            args->threads[i].handle_counter++;
            if (is_static == 1){//static
                args->threads[i].static_counter++;
            }
            else if (is_static == 0){
                args->threads[i].dynamic_counter++;
            }
        }
    }
    printf("finished request...closing\n");
    Close(request->requestID);
    return NULL;
};


int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen;
    struct sockaddr_in clientaddr;

    getargs(&port, argc, argv);

    int numberOfThreads = atoi(argv[2]);
    int numberOfRequests = atoi(argv[3]);

    //create buffers
    Buffer * waitingBuffer = malloc (sizeof(Buffer));
    if (!waitingBuffer)
        printf("error waitingBuffer malloc\n");
    Buffer * handledBuffer = malloc (sizeof(Buffer));
    if (!handledBuffer){
        printf("error handledBuffer malloc\n");
        free(waitingBuffer);
        return -1;
    }
    int result = buffer_init(waitingBuffer, numberOfRequests); //initialize the buffer
    if (result == -1) {
        printf("error waitingBuffer buffer_init malloc\n");
        free(waitingBuffer);
        free(handledBuffer);
        return -1;
    }
    result = buffer_init(handledBuffer, numberOfThreads); //initialize the buffer
    if (result == -1){
        printf("error handledBuffer buffer_init malloc\n");
        destroyRequests(waitingBuffer);
        free(waitingBuffer);
        free(handledBuffer);
        return -1;
    }

    //initialization of conds and mutexs
    readers_writers_init();


    Thread * threadsArray = malloc (sizeof(Thread)*numberOfThreads);
    if(!threadsArray){
        printf("error threadsArray malloc\n");
        //handle error
    }

    Args * threadFunctionArgs = malloc(sizeof (Args));
    if (!threadFunctionArgs){
        printf("error threadFunctionArgs malloc\n");
        //handle error
    }

    threadFunctionArgs->waiting = waitingBuffer;
    threadFunctionArgs->handled = handledBuffer;
    threadFunctionArgs->threads = threadsArray;
    threadFunctionArgs->threadArraySize = numberOfThreads;

    //create array of threads
    for (int i=0; i<numberOfThreads; i++){
        pthread_t thread;
        int result = pthread_create(&thread, NULL, threadFunction,(void*)&threadFunctionArgs);///maybe pop_front?
        ///insert the thread to the array
        threadsArray[i].pidT = thread;
        threadsArray[i].index = i;
        threadsArray[i].dynamic_counter = 0;
        threadsArray[i].handle_counter = 0;
        threadsArray[i].static_counter = 0;
    }

    listenfd = Open_listenfd(port);
    while (1) {
        printf("IM HERE1\n");
	clientlen = sizeof(clientaddr);
        printf("IM HERE2\n");
	connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
        printf("IM HERE3\n");

	// 
	// HW3: In general, don't handle the request in the main thread.
	// Save the relevant info in a buffer and have one of the worker threads 
	// do the work. 
	//
	Request * newRequest = malloc(sizeof (Request));
	if (!newRequest){
        printf("error newRequest malloc\n");
	    ///need to return bad request?
	}
	newRequest->requestID = connfd;
	gettimeofday(&newRequest->arrival_time, NULL);

	if (waitingBuffer->numOfItems + handledBuffer->numOfItems >= numberOfRequests){///maybe need to handle this only in the lock functions???
        char* overloadMethod = argv[4];

        if(!strcmp(overloadMethod, "block")){

        }else if(!strcmp(overloadMethod, "dt")){
            pop_back(waitingBuffer);///???
        }else if(!strcmp(overloadMethod, "dh")){
            pop_front(waitingBuffer);///???
        }else if(!strcmp(overloadMethod, "random")){

        }
        printf("OVERLOAD\n");
        Close(connfd);
	}
	else{
        printf("locking and pushing\n");
        master_lock(waitingBuffer->sizeAllowed);
        int result =  push_back(waitingBuffer, newRequest);//adds the request to the buffer(there is master_lock & unlock inside
        waiting_requests++;
        sleep(3);
        master_unlock();
        sleep(10);
	}

	//requestHandle(connfd);///how, where & when to make the request handling? where to do the pop from the waiting_buffer to the handling_buffer
	                    //and make the thread to do the requestHandle?

	//Close(connfd);
    }
    ///join thread
    for (int i=0; i<numberOfThreads; i++){
        int result = pthread_join(threadsArray[i].pidT, NULL);
    }

}


    


 
