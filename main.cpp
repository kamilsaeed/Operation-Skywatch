#include "utils.h"
#include "scheduler.h" 
#include <stdio.h>
#include <string.h>
#include <stdarg.h> 
#include <vector>     // --- NEW: For stats
#include <time.h>     // --- NEW: For stats
#include <fcntl.h>    // --- NEW: For console loop
#include <errno.h>    // --- NEW: For console loop

// --- Student Information ---
const char* STUDENT_NAME = "Student Name";
const char* STUDENT_ROLLNO = "23i-2035";
const char* ROLLNO_LAST_TWO = "35"; 

// --- Global State (Unchanged) ---
int generator_pipe_write_end;
int console_pipe[2]; 
bool keep_running = true;
int active_jet_count = 0;
static int jet_counter = 0; 
SchedulerState scheduler;
FILE* log_file = NULL;

// --- NEW: Global state for statistics ---
time_t simulation_start_time;
struct JetStats {
    pid_t pid;
    double turnaround_time;
    double waiting_time;
    double response_time;
};
std::vector<JetStats> completed_jet_stats;
pthread_mutex_t stats_lock; // To protect the stats vector


/**
 * @brief MODIFIED: Reverted - prints to console AND log file
 */
void log_event(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args); // Print to console
    va_end(args);
    
    if (log_file) {
        time_t now = time(0);
        tm *ltm = localtime(&now);
        char time_buf[20];
        snprintf(time_buf, 20, "[%02d:%02d:%02d] ", ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
        fprintf(log_file, "%s", time_buf);
        va_start(args, format);
        vfprintf(log_file, format, args); // Print to log file
        va_end(args);
        fflush(log_file);
    }
}


/**
 * @brief MODIFIED: Creates 8 jets
 * MODIFIED: Removed all cout statements
 */
void run_jet_generator() {
    // --- REMOVED cout ---
    
    // --- MODIFIED: Create a "traffic jam" to test all queues ---
    
    // Total 8 jets, 1 per second
    int fuel_levels[] = {
        60, // Standard jet
        20, // EMERGENCY jet (will hit 10 fuel while waiting)
        60, // Standard jet
        40, // REFUEL jet (will hit 25 fuel while waiting)
        60, // Standard jet (will be demoted by RR)
        60, // Standard jet (will be demoted by RR)
        18, // EMERGENCY jet
        50  // REFUEL jet
    };
    
    for (int i = 0; i < 8; i++) {
        sleep(1); // Spawn a jet every second
        int initial_fuel = fuel_levels[i];

        if (initial_fuel <= 20) { 
            // --- REMOVED cout ---
        } 
        else if (initial_fuel <= 40) { 
            // --- REMOVED cout ---
        }

        JetMessage new_jet_request = { initial_fuel };
        // --- REMOVED cout ---
             
        if (write(generator_pipe_write_end, &new_jet_request, sizeof(JetMessage)) == -1) {
            perror("Generator: Pipe write error");
        }
    }

    // --- REMOVED cout ---
    close(generator_pipe_write_end);
}

/**
 * @brief MODIFIED: Reverted - calls print_queues normally
 */
void* display_loop(void* arg) {
    log_event("[ATC Display Thread]: Display started.\n");
    SchedulerState* s = (SchedulerState*)arg;
    while (keep_running) {
        // --- MODIFIED: `scheduler_print_queues` now prints to console by default ---
        scheduler_print_queues(s, log_file);
        sleep(2);
    }
    log_event("[ATC Display Thread]: Display shutting down.\n");
    return NULL;
}

// ... (scheduler_loop is unchanged) ...
void* scheduler_loop(void* arg) {
    log_event("[Scheduler Thread]: Clock started.\n");
    SchedulerState* s = (SchedulerState*)arg;
    while (keep_running) {
        sleep(1); // 1-second tick
        scheduler_tick(s, log_file);
    }
    log_event("[Scheduler Thread]: Clock shutting down.\n");
    return NULL;
}

/**
 * @brief MODIFIED: Interactive console loop
 * Uses select() with a timeout to remain non-blocking
 * and prints a prompt "ATC-CMD>" for a user-friendly experience.
 */
void* console_loop(void* arg) {
    // --- MODIFIED: Print initial messages to console directly ---
    printf("[Console Thread]: Ready for commands.\n");
    printf("Commands: status, new_jet <fuel>, force_emergency <pid>, boost_priority <pid>, change_quantum <val>, pause_sim, resume_sim, exit\n");
    log_event("[Console Thread]: Ready for commands.\n");
    SchedulerState* s = (SchedulerState*)arg;
    
    cout << "\nATC-CMD> "; // Print initial prompt
    fflush(stdout);

    while (keep_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        
        // 1 second timeout
        struct timeval timeout = { 1, 0 }; 
        int activity = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno == EINTR) continue; // Interrupted by signal, just continue
            perror("Console select");
            break;
        }

        // If activity > 0, STDIN has input
        if (activity > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
            char buffer[256];
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                // Ctrl+D pressed or error
                printf("\n[Console]: STDIN closed. Shutting down.\n");
                log_event("[Console]: STDIN closed. Shutting down.\n");
                keep_running = false;
                break;
            }
            
            buffer[strcspn(buffer, "\n")] = 0; // Remove newline
            
            int arg1;
            
            if (sscanf(buffer, "new_jet %d", &arg1) == 1) {
                if (arg1 > 0) {
                    printf("[Console]: Requesting new jet with %d fuel.\n", arg1);
                    log_event("[Console]: Requesting new jet with %d fuel.\n", arg1);
                    JetMessage new_jet_request = { arg1 };
                    if (write(console_pipe[1], &new_jet_request, sizeof(JetMessage)) == -1) {
                        printf("[Console]: ERROR: Failed to send new_jet request to main thread.\n");
                        log_event("[Console]: ERROR: Failed to send new_jet request to main thread.\n");
                    }
                } else {
                    printf("[Console]: Fuel must be > 0.\n");
                    log_event("[Console]: Fuel must be > 0.\n");
                }
            }
            else if (sscanf(buffer, "force_emergency %d", &arg1) == 1) {
                printf("[Console]: Executing 'force_emergency %d'\n", arg1);
                log_event("[Console]: Executing 'force_emergency %d'\n", arg1);
                pthread_mutex_lock(&s->lock);
                scheduler_handle_emergency_unsafe(s, (pid_t)arg1, 1, log_file);
                pthread_mutex_unlock(&s->lock);
            
            } else if (sscanf(buffer, "boost_priority %d", &arg1) == 1) {
                printf("[Console]: Executing 'boost_priority %d'\n", arg1);
                log_event("[Console]: Executing 'boost_priority %d'\n", arg1);
                pthread_mutex_lock(&s->lock);
                int q, idx;
                SchedulerJet* jet = scheduler_find_jet_unsafe(s, (pid_t)arg1, &q, &idx);
                if (jet) {
                    if (q == 3) {
                        printf("[Console]: Jet %d boosted from Q3 to Q2.\n", arg1);
                        scheduler_move_jet_unsafe(s, 3, idx, 2, log_file);
                    } else if (q == 2) {
                        printf("[Console]: Jet %d boosted from Q2 to Q1.\n", arg1);
                        scheduler_move_jet_unsafe(s, 2, idx, 1, log_file);
                    } else {
                        printf("[Console]: Jet %d already in Q1.\n", arg1);
                    }
                } else {
                    printf("[Console]: Jet %d not found.\n", arg1);
                }
                pthread_mutex_unlock(&s->lock);
            
            } else if (sscanf(buffer, "change_quantum %d", &arg1) == 1) {
                if (arg1 > 0) {
                    printf("[Console]: Executing 'change_quantum %d'\n", arg1);
                    log_event("[Console]: Executing 'change_quantum %d'\n", arg1);
                    pthread_mutex_lock(&s->lock);
                    s->q2_rr_quantum = arg1;
                    pthread_mutex_unlock(&s->lock);
                } else {
                    printf("[Console]: Quantum must be > 0.\n");
                    log_event("[Console]: Quantum must be > 0.\n");
                }

            } else if (strcmp(buffer, "pause_sim") == 0) {
                printf("[Console]: Executing 'pause_sim'\n");
                log_event("[Console]: Executing 'pause_sim'\n");
                pthread_mutex_lock(&s->lock);
                s->is_paused = true;
                pthread_mutex_unlock(&s->lock);

            } else if (strcmp(buffer, "resume_sim") == 0) {
                printf("[Console]: Executing 'resume_sim'\n");
                log_event("[Console]: Executing 'resume_sim'\n");
                pthread_mutex_lock(&s->lock);
                s->is_paused = false;
                pthread_mutex_unlock(&s->lock);
            
            } else if (strcmp(buffer, "status") == 0) {
                printf("[Console]: Forcing display refresh.\n");
                log_event("[Console]: Forcing display refresh.\n");
                // --- MODIFIED: `scheduler_print_queues` prints to console ---
                scheduler_print_queues(s, log_file); 
            
            } else if (strcmp(buffer, "exit") == 0) {
                printf("[Console]: Exit command received. Shutting down.\n");
                log_event("[Console]: Exit command received. Shutting down.\n");
                keep_running = false;
                
            } else if (strlen(buffer) > 0) {
                printf("[Console]: Unknown command '%s'\n", buffer);
                log_event("[Console]: Unknown command '%s'\n", buffer);
            }

            // Print prompt for next command
            if (keep_running) {
                cout << "ATC-CMD> "; 
                fflush(stdout);
            }
        }
        // If select times out (activity == 0), loop repeats
    }
    log_event("[Console Thread]: Shutting down.\n");
    return NULL;
}


/**
 * @brief NEW: Prints the final statistics summary
 * --- MODIFIED: Now also prints to console
 */
void print_final_summary()
{
    time_t simulation_end_time = time(NULL);
    double total_simulation_time = difftime(simulation_end_time, simulation_start_time);
    if (total_simulation_time < 1) total_simulation_time = 1; // Avoid division by zero

    char buffer[2048]; // Buffer to hold the summary string
    char* buf_ptr = buffer;
    int len = 0;

    len += snprintf(buf_ptr + len, sizeof(buffer) - len, "\n\n========================================================\n");
    len += snprintf(buf_ptr + len, sizeof(buffer) - len, "           FINAL SIMULATION SUMMARY\n");
    len += snprintf(buf_ptr + len, sizeof(buffer) - len, "========================================================\n\n");
    
    len += snprintf(buf_ptr + len, sizeof(buffer) - len, "Total Simulation Time: %.0f seconds\n", total_simulation_time);
    
    double avg_turnaround = 0, avg_wait = 0, avg_response = 0;
    
    pthread_mutex_lock(&stats_lock);
    int jet_count = completed_jet_stats.size();
    
    if (jet_count > 0) {
        len += snprintf(buf_ptr + len, sizeof(buffer) - len, "\n--- Individual Jet Stats ---\n");
        for (const auto& stats : completed_jet_stats) {
            len += snprintf(buf_ptr + len, sizeof(buffer) - len, "  - Jet %d: Turnaround=%.0fs, Wait=%.0fs, Response=%.0fs\n", 
                (int)stats.pid, stats.turnaround_time, stats.waiting_time, stats.response_time);
            avg_turnaround += stats.turnaround_time;
            avg_wait += stats.waiting_time;
            avg_response += stats.response_time;
        }
        
        avg_turnaround /= jet_count;
        avg_wait /= jet_count;
        avg_response /= jet_count;

        len += snprintf(buf_ptr + len, sizeof(buffer) - len, "\n--- Average Stats ---\n");
        len += snprintf(buf_ptr + len, sizeof(buffer) - len, "Average Turnaround Time: %.2f s\n", avg_turnaround);
        len += snprintf(buf_ptr + len, sizeof(buffer) - len, "Average Waiting Time:    %.2f s\n", avg_wait);
        len += snprintf(buf_ptr + len, sizeof(buffer) - len, "Average Response Time:   %.2f s\n", avg_response);

    } else {
        len += snprintf(buf_ptr + len, sizeof(buffer) - len, "\nNo jets completed simulation.\n");
    }
    pthread_mutex_unlock(&stats_lock);

    pthread_mutex_lock(&scheduler.lock);
    int context_switches = scheduler.total_context_switches;
    double runway_busy_time = scheduler.total_runway_busy_time;
    pthread_mutex_unlock(&scheduler.lock);

    double cpu_utilization = (runway_busy_time / total_simulation_time) * 100.0;

    len += snprintf(buf_ptr + len, sizeof(buffer) - len, "\n--- System Stats ---\n");
    len += snprintf(buf_ptr + len, sizeof(buffer) - len, "Total Context Switches:  %d\n", context_switches);
    len += snprintf(buf_ptr + len, sizeof(buffer) - len, "Runway Utilization (CPU): %.2f %% (%.0f / %.0f s)\n", 
        cpu_utilization, runway_busy_time, total_simulation_time);
    len += snprintf(buf_ptr + len, sizeof(buffer) - len, "\n========================================================\n");

    // --- NEW: Print the entire buffer to console and log file ---
    printf("%s", buffer);
    if (log_file) {
        fprintf(log_file, "%s", buffer);
        fflush(log_file);
    }
}


// --- Main function for the ATC Tower ---
int main() {
    
    // ... (Step 1: Init, Get Seed, Open Log is unchanged) ...
    cout << "======================================" << endl;
    cout << "    OPERATION SKYWATCH ATC SIMULATOR" << endl;
    cout << "    Name: " << STUDENT_NAME << endl;
    cout << "    Roll No: " << STUDENT_ROLLNO << endl;
    cout << "======================================" << endl;
    int roll_no_seed;
    cout << "Enter your 4-digit roll number (e.g., 2035) to seed simulation: ";
    cin >> roll_no_seed;
    cin.ignore(1000, '\n'); 
    srand(roll_no_seed);
    
    char log_filename[100];
    snprintf(log_filename, 100, "%s_skywatch_log.txt", STUDENT_ROLLNO);
    log_file = fopen(log_filename, "w"); // Use "w" to overwrite old logs
    if (log_file == NULL) {
        perror("Failed to open log file"); return 1;
    }
    
    // --- MODIFIED: Reverted - use log_event to print to console ---
    log_event("\n--- Simulation Started by %s (%s) ---\n", STUDENT_NAME, STUDENT_ROLLNO);
    log_event("Seed set to %d.\n", roll_no_seed);
    
    scheduler_init(&scheduler);
    pthread_mutex_init(&stats_lock, NULL); // --- NEW: Init stats lock
    simulation_start_time = time(NULL);    // --- NEW: Record start time
    
    // ... (Seed explanation comment) ...
    // Using the roll number as a seed (srand) ensures that
    // the sequence of random numbers (rand) is unique to me.
    // This makes my simulation's output (jet arrival times, fuel)
    // different from other students' but repeatable for debugging.

    
    // ... (Step 2: Create Pipes is unchanged) ...
    int generator_pipe[2]; 
    if (pipe(generator_pipe) == -1) {
        log_event("FATAL: Failed to create generator pipe.\n");
        return 1;
    }
    generator_pipe_write_end = generator_pipe[1];
    if (pipe(console_pipe) == -1) {
        log_event("FATAL: Failed to create console pipe.\n");
        return 1;
    }
    bool generator_is_done = false;

    
    // ... (Step 3: Fork Generator is unchanged) ...
    pid_t generator_pid = fork();
    if (generator_pid < 0) {
        log_event("FATAL: Failed to fork Jet Generator.\n");
        return 1;
    } else if (generator_pid == 0) {
        if (log_file) fclose(log_file);
        close(generator_pipe[0]);
        close(console_pipe[0]); 
        close(console_pipe[1]);
        run_jet_generator();
        exit(0); 
    } 
    log_event("[ATC Tower %d]: Jet Generator process started (PID: %d).\n", getpid(), generator_pid);
    close(generator_pipe[1]); 
    
    // ... (Step 4: Create Threads is unchanged) ...
    pthread_t display_thread_id, scheduler_thread_id, console_thread_id;
    if (pthread_create(&display_thread_id, NULL, display_loop, &scheduler) != 0) {
        log_event("FATAL: Failed to create ATC Display thread.\n"); return 1;
    }
    if (pthread_create(&scheduler_thread_id, NULL, scheduler_loop, &scheduler) != 0) {
        log_event("FATAL: Failed to create Scheduler Clock thread.\n"); return 1;
    }
    if (pthread_create(&console_thread_id, NULL, console_loop, &scheduler) != 0) {
        log_event("FATAL: Failed to create Console thread.\n"); return 1;
    }
    close(console_pipe[1]);


    
    // --- Step 5: Main I/O Loop (Unchanged) ---
    log_event("[ATC Tower]: Main I/O loop started.\n");
    
    while (keep_running) 
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        int max_fd = 0;
        
        if (!generator_is_done) {
            FD_SET(generator_pipe[0], &read_fds);
            max_fd = generator_pipe[0];
        }
        
        FD_SET(console_pipe[0], &read_fds);
        if (console_pipe[0] > max_fd) max_fd = console_pipe[0];
        
        pthread_mutex_lock(&scheduler.lock);
        for (int q = 0; q < 3; q++) {
            SchedulerJet* queue = (q == 0) ? scheduler.queue1 : (q == 1) ? scheduler.queue2 : scheduler.queue3;
            for (int i = 0; i < MAX_JETS; i++) {
                if (queue[i].pid != 0) {
                    int fd = queue[i].atc_read_fd;
                    FD_SET(fd, &read_fds);
                    if (fd > max_fd) max_fd = fd;
                }
            }
        }
        pthread_mutex_unlock(&scheduler.lock);

        
        struct timeval timeout = { 0, 100000 }; 
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno == EINTR) continue;
            log_event("ERROR: select() error.\n");
            continue;
        }
        
        // Helper lambda (unchanged)
        auto create_new_jet = [&](int initial_fuel) {
            log_event("[ATC Tower]: Creating new jet with %d fuel.\n", initial_fuel);
            int atc_to_jet_pipe[2], jet_to_atc_pipe[2];
            // --- FIX 1: Typo jet_to_ata_pipe -> jet_to_atc_pipe ---
            if (pipe(atc_to_jet_pipe) == -1 || pipe(jet_to_atc_pipe) == -1) {
                log_event("ERROR: Failed to create jet pipes.\n");
                return;
            }
            
            pid_t jet_pid = fork();
            if (jet_pid < 0) {
                log_event("ERROR: Failed to fork jet process.\n");
                return;
            }
            
            if (jet_pid == 0) {
                if (log_file) fclose(log_file);
                close(generator_pipe[0]);
                close(console_pipe[0]);
                close(atc_to_jet_pipe[1]);
                close(jet_to_atc_pipe[0]);
                
                char read_fd_str[10], write_fd_str[10], fuel_str[10], jet_id_str[20];
                snprintf(read_fd_str, 10, "%d", atc_to_jet_pipe[0]);
                snprintf(write_fd_str, 10, "%d", jet_to_atc_pipe[1]);
                snprintf(fuel_str, 10, "%d", initial_fuel);
                snprintf(jet_id_str, 20, "%s-%02d", ROLLNO_LAST_TWO, jet_counter);
                
                execlp("./drone", "drone", read_fd_str, write_fd_str, fuel_str, jet_id_str, (char*)NULL);
                perror("ATC: execlp failed");
                exit(1);
            }
            
            close(atc_to_jet_pipe[0]);
            close(jet_to_atc_pipe[1]);
            log_event("[ATC Tower]: Forked new jet (PID %d)\n", jet_pid);
            scheduler_add_jet(&scheduler, jet_pid, jet_to_atc_pipe[0], 
                              atc_to_jet_pipe[1], initial_fuel, log_file);
            active_jet_count++;
        };
        
        // Check generator (unchanged)
        if (!generator_is_done && FD_ISSET(generator_pipe[0], &read_fds)) {
            JetMessage received_jet_request;
            ssize_t bytes_read = read(generator_pipe[0], &received_jet_request, sizeof(JetMessage));
            if (bytes_read > 0) {
                jet_counter++; 
                create_new_jet(received_jet_request.initial_fuel);
            } else if (bytes_read == 0) {
                log_event("[ATC Tower]: Jet Generator has shut down.\n");
                close(generator_pipe[0]);
                generator_is_done = true;
            }
        }
        
        // Check console pipe (unchanged)
        if (FD_ISSET(console_pipe[0], &read_fds)) {
            JetMessage received_jet_request;
            ssize_t bytes_read = read(console_pipe[0], &received_jet_request, sizeof(JetMessage));
            if (bytes_read > 0) {
                jet_counter++; 
                // --- FIX 2: Typo initial_ael -> initial_fuel ---
                create_new_jet(received_jet_request.initial_fuel);
            }
        }
        
        // Check jet feedback
        pthread_mutex_lock(&scheduler.lock);
        for (int q = 0; q < 3; q++) {
            SchedulerJet* queue = (q == 0) ? scheduler.queue1 : (q == 1) ? scheduler.queue2 : scheduler.queue3;
            for (int i = 0; i < MAX_JETS; i++) {
                SchedulerJet* jet = &queue[i];
                if (jet->pid != 0 && FD_ISSET(jet->atc_read_fd, &read_fds)) {
                    JetFeedbackMessage feedback;
                    ssize_t bytes = read(jet->atc_read_fd, &feedback, sizeof(JetFeedbackMessage));
                    
                    if (bytes > 0) {
                        if (feedback.status == STATUS_LANDED) {
                            pid_t landed_pid = jet->pid;

                            // --- NEW: Capture stats BEFORE clearing jet data ---
                            SchedulerJet* landed_jet = scheduler_find_jet_unsafe(&scheduler, landed_pid, NULL, NULL);
                            if (landed_jet) {
                                time_t completion_time = time(NULL);
                                JetStats stats;
                                stats.pid = landed_pid;
                                stats.turnaround_time = difftime(completion_time, landed_jet->arrival_time);
                                stats.waiting_time = landed_jet->total_wait_time;
                                
                                if (landed_jet->first_run_time != 0) {
                                    stats.response_time = difftime(landed_jet->first_run_time, landed_jet->arrival_time);
                                } else {
                                    // Should not happen if it landed, but as a fallback:
                                    stats.response_time = stats.turnaround_time;
                                }
                                
                                pthread_mutex_lock(&stats_lock);
                                completed_jet_stats.push_back(stats);
                                pthread_mutex_unlock(&stats_lock);
                            }
                            // --- End of stats capture ---

                            scheduler_jet_landed_unsafe(&scheduler, landed_pid, log_file); 
                            waitpid(landed_pid, NULL, 0); 
                            active_jet_count--;
                            log_event("[ATC Tower]: Cleaned up jet %d. %d jets remaining.\n", landed_pid, active_jet_count);
                        } 
                        else if (feedback.status == STATUS_EMERGENCY) {
                            log_event("[ATC Tower]: EMERGENCY from Jet %d! (Fuel: %d)\n", jet->pid, feedback.data);
                            scheduler_handle_emergency_unsafe(&scheduler, jet->pid, feedback.data, log_file);
                        } 
                        else if (feedback.status == STATUS_FUEL_LOW) {
                            log_event("[ATC Tower]: Low fuel warning from Jet %d (Fuel: %d).\n", jet->pid, feedback.data);
                            jet->fuel = feedback.data; 
                        }
                        else if (feedback.status == STATUS_WAITING_FUEL) {
                            log_event("[ATC Tower]: Refuel request from Jet %d (Fuel: %d).\n", jet->pid, feedback.data);
                            scheduler_handle_refuel_request_unsafe(&scheduler, jet->pid, feedback.data, log_file);
                        }
                        else if (feedback.status == STATUS_REFUELED) {
                            log_event("[ATC Tower]: Jet %d finished refueling (New Fuel: %d).\n", jet->pid, feedback.data);
                            jet->fuel = feedback.data;
                            jet->status = STATUS_IN_QUEUE; 
                            if (scheduler.runway_jet_pid == jet->pid) {
                                scheduler.is_runway_busy = false;
                                scheduler.runway_jet_pid = 0;
                                scheduler.runway_jet_q = 0;
                            }
                        }
                        
                    } else if (bytes == 0) {
                        pid_t crashed_pid = jet->pid;
                        log_event("[ATC Tower]: Jet %d pipe closed unexpectedly.\n", crashed_pid);
                        scheduler_jet_landed_unsafe(&scheduler, crashed_pid, log_file); 
                        waitpid(crashed_pid, NULL, 0);
                        active_jet_count--;
                    }
                }
            }
        }
        pthread_mutex_unlock(&scheduler.lock);
        
        
        // ... (Shutdown check is unchanged) ...
        if (generator_is_done && active_jet_count == 0) {
            log_event("[ATC Tower]: All jets have landed. Shutting down.\n");
            keep_running = false;
        }
    } 
    
    // ... (Step 6: Cleanup is unchanged) ...
    log_event("[ATC Tower]: Cleaning up resources.\n");
    keep_running = false; 
    
    pthread_join(display_thread_id, NULL);
    pthread_join(scheduler_thread_id, NULL);
    
    // Send a newline to console thread to unblock fgets
    write(STDIN_FILENO, "\n", 1); 
    pthread_cancel(console_thread_id);
    pthread_join(console_thread_id, NULL);
    
    waitpid(generator_pid, NULL, 0); 
    
    if (!generator_is_done) close(generator_pipe[0]);
    close(console_pipe[0]); 
    
    scheduler_destroy(&scheduler);
    pthread_mutex_destroy(&stats_lock); // --- NEW: Destroy stats lock

    // --- NEW: Print final summary before closing log ---
    print_final_summary();

    if (log_file) fclose(log_file);

    cout << "[ATC Tower]: Simulation finished. Log file created. Exiting." << endl;
    return 0;
}

