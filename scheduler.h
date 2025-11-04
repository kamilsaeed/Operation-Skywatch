#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "utils.h"
#include <time.h> // --- NEW: For stats

// --- Assignment Constants ---
#define RR_QUANTUM 5        // Default 5-second time quantum for Q2
#define AGING_THRESHOLD 10  // 10-second wait in Q3 before promotion

// --- MODIFIED: Added fields for statistics ---
struct SchedulerJet {
    pid_t pid;
    int atc_read_fd;
    int atc_write_fd;
    int fuel;
    JetStatus status;
    int time_on_runway;
    int time_in_q3;

    // --- NEW: Fields for statistics ---
    time_t arrival_time;
    time_t first_run_time; // 0 if not run yet
    int total_wait_time;
};

// --- MODIFIED: Added fields for statistics ---
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

    // --- NEW: Fields for statistics ---
    int total_context_switches;
    double total_runway_busy_time; // in seconds
};

// --- Function Declarations ---

void scheduler_init(SchedulerState* s);
void scheduler_add_jet(SchedulerState* s, pid_t pid, int read_fd, int write_fd, int fuel, FILE* log_file);

// --- MODIFIED: Reverted - 2 arguments, console print is back on
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

