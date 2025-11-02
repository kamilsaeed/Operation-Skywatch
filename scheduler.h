#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "utils.h"

// --- Assignment Constants ---
#define RR_QUANTUM 5        // Default 5-second time quantum for Q2
#define AGING_THRESHOLD 10  // 15-second wait in Q3 before promotion

// ... (SchedulerJet struct is unchanged) ...
struct SchedulerJet {
    pid_t pid;
    int atc_read_fd;
    int atc_write_fd;
    int fuel;
    JetStatus status;
    int time_on_runway;
    int time_in_q3;
};

// ... (SchedulerState struct is unchanged) ...
struct SchedulerState 
{
    SchedulerJet queue1[MAX_JETS]; // Q1: SRTF
    SchedulerJet queue2[MAX_JETS]; // Q2: RR
    SchedulerJet queue3[MAX_JETS]; // Q3: FCFS

    int q1_count;
    int q2_count;
    int q3_count;
    
    bool is_runway_busy;
    pid_t runway_jet_pid;
    int runway_jet_q;
    
    int q2_rr_quantum;
    bool is_paused;     
    
    pthread_mutex_t lock;
};

// --- Function Declarations ---

void scheduler_init(SchedulerState* s);
void scheduler_add_jet(SchedulerState* s, pid_t pid, int read_fd, int write_fd, int fuel);
void scheduler_print_queues(SchedulerState* s, FILE* log_file);
void scheduler_destroy(SchedulerState* s);
void scheduler_tick(SchedulerState* s, FILE* log_file); 
void scheduler_jet_landed_unsafe(SchedulerState* s, pid_t pid, FILE* log_file); 
void scheduler_handle_emergency_unsafe(SchedulerState* s, pid_t pid, int current_fuel, FILE* log_file); 

// --- NEW: Handle refuel request ---
void scheduler_handle_refuel_request_unsafe(SchedulerState* s, pid_t pid, int current_fuel, FILE* log_file);

// --- Helper functions ---
SchedulerJet* scheduler_find_jet_unsafe(SchedulerState* s, pid_t pid, int* out_q, int* out_idx);
bool scheduler_move_jet_unsafe(SchedulerState* s, int from_q, int from_idx, int to_q, FILE* log_file);

#endif // SCHEDULER_H