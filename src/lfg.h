#ifndef LFG_H
#define LFG_H

#include <pthread.h>
#include <semaphore.h>

// Shared semaphores
extern sem_t dungeon_instances;
extern sem_t tank_queue;
extern sem_t healer_queue;
extern sem_t dps_queue;

// Function Prototypes
void engine(int n, int t, int h, int d, int t1, int t2);
void *matchmaker_thread(void *arg);
void print_instance_status(void);
void print_summary(double total_wall_time);
void cleanup(void);

#endif // LFG_H