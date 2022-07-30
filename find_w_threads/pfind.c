
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>
/*in order to use opendir, readdir*/
#include <dirent.h>
/*in order use lstat, info taken from "https://linux.die.net/man/2/lstat" */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define SUCCESS 0

#define DIRECTORY_CANT_BE_SEARCHED(dir) printf("Directory %s: Permission denied.\n", dir)

#define SUMMERY(nof_found) printf("Done searching, found %d files\n", nof_found)

#define IS_DIR(mode) S_ISDIR(mode)

/*from the forum its enough to check read permission for the owner*/
#define IS_DIR_SEARCHABLE(mode) ((mode) & S_IRUSR)

typedef enum{ALL_THREAD_DEAD, ALL_THREAD_SLEEPING, NONE}terminate_conditions_t;

/*=============================================================================*/
								//directory queue imp//
/*=============================================================================*/

typedef struct dir_queue_node{
    char dir[PATH_MAX];
    struct dir_queue_node* next;
}dir_queue_node;

typedef struct dir_queue{
    int size;
    dir_queue_node* first;
    dir_queue_node* last;
}dir_queue;

char push_to_back(dir_queue* q, char* dir){
    dir_queue_node* new_node = (dir_queue_node*) malloc(sizeof(dir_queue_node));
    if(!new_node){
        return !SUCCESS;
    }
    new_node->next = NULL;
    strcpy(new_node->dir, dir);
    if(!(q->size)){ /*q is empty*/
        q->first = new_node;
        q->last = q->first;
    }
    else{
        q->last->next = new_node;
        q->last = q->last->next;
    }
    (q->size)++;
    return SUCCESS;
}

char* pop_from_front(dir_queue* q){
    dir_queue_node* node2pop = q->first;
    q->first = q->first->next;
    if(node2pop == q->last) q->last = q->first;
    char* path = (char*) malloc(sizeof(char)* PATH_MAX);
    if(!path){
        return NULL;
    }
    strcpy(path, node2pop->dir);
    free(node2pop);
    (q->size)--;
    return path;
}

dir_queue* create_queue(){
    dir_queue* q = (dir_queue*) malloc(sizeof(dir_queue));
    if(!q){
        return NULL;
    }
    q->first = NULL;
    q->last = q->first;
    q->size = 0;
    return q;
}

void destroy_queue(dir_queue* q){
    char* dir;
    while(q->size) {
        /*should never get here... just in case*/
        dir = pop_from_front(q);
        if(!dir) free(dir);
    }
    free(q);
}

/*=============================================================================*/
								//global fields//
/*=============================================================================*/
int nof_threads;
const char* pattern2search;
pthread_mutex_t q_lock;
dir_queue* directory_q;
pthread_cond_t not_empty;
pthread_mutex_t nof_idle_lock;
int nof_idle;  /*keep track of number of sleeping threads*/
pthread_cond_t idle_number_increased; /*notify main thread when thread is sleeping*/
pthread_mutex_t nof_active_lock;
int nof_active;  /*keep track of number of active threads (threads which havn't terminate)*/
pthread_mutex_t nof_matches_lock;
int nof_matches; /*keeps track of number of files containing pattern2search*/
pthread_cond_t not_empty_low_priority;
int nof_idle_low_priority;
/*used to give priority to sleeping threads and to force threads to wait at the begining*/
pthread_mutex_t nof_idle_low_priority_lock;
char ready_set_go; /*main thread uses it to force threads to wait at the begining*/
pthread_mutex_t ready_set_go_lock;
pthread_cond_t may_the_odds_be_ever_in_your_favor;
/*=============================================================================*/
								//functions//
/*=============================================================================*/

/*validates the inputs and pushes the root directory to the queue*/
char process_inputs(int argc, char* argv[]){
    if(argc < 4){
        fprintf(stderr ,"ERROR: expected 3 arguments but got only %d\n", argc);
        return !SUCCESS;
    }
    struct stat file_info;
    /*is identical to stat(), except that if path is a symbolic link,
    then the link itself is stat-ed, not the file that it refers to.*/
    if(lstat(argv[1], &file_info) == -1){
        fprintf(stderr ,"ERROR: fail to get %s stats\n", argv[1]);
        return !SUCCESS;
    }
    if(!IS_DIR(file_info.st_mode) || !IS_DIR_SEARCHABLE(file_info.st_mode)){
        fprintf(stderr, "file %s is not a directory or not a searchable directory!\n", argv[1]);
        return !SUCCESS;
    }
    pattern2search = argv[2];
    nof_threads = atoi(argv[3]);

    if(argv[1][strlen(argv[1]) -1] == '/')  argv[1][strlen(argv[1]) -1] = 0; /*getting rid of redundant '/' */
    pthread_mutex_lock(&q_lock);
    if( push_to_back(directory_q, argv[1])){
        fprintf(stderr, "FAILED to insert element %s to the queue\n", argv[1]);
        return !SUCCESS;
    }
    pthread_mutex_unlock(&q_lock);

    return SUCCESS;
}

char search_dir(char* path){
    DIR *dir;
    struct dirent *dp;
    struct stat file_info;
    /*is identical to stat(), except that if path is a symbolic link,
    then the link itself is stat-ed, not the file that it refers to.*/
    if(lstat(path, &file_info) == -1){
        return !SUCCESS;
    }
    if((dir = opendir(path)) == NULL){
        return !SUCCESS;
    }
    char curr_path[PATH_MAX];
    /*going over files in dir*/
    while((dp = readdir(dir)) != NULL){
        /*ignoring '.' and '..' directories*/
        if(strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) continue;
        strcpy(curr_path, path);
        strcat(curr_path, "/");
        strcat(curr_path, dp->d_name);
        if(lstat(curr_path, &file_info) == -1){
            closedir(dir);
            return !SUCCESS;
        }
        if(IS_DIR(file_info.st_mode)){
            /*checking permission to search directory*/
            if(!IS_DIR_SEARCHABLE(file_info.st_mode)){
                DIRECTORY_CANT_BE_SEARCHED(curr_path);
                continue;
            }
            pthread_mutex_lock(&q_lock);
            if( push_to_back(directory_q, curr_path) != SUCCESS){
                pthread_mutex_unlock(&q_lock);
                closedir(dir);
                return !SUCCESS;
            }
            pthread_mutex_unlock(&q_lock);
            pthread_cond_signal(&not_empty);
        }
        else{ /*found a file*/
            if(strstr(dp->d_name, pattern2search) != NULL){ /*found a match*/
                printf("%s\n",curr_path);
                pthread_mutex_lock(&nof_matches_lock);
                nof_matches++;
                pthread_mutex_unlock(&nof_matches_lock);
            }
        }
    }
    closedir(dir);
    return SUCCESS;
}

void* thread_run(){
    /*waiting for inital signal from main*/
    pthread_mutex_lock(&ready_set_go_lock);
    if(!ready_set_go){
        pthread_cond_wait(&may_the_odds_be_ever_in_your_favor,&ready_set_go_lock);
    }
    pthread_mutex_unlock(&ready_set_go_lock);

    while(1){

        pthread_mutex_lock(&nof_idle_lock);
        /*if there are threads sleeping , prioritize them*/
        if(nof_idle) {
            pthread_mutex_unlock(&nof_idle_lock);
            pthread_mutex_lock(&nof_idle_low_priority_lock);
            nof_idle_low_priority++;
            pthread_cond_broadcast(&idle_number_increased);
            pthread_cond_wait(&not_empty_low_priority,&nof_idle_low_priority_lock);
            nof_idle_low_priority--;
            pthread_mutex_unlock(&nof_idle_low_priority_lock);
        }
        else pthread_mutex_unlock(&nof_idle_lock);

        pthread_mutex_lock(&q_lock);
        while(!(directory_q->size)){

            pthread_mutex_lock(&nof_idle_lock);
            nof_idle++;
            /*notify main thread that I'm going to sleep now*/
            pthread_cond_broadcast(&idle_number_increased);
            pthread_mutex_unlock(&nof_idle_lock);
            pthread_cond_wait(&not_empty,&q_lock);
 
            pthread_mutex_lock(&nof_idle_lock);
            nof_idle--;
            pthread_mutex_unlock(&nof_idle_lock);
        }
        pthread_cond_broadcast(&not_empty_low_priority);
        char* curr_path = pop_from_front(directory_q);
        if(!curr_path){
            pthread_mutex_lock(&nof_active_lock);
            nof_active--;
            pthread_mutex_unlock(&nof_active_lock);
            pthread_mutex_unlock(&q_lock);
            fprintf(stderr, "THREAD ERROR: failed to get item from queue\n");
            pthread_cond_broadcast(&idle_number_increased);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&q_lock);
        if(search_dir(curr_path) != SUCCESS){
            pthread_mutex_lock(&nof_active_lock);
            nof_active--;
            pthread_mutex_unlock(&nof_active_lock);
            fprintf(stderr, "THREAD ERROR: failed to process file %s\n",curr_path);
            free(curr_path);
            pthread_cond_broadcast(&idle_number_increased);
            pthread_exit(NULL);
        }
        free(curr_path);
    }
}


terminate_conditions_t check_if_nof_idle_equals_nof_active(){
    int curr_nof_idle, curr_nof_idle_low_priority, curr_nof_active;

    pthread_mutex_lock(&nof_active_lock);
    curr_nof_active = nof_active;
    pthread_mutex_unlock(&nof_active_lock);

    pthread_mutex_lock(&nof_idle_lock);
    curr_nof_idle = nof_idle;
    pthread_mutex_unlock(&nof_idle_lock);

    pthread_mutex_lock(&nof_idle_low_priority_lock);
    curr_nof_idle_low_priority = nof_idle_low_priority;
    pthread_mutex_unlock(&nof_idle_low_priority_lock);

    if(curr_nof_active == 0) return ALL_THREAD_DEAD;
    else if (curr_nof_active == curr_nof_idle_low_priority + curr_nof_idle) return ALL_THREAD_SLEEPING;
    return NONE;
}


/*=============================================================================*/
								//main//
/*=============================================================================*/

int main(int argc, char* argv[]) {
    pthread_t* threads;
    int rc;
    directory_q = create_queue();
    nof_idle = 0;
    ready_set_go = 0;
    nof_idle_low_priority = 0;
    nof_matches = 0;
    terminate_conditions_t cond;
    if(!directory_q){
        fprintf(stderr, "FAILED to create queue\n");
        exit(!SUCCESS);
    }
    if(process_inputs(argc, argv)) exit(!SUCCESS);
    char ret = 0;
    /*init mutex and condition variable*/
    if (pthread_mutex_init(&q_lock, NULL) != 0)                             ret = 1;
    if (pthread_cond_init(&not_empty, NULL) != 0)                           ret = 1;
    if (pthread_mutex_init(&nof_active_lock, NULL) != 0)                    ret = 1;
    if (pthread_mutex_init(&nof_idle_lock, NULL) != 0)                      ret = 1;
    if (pthread_cond_init(&idle_number_increased, NULL) != 0)               ret = 1;
    if (pthread_mutex_init(&nof_matches_lock, NULL) != 0)                   ret = 1;
    if (pthread_cond_init(&not_empty_low_priority, NULL) != 0)              ret = 1;
    if (pthread_mutex_init(&nof_idle_low_priority_lock, NULL) != 0)         ret = 1;
    if (pthread_mutex_init(&ready_set_go_lock, NULL) != 0)                  ret = 1;
    if (pthread_cond_init(&may_the_odds_be_ever_in_your_favor, NULL) != 0)  ret = 1;
    
    if(ret) {
        fprintf(stderr, "FAILED to create locks\n");
        exit(!SUCCESS);
    }
    /*init threads*/
    threads = (pthread_t*) malloc(sizeof(pthread_t)*nof_threads);
    if(!threads){
        fprintf(stderr, "FAILED to create threads\n");
        exit(!SUCCESS);
    }
    for(int i=0; i < nof_threads; i++){
        rc = pthread_create(&threads[i], NULL, &thread_run, NULL);
        if (rc) {
            fprintf(stderr, "ERROR in pthread_create(): %s\n", strerror(rc));
            exit(!SUCCESS);
        }
    }
    pthread_mutex_lock(&nof_active_lock);
    nof_active = nof_threads;
    pthread_mutex_unlock(&nof_active_lock);

    pthread_mutex_lock(&ready_set_go_lock);
    ready_set_go = 1;
    pthread_mutex_unlock(&ready_set_go_lock);
    /*signal threads to begin*/
    pthread_cond_broadcast(&may_the_odds_be_ever_in_your_favor);
    /*main waits for all threads to finish or untill all threads have terminated*/
    while(1){
        if( (cond=check_if_nof_idle_equals_nof_active()) != NONE){
            pthread_mutex_lock(&q_lock);
            /*if all threads are sleeping but there is still job to do*/
            if(directory_q->size && cond != ALL_THREAD_DEAD){
                pthread_mutex_unlock(&q_lock);
                pthread_cond_broadcast(&not_empty);
            }
            else break;
        }
        pthread_mutex_lock(&q_lock);
        if(directory_q->size == 0){
            pthread_mutex_unlock(&q_lock);
            continue; //check_if_nof_idle_equals_nof_active agian!
        }
        pthread_mutex_unlock(&q_lock);
        pthread_mutex_lock(&nof_idle_lock);
        pthread_cond_wait(&idle_number_increased, &nof_idle_lock);
        pthread_mutex_unlock(&nof_idle_lock);
    }
    pthread_mutex_unlock(&q_lock);
    /*cancle all threads at the end. code taken from: https://www.geeksforgeeks.org/pthread_cancel-c-example/ */
    for(int i=0; i<nof_threads; i++){
        pthread_cancel(threads[i]);
    }
    /*destroy locks and cvs*/
    pthread_mutex_destroy(&q_lock);
    pthread_cond_destroy(&not_empty);
    pthread_mutex_destroy(&nof_active_lock);
    pthread_mutex_destroy(&nof_idle_lock);
    pthread_cond_destroy(&idle_number_increased);
    pthread_mutex_destroy(&nof_matches_lock);
    pthread_cond_destroy(&not_empty_low_priority);
    pthread_mutex_destroy(&nof_idle_low_priority_lock);
    pthread_mutex_destroy(&ready_set_go_lock);
    pthread_cond_destroy(&may_the_odds_be_ever_in_your_favor);
    destroy_queue(directory_q);
    free(threads);

    SUMMERY(nof_matches);
    /*no need for a lock at this point since all threads are cancled*/
    if(nof_active < nof_threads) exit(!SUCCESS); /*error occur in some thread*/
    exit(SUCCESS);
}