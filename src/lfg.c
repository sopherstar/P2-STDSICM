//Contains how the synchronization logic works

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

// Argument structure passed into each worker thread
//UNDERSTAND
typedef struct {
    int party_id;
    int t1;
    int t2;
} party_args_t;

// Global array allocated dynamically in engine based on n - TO-DO : don't i already have this in matchmaker
static instance_slot_t *instances = NULL;
static int global_n = 0;                  // number of instances
static int total_parties_formed = 0;       // number of total parties 

static pthread_mutex_t instance_mutex = PTHREAD_MUTEX_INITIALIZER; // Protects instances array & print statements
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;      // Protects thread log output

//CURRENT STATUS OF ALL AVAILABLE INSTANCES - EMPTY / ACTIVE
//UNDERTAND: EVERY WHEN IS THIS SHOWN
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
    printf("Actual Real-World Elapsed Time            : %.0f seconds\n", total_wall_time);
    printf("=======================================================\n\n");
}

//matchmakes - handles 1 team
void *matchmaker_thread(void *arg)          //UNDERSTAND - where did arg come from and why *
{                

    // 1. Unpack struct argument passed via pthread_create UNDERSTAND
    party_args_t *args = (party_args_t *)arg;
    int party_id = args->party_id;
    int t1 = args->t1;
    int t2 = args->t2;
    free(args); // Free argument heap allocation once unpacked

    //1 CLAIM ROLES - assigned a team

    sem_wait(&tank_queue);    // Decrements available tanks by 1
    sem_wait(&healer_queue);  // Decrements available healers by 1
    sem_wait(&dps_queue);     // Decrements available DPS by 1
    sem_wait(&dps_queue);     // Decrements available DPS by 1
    sem_wait(&dps_queue);     // Decrements available DPS by 1


    //UPDATE 

    pthread_mutex_lock(&log_mutex);
    printf("[Party %d] Formed! Waiting for an available dungeon instance...\n", party_id);
    pthread_mutex_unlock(&log_mutex);

    //2 Wait for Dungeon Instance Slot (sem_wait)
    sem_wait(&dungeon_instances);

    // 4. FIND AN EMPTY SLOT (0 to n-1) & MARK AS "active"
    int assigned_instance = -1;

    //TO-DO: IS THIS CLEAR TIME GOOD
    int clear_time = t1;
    if (t2 > t1) {
        clear_time = t1 + rand() % (t2 - t1 + 1);
    }

    pthread_mutex_lock(&instance_mutex);
    for (int i = 0; i < global_n; i++) {
        if (!instances[i].is_active) {
            instances[i].is_active = true;
            assigned_instance = i;
            break;
        }
    }

    // Update instance stats
    instances[assigned_instance].total_parties++;
    instances[assigned_instance].total_time += clear_time;

    printf("\n -> [Party %d] ENTERED Instance %d (Clear time: %ds)\n", 
           party_id, assigned_instance + 1, clear_time);
    print_instance_status(); // Display status update upon entry
    pthread_mutex_unlock(&instance_mutex);

    //4 Simulate Run -> sleep(t)
    sleep(clear_time);

    // 6. LEAVE DUNGEON & MARK SLOT AS "empty"
    pthread_mutex_lock(&instance_mutex);
    instances[assigned_instance].is_active = false;
    printf("\n <- [Party %d] CLEARED Instance %d\n", party_id, assigned_instance + 1);
    print_instance_status(); // Display status update upon exit
    pthread_mutex_unlock(&instance_mutex);

    //5 Leave Dungeon -> Release Instance Slot (sem_post)
    sem_post(&dungeon_instances);

    return NULL;
}

// Implementation Function 1: Initialization / Engine Entry
void engine(int n, int t, int h, int d, int t1, int t2) 
{
    srand(time(NULL)); // UNDERSTAND Seed RNG for random clear times 

    global_n = n; // Save n for global status functions

    //initialize semaphores
    sem_init(&dungeon_instances, 0, n); // n available dungeon slots
    sem_init(&tank_queue, 0, t);        // t tanks available
    sem_init(&healer_queue, 0, h);      // h healers available
    sem_init(&dps_queue, 0, d);         // d DPS available


// 2. Allocate instances array on the heap
    instances = calloc(n, sizeof(instance_slot_t));
    if (!instances) {
        perror("Failed to allocate instances array");
        return;
    }

    //there should be 1 tank, 1 healer, and 3 DPS per party. 
    int dps_parties = d / 3; // each party requires 3 DPS players
    int max_parties = t;
    if (h < max_parties) max_parties = h;
    if (dps_parties < max_parties) max_parties = dps_parties;
    //max_parties = min(t, h, dps_parties); 

    total_parties_formed = max_parties;

    if (max_parties == 0) {
        printf("\nInsufficient queue players to form a full 5-player party (1T, 1H, 3D).\n");
        cleanup();
        return;
    }

    printf("\n--- Starting LFG Matchmaker (%d Parties Possible, %d Instance Slots) ---\n\n", max_parties, n);

    time_t start_time = time(NULL);

    // 4. Allocate thread handles array
    pthread_t *threads = malloc(sizeof(pthread_t) * max_parties);

    // 5. SPAWN MATCHMAKER THREADS CONCURRENTLY
    for (int i = 0; i < max_parties; i++) {
        party_args_t *args = malloc(sizeof(party_args_t));
        args->party_id = i + 1;
        args->t1 = t1;
        args->t2 = t2;

        pthread_create(&threads[i], NULL, matchmaker_thread, args);
    }

    // 6. JOIN THREADS (Wait for all parties to clear dungeons)
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

    // 3. Free dynamically allocated heap memory for instance tracking - UNDERSTAND 
    if (instances != NULL) {
        free(instances);
        instances = NULL; // Prevent dangling pointer
    }
}





