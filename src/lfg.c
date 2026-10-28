#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include "lfg.h"

sem_t dungeon_instances;
sem_t tank_queue;
sem_t healer_queue;
sem_t dps_queue;

/*
n - maximum number of concurrent instances
t - number of tank players in the queue
h - number of healer players in the queue
d - number of DPS players in the queue
t1 - minimum time before an instance is finished
t2 - maximum time before an instance is finished 
*/

//intialize variables for the reports
typedef struct {
    bool is_active;        // 1= active, 0= empty
    int total_parties;    // parties served by instance
    int total_time;       // time served
} instance_slot_t;

// struct passed into each worker thread
typedef struct {
    int party_id;
    int clear_time;
} party_args_t;

// global array 
static instance_slot_t *instances = NULL;
static int global_n = 0;                  // number of instances
static int total_parties_formed = 0;       // number of total parties 

static pthread_mutex_t instance_mutex = PTHREAD_MUTEX_INITIALIZER; // protects instances array and print statements
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;      // proetcts thread log output

//CURRENT STATUS OF ALL AVAILABLE INSTANCES - EMPTY / ACTIVE
void print_instance_status(void)        
{
    printf("-- Current Instance Status --\n");
    for (int i = 0; i < global_n; i++) 
    {
        printf("  Instance %d: %s\n", i + 1, instances[i].is_active ? "active" : "empty");
    }
    printf("-------------------------------\n");
}

// STATUS SUMMARY
void print_summary(double total_wall_time)
{
    printf("\n=======================================================\n");
    printf("              FINAL SUMMARY STATISTICS        \n");
    printf("=======================================================\n");
    
    int cumulative_instance_time = 0;
    for (int i = 0; i < global_n; i++) {
        printf("Instance %d served: %d parties | Total time served: %d seconds\n",
               i + 1, instances[i].total_parties, instances[i].total_time);
        cumulative_instance_time += instances[i].total_time;
    }
    
    printf("=======================================================\n");
    printf("Total Parties Served Across All Instances : %d\n", total_parties_formed);
    printf("Cumulative Instance Active Time           : %d seconds\n", cumulative_instance_time);
    printf("Actual Elapsed Time            : %.0f seconds\n", total_wall_time);
    printf("=======================================================\n\n");
}

//matchmakes - handles 1 team
void *matchmaker_thread(void *arg)         
{                

    // unpack struct argument
    party_args_t *args = (party_args_t *)arg;
    int party_id = args->party_id;
    int clear_time = args->clear_time;
    free(args); //argument heap allocation

    //1 CLAIM ROLES - assigned a team

    //decrements available
    sem_wait(&tank_queue);    
    sem_wait(&healer_queue);  
    sem_wait(&dps_queue);     
    sem_wait(&dps_queue);     
    sem_wait(&dps_queue);     

    pthread_mutex_lock(&log_mutex);
    printf("[Party %d] Formed! Waiting for an available dungeon instance...\n", party_id);
    pthread_mutex_unlock(&log_mutex);

    //wait for dungeon instance slot
    sem_wait(&dungeon_instances);

    // FIND AN EMPTY SLOT and MARK AS active
    int assigned_instance = -1;

    pthread_mutex_lock(&instance_mutex);
    for (int i = 0; i < global_n; i++) {
        if (!instances[i].is_active) 
        {
            instances[i].is_active = true;
            assigned_instance = i;
            break;
        }
    }

    // update instance stats
    instances[assigned_instance].total_parties++;
    instances[assigned_instance].total_time += clear_time;

    printf("\n -> [Party %d] ENTERED Instance %d (Clear time: %ds)\n", 
           party_id, assigned_instance + 1, clear_time);
    print_instance_status(); // display status upon entry
    pthread_mutex_unlock(&instance_mutex);

    sleep(clear_time);

    // leave and mark as empty
    pthread_mutex_lock(&instance_mutex);
    instances[assigned_instance].is_active = false;
    printf("\n <- [Party %d] CLEARED Instance %d\n", party_id, assigned_instance + 1);
    print_instance_status(); //print status
    pthread_mutex_unlock(&instance_mutex);

    //release instance slot
    sem_post(&dungeon_instances);

    return NULL;
}

void engine(int n, int t, int h, int d, int t1, int t2) 
{
    srand(time(NULL)); // Seed RNG for random clear times 

    global_n = n;

    //initialize semaphores
    sem_init(&dungeon_instances, 0, n); // n available dungeon slots
    sem_init(&tank_queue, 0, t);        // t tanks available
    sem_init(&healer_queue, 0, h);      // h healers available
    sem_init(&dps_queue, 0, d);         // d DPS available


// allocate instances array on the heap
    instances = calloc(n, sizeof(instance_slot_t));
    if (!instances) {
        perror("Failed to allocate instances array");
        return;
    }

    //there should be 1 tank, 1 healer, and 3 DPS per party
    int dps_parties = d / 3; // each party requires 3 DPS players
    int max_parties = t;
    if (h < max_parties) max_parties = h;
    if (dps_parties < max_parties) max_parties = dps_parties;

    total_parties_formed = max_parties;

    if (max_parties == 0) {
        printf("\nInsufficient queue players to form a full 5-player party (1T, 1H, 3D).\n");
        cleanup();
        return;
    }

    printf("\n--- Starting LFG Matchmaker (%d Parties Possible, %d Instance Slots) ---\n\n", max_parties, n);

    time_t start_time = time(NULL);

    // allocate thread handles array
    pthread_t *threads = malloc(sizeof(pthread_t) * max_parties);

    // SPAWN MATCHMAKER THREADS CONCURRENTLY
    for (int i = 0; i < max_parties; i++) {
        party_args_t *args = malloc(sizeof(party_args_t));
        args->party_id = i + 1;
        args->clear_time = t1;
        if (t2 > t1) {
            args->clear_time = t1 + rand() % (t2 - t1 + 1);
        }

        pthread_create(&threads[i], NULL, matchmaker_thread, args);
    }

    // JOIN THREADS (Wait for all parties to clear dungeons)
    for (int i = 0; i < max_parties; i++) {
        pthread_join(threads[i], NULL);
    }

    //
    time_t end_time = time(NULL);
    double total_time = difftime(end_time, start_time);

    free(threads);

    print_summary(total_time);
    cleanup();

}

//resource cleanup
void cleanup(void) 
{
    //destroy semaphore
    sem_destroy(&dungeon_instances);
    sem_destroy(&tank_queue);
    sem_destroy(&healer_queue);
    sem_destroy(&dps_queue);

    //destroy mutex
    pthread_mutex_destroy(&log_mutex);
    pthread_mutex_destroy(&instance_mutex);

    // free heap memory for instance tracking
    if (instances != NULL) {
        free(instances);
        instances = NULL;
    }
}




