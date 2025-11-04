#include "scheduler.h"
#include <stdarg.h>

// Helper function for logging within the scheduler
static void log_scheduler_event(FILE* log_file, const char* format, ...) {
    if (log_file) {
        time_t now = time(0);
        tm *ltm = localtime(&now);
        char time_buf[20];
        snprintf(time_buf, 20, "[%02d:%02d:%02d] ", ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
        fprintf(log_file, "%s", time_buf);
        va_list args;
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        fflush(log_file);
    }
}


// --- Helper Functions (Internal) ---

static int find_empty_slot(SchedulerJet queue[]) {
    for (int i = 0; i < MAX_JETS; i++) {
        if (queue[i].pid == 0) { return i; }
    }
    return -1;
}

SchedulerJet* scheduler_find_jet_unsafe(SchedulerState* s, pid_t pid, int* out_q, int* out_idx) {
    SchedulerJet* queues[] = { s->queue1, s->queue2, s->queue3 };
    for (int q = 0; q < 3; q++) {
        for (int i = 0; i < MAX_JETS; i++) {
            if (queues[q][i].pid == pid) {
                if (out_q) *out_q = q + 1;
                if (out_idx) *out_idx = i;
                return &queues[q][i];
            }
        }
    }
    return NULL;
}

bool scheduler_move_jet_unsafe(SchedulerState* s, int from_q, int from_idx, int to_q, FILE* log_file) {
    SchedulerJet* from_queue_arr[] = { s->queue1, s->queue2, s->queue3 };
    SchedulerJet* to_queue_arr[] = { s->queue1, s->queue2, s->queue3 };
    int* from_count[] = { &s->q1_count, &s->q2_count, &s->q3_count };
    int* to_count[] = { &s->q1_count, &s->q2_count, &s->q3_count };

    SchedulerJet* jet_to_move = &from_queue_arr[from_q - 1][from_idx];
    pid_t pid = jet_to_move->pid;
    
    int to_slot = find_empty_slot(to_queue_arr[to_q - 1]);
    if (to_slot == -1) {
        log_scheduler_event(log_file, "[Scheduler]: ERROR: Queue %d is full. Cannot move jet %d.\n", to_q, pid);
        return false;
    }

    // Use memcpy to move all data, including new stats
    memcpy(&to_queue_arr[to_q - 1][to_slot], jet_to_move, sizeof(SchedulerJet));
    memset(jet_to_move, 0, sizeof(SchedulerJet));
    
    (*from_count[from_q - 1])--;
    (*to_count[to_q - 1])++;
    
    // IMPORTANT: Reset status and timer when moving
    to_queue_arr[to_q - 1][to_slot].status = (to_q == 3) ? jet_to_move->status : STATUS_IN_QUEUE; // Preserve refuel status if moving to Q3
    if (to_q != 3) to_queue_arr[to_q - 1][to_slot].status = STATUS_IN_QUEUE; // Always reset when promoting
    
    to_queue_arr[to_q - 1][to_slot].time_in_q3 = 0; // Reset Q3 timer
    to_queue_arr[to_q - 1][to_slot].time_on_runway = 0;
    
    log_scheduler_event(log_file, "[Scheduler]: Jet %d moved from Q%d to Q%d.\n", pid, from_q, to_q);
    return true;
}

static void scheduler_preempt_runway_unsafe(SchedulerState* s, FILE* log_file) {
    if (!s->is_runway_busy) return;
    
    pid_t pid = s->runway_jet_pid;
    log_scheduler_event(log_file, "[Scheduler]: PREEMPTING runway jet %d!\n", pid);
    
    SchedulerJet* jet = scheduler_find_jet_unsafe(s, pid, NULL, NULL);
    if (jet) {
        jet->status = STATUS_IN_QUEUE;
        jet->time_on_runway = 0;
    }
    
    s->is_runway_busy = false;
    s->runway_jet_pid = 0;
    s->runway_jet_q = 0;

    s->total_context_switches++; // Count preemption as a context switch
}


// --- Public Functions ---

void scheduler_init(SchedulerState* s) {
    memset(s->queue1, 0, sizeof(SchedulerJet) * MAX_JETS);
    memset(s->queue2, 0, sizeof(SchedulerJet) * MAX_JETS);
    memset(s->queue3, 0, sizeof(SchedulerJet) * MAX_JETS);

    s->q1_count = 0;
    s->q2_count = 0;
    s->q3_count = 0;
    
    s->is_runway_busy = false;
    s->runway_jet_pid = 0;
    s->runway_jet_q = 0;

    s->q2_rr_quantum = RR_QUANTUM;
    s->is_paused = false;

    // --- NEW: Init stats ---
    s->total_context_switches = 0;
    s->total_runway_busy_time = 0;

    if (pthread_mutex_init(&s->lock, NULL) != 0) {
        perror("Scheduler: Failed to initialize mutex");
        exit(1);
    }
}

void scheduler_destroy(SchedulerState* s) {
    pthread_mutex_destroy(&s->lock);
}

void scheduler_add_jet(SchedulerState* s, pid_t pid, int read_fd, int write_fd, int fuel, FILE* log_file) {
    pthread_mutex_lock(&s->lock);

    int slot = find_empty_slot(s->queue2);
    
    if (slot != -1) {
        s->queue2[slot].pid = pid;
        s->queue2[slot].atc_read_fd = read_fd;
        s->queue2[slot].atc_write_fd = write_fd;
        s->queue2[slot].fuel = fuel;
        s->queue2[slot].status = STATUS_IN_QUEUE;
        s->queue2[slot].time_on_runway = 0;
        s->queue2[slot].time_in_q3 = 0;
        
        // --- NEW: Init stats for jet ---
        s->queue2[slot].arrival_time = time(NULL);
        s->queue2[slot].first_run_time = 0; // 0 indicates not run yet
        s->queue2[slot].total_wait_time = 0;

        s->q2_count++;
        log_scheduler_event(log_file, "[Scheduler]: Jet %d added to Q2. (Fuel: %d)\n", pid, fuel);
    } else {
        log_scheduler_event(log_file, "[Scheduler]: ERROR: Q2 is full. Jet %d rejected.\n", pid);
        close(read_fd);
        close(write_fd);
    }

    pthread_mutex_unlock(&s->lock);
}

// --- MODIFIED: Reverted - 2 arguments, console print is back on
void scheduler_print_queues(SchedulerState* s, FILE* log_file) {
    pthread_mutex_lock(&s->lock);
    
    time_t now = time(0);
    tm *ltm = localtime(&now);
    
    char time_str[20];
    snprintf(time_str, 20, "%02d:%02d:%02d", ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

    // --- Print to Console ---
    cout << "\n========================================================" << endl;
    cout << "           OPERATION SKYWATCH - RADAR DISPLAY" << endl;
    cout << "           Time: " << time_str << (s->is_paused ? " (PAUSED)" : "") << endl;
    cout << "--------------------------------------------------------" << endl;
    
    if (s->is_runway_busy) {
        cout << "RUNWAY STATUS: [BUSY - Jet " << s->runway_jet_pid << " (from Q" << s->runway_jet_q << ")]" << endl;
    } else {
        cout << "RUNWAY STATUS: [IDLE]" << endl;
    }
    cout << "--------------------------------------------------------" << endl;
    
    cout << "Q1 (SRTF - Emergency): [" << s->q1_count << " jets]" << endl;
    if (s->q1_count == 0) cout << "  [Empty]" << endl;
    else {
        for (int i = 0; i < MAX_JETS; i++)
            if (s->queue1[i].pid != 0)
                cout << "  - Jet PID: " << s->queue1[i].pid << " (Fuel: " << s->queue1[i].fuel << ")" << endl;
    }

    cout << "Q2 (RR - Q=" << s->q2_rr_quantum << "):    [" << s->q2_count << " jets]" << endl;
    if (s->q2_count == 0) cout << "  [Empty]" << endl;
    else {
        for (int i = 0; i < MAX_JETS; i++)
            if (s->queue2[i].pid != 0)
                cout << "  - Jet PID: " << s->queue2[i].pid << " (Fuel: " << s->queue2[i].fuel << ")" << endl;
    }
    
    cout << "Q3 (FCFS - Standby):   [" << s->q3_count << " jets]" << endl;
    if (s->q3_count == 0) cout << "  [Empty]" << endl;
    else {
        for (int i = 0; i < MAX_JETS; i++)
            if (s->queue3[i].pid != 0) {
                cout << "  - Jet PID: " << s->queue3[i].pid 
                     << " (Wait: " << s->queue3[i].time_in_q3 << "s"
                     << ", Status: " << s->queue3[i].status << ")" << endl;
            }
    }
    cout << "========================================================" << endl;


    // --- Log to File (a snapshot) ---
    log_scheduler_event(log_file, "[Status]: Q1=%d, Q2=%d, Q3=%d, Runway=%s (Jet %d)\n",
        s->q1_count, s->q2_count, s->q3_count,
        s->is_runway_busy ? "BUSY" : "IDLE",
        s->is_runway_busy ? s->runway_jet_pid : 0);

    pthread_mutex_unlock(&s->lock);
}


void scheduler_tick(SchedulerState* s, FILE* log_file) {
    pthread_mutex_lock(&s->lock);
    
    if (s->is_paused) {
        pthread_mutex_unlock(&s->lock);
        return;
    }

    // --- 1. UPDATE STATS (Wait Time, Runway Time) ---
    if (s->is_runway_busy) {
        s->total_runway_busy_time++;
    }

    SchedulerJet* queues[] = { s->queue1, s->queue2, s->queue3 };
    for (int q = 0; q < 3; q++) {
        for (int i = 0; i < MAX_JETS; i++) {
            SchedulerJet* jet = &queues[q][i];
            // Increment wait time if in any queue and not currently landing/refueling
            if (jet->pid != 0 && (jet->status == STATUS_IN_QUEUE || jet->status == STATUS_WAITING_FUEL)) {
                jet->total_wait_time++; // Increment wait time if in queue
            }
        }
    }


    // --- 2. AGING (Q3 -> Q2) ---
    for (int i = 0; i < MAX_JETS; i++) {
        SchedulerJet* jet = &s->queue3[i];
        
        if (jet->pid != 0 && (jet->status == STATUS_IN_QUEUE || jet->status == STATUS_WAITING_FUEL)) {
            jet->time_in_q3++;
            if (jet->time_in_q3 > AGING_THRESHOLD) {
                log_scheduler_event(log_file, "[Scheduler]: AGING Jet %d from Q3 to Q2.\n", jet->pid);
                JetStatus old_status = jet->status;
                if (scheduler_move_jet_unsafe(s, 3, i, 2, log_file)) {
                    int q_new, idx_new;
                    SchedulerJet* jet_in_q2 = scheduler_find_jet_unsafe(s, jet->pid, &q_new, &idx_new);
                    if (jet_in_q2) {
                        jet_in_q2->status = old_status; 
                    }
                }
            }
        }
    }


    // --- 3. RUNWAY CHECK (RR Demotion) ---
    if (s->is_runway_busy && s->runway_jet_q == 2) {
        SchedulerJet* jet = scheduler_find_jet_unsafe(s, s->runway_jet_pid, NULL, NULL);
        if (jet) {
            jet->time_on_runway++;
            if (jet->time_on_runway >= s->q2_rr_quantum) {
                log_scheduler_event(log_file, "[Scheduler]: RR QUANTUM expired for Jet %d. Demoting to Q3.\n", jet->pid);
                
                s->is_runway_busy = false;
                s->runway_jet_pid = 0;
                s->runway_jet_q = 0;
                s->total_context_switches++; // Count RR demotion as context switch
                
                int q, idx;
                if (scheduler_find_jet_unsafe(s, jet->pid, &q, &idx)) {
                    scheduler_move_jet_unsafe(s, q, idx, 3, log_file);
                }
            }
        }
    }
    
    // --- 4. DISPATCH (if runway is free) ---
    if (s->is_runway_busy) {
        pthread_mutex_unlock(&s->lock);
        return;
    }

    // 4a. Check Queue 1 (SRTF)
    if (s->q1_count > 0) {
        int srtf_jet_idx = -1, min_fuel = 9999;
        for (int i = 0; i < MAX_JETS; i++) {
            if (s->queue1[i].pid != 0 && s->queue1[i].status == STATUS_IN_QUEUE && s->queue1[i].fuel < min_fuel) {
                min_fuel = s->queue1[i].fuel;
                srtf_jet_idx = i;
            }
        }
        
        if (srtf_jet_idx != -1) {
            SchedulerJet* jet = &s->queue1[srtf_jet_idx];
            AtcCommandMessage cmd = { CMD_START_LANDING };
            if (write(jet->atc_write_fd, &cmd, sizeof(cmd)) != -1) {
                s->is_runway_busy = true; s->runway_jet_pid = jet->pid; s->runway_jet_q = 1;
                jet->status = STATUS_LANDING_CMD; 
                if (jet->first_run_time == 0) jet->first_run_time = time(NULL); // Set response time
                s->total_context_switches++; // Count dispatch
                log_scheduler_event(log_file, "[Scheduler]: Runway assigned to EMERGENCY Jet %d (from Q1).\n", jet->pid);
            }
            pthread_mutex_unlock(&s->lock);
            return;
        }
    }
    
    // 4b. Check Queue 2 (RR)
    if (s->q2_count > 0) {
        int jet_idx = -1;
        // First, check for any promoted refuel requests
        for (int i = 0; i < MAX_JETS; i++) {
            if (s->queue2[i].pid != 0 && s->queue2[i].status == STATUS_WAITING_FUEL) {
                jet_idx = i;
                break; 
            }
        }
        // If no refuel requests, find a normal landing request
        if (jet_idx == -1) {
            for (int i = 0; i < MAX_JETS; i++) {
                if (s->queue2[i].pid != 0 && s->queue2[i].status == STATUS_IN_QUEUE) {
                    jet_idx = i;
                    break;
                }
            }
        }

        if (jet_idx != -1) {
            SchedulerJet* jet = &s->queue2[jet_idx];
            AtcCommandMessage cmd;
            if (jet->status == STATUS_WAITING_FUEL) {
                cmd.command = CMD_REFUEL;
                jet->status = STATUS_REFUELING;
                log_scheduler_event(log_file, "[Scheduler]: Runway assigned to Jet %d for REFUELING (from Q2).\n", jet->pid);
            } else {
                cmd.command = CMD_START_LANDING;
                jet->status = STATUS_LANDING_CMD;
                jet->time_on_runway = 0;
                log_scheduler_event(log_file, "[Scheduler]: Runway assigned to Jet %d for LANDING (from Q2).\n", jet->pid);
            }

            if (write(jet->atc_write_fd, &cmd, sizeof(cmd)) != -1) {
                s->is_runway_busy = true; s->runway_jet_pid = jet->pid; s->runway_jet_q = 2;
                if (jet->first_run_time == 0) jet->first_run_time = time(NULL); // Set response time
                s->total_context_switches++; // Count dispatch
            }
            pthread_mutex_unlock(&s->lock);
            return;
        }
    }
    
    // Q3 is standby/aging only. No dispatch from Q3.

    pthread_mutex_unlock(&s->lock);
}

void scheduler_jet_landed_unsafe(SchedulerState* s, pid_t pid, FILE* log_file) {
    log_scheduler_event(log_file, "[Scheduler]: Jet %d has landed and is being cleared.\n", pid);
    
    if (s->runway_jet_pid == pid) {
        s->is_runway_busy = false; s->runway_jet_pid = 0; s->runway_jet_q = 0;
    }
    
    int q, idx;
    // Find the jet to clear its data
    SchedulerJet* jet = scheduler_find_jet_unsafe(s, pid, &q, &idx);
    
    if (jet) {
        close(jet->atc_read_fd); close(jet->atc_write_fd);
        
        // Clear the jet's slot in the queue
        if (q == 1) {
            memset(&s->queue1[idx], 0, sizeof(SchedulerJet)); s->q1_count--;
        } else if (q == 2) {
            memset(&s->queue2[idx], 0, sizeof(SchedulerJet)); s->q2_count--;
        } else if (q == 3) {
            memset(&s->queue3[idx], 0, sizeof(SchedulerJet)); s->q3_count--;
        }
    } else {
        log_scheduler_event(log_file, "[Scheduler]: ERROR: Could not find landed jet %d in queues.\n", pid);
    }
}


void scheduler_handle_emergency_unsafe(SchedulerState* s, pid_t pid, int current_fuel, FILE* log_file) {
    int q, idx;
    SchedulerJet* jet = scheduler_find_jet_unsafe(s, pid, &q, &idx);

    if (!jet) {
        log_scheduler_event(log_file, "[Scheduler]: ERROR: Could not find jet %d to handle emergency.\n", pid);
        return;
    }
    
    jet->fuel = current_fuel;
    jet->status = STATUS_IN_QUEUE; // Ensure it's ready to run

    if (q != 1) {
        log_scheduler_event(log_file, "[Scheduler]: Jet %d moved to Q1 (Emergency).\n", pid);
        if (!scheduler_move_jet_unsafe(s, q, idx, 1, log_file)) return;
        jet = scheduler_find_jet_unsafe(s, pid, &q, &idx); 
        if (!jet) return;
        jet->fuel = current_fuel;
        jet->status = STATUS_IN_QUEUE; // Set status again after move
    }

    if (s->is_runway_busy && s->runway_jet_pid != pid) {
        SchedulerJet* running_jet = scheduler_find_jet_unsafe(s, s->runway_jet_pid, NULL, NULL);
        if (!running_jet) return;
        
        bool preempt = false;
        if (s->runway_jet_q == 1) {
            if (jet->fuel < running_jet->fuel) {
                preempt = true;
                log_scheduler_event(log_file, "[Scheduler]: New emergency Jet %d (fuel %d) preempting running Jet %d (fuel %d).\n",
                     jet->pid, jet->fuel, running_jet->pid, running_jet->fuel);
            }
        } else {
            preempt = true;
             log_scheduler_event(log_file, "[Scheduler]: Emergency Jet %d preempting non-emergency Jet %d.\n",
                  jet->pid, running_jet->pid);
        }

        if (preempt) {
            scheduler_preempt_runway_unsafe(s, log_file);
        }
    }
}

// --- MODIFIED: Handle refuel request ---
void scheduler_handle_refuel_request_unsafe(SchedulerState* s, pid_t pid, int current_fuel, FILE* log_file) {
    int q, idx;
    SchedulerJet* jet = scheduler_find_jet_unsafe(s, pid, &q, &idx);
    if (!jet) {
        log_scheduler_event(log_file, "[Scheduler]: ERROR: Could not find jet %d to handle refuel request.\n", pid);
        return;
    }
    
    jet->fuel = current_fuel;
    jet->status = STATUS_WAITING_FUEL;

    if (q != 3) {
        log_scheduler_event(log_file, "[Scheduler]: Jet %d moved to Q3 for refueling.\n", pid);
        scheduler_move_jet_unsafe(s, q, idx, 3, log_file);
    } else {
        log_scheduler_event(log_file, "[Scheduler]: Jet %d is waiting in Q3 to refuel.\n", pid);
    }
}

