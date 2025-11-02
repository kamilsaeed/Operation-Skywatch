#include "utils.h"
#include "scheduler.h" 
#include <stdio.h>
#include <string.h>
#include <stdarg.h> 

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

// ... (log_event function is unchanged) ...
void log_event(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    if (log_file) {
        time_t now = time(0);
        tm *ltm = localtime(&now);
        char time_buf[20];
        snprintf(time_buf, 20, "[%02d:%02d:%02d] ", ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
        fprintf(log_file, "%s", time_buf);
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        fflush(log_file);
    }
}


// ... (run_jet_generator function is unchanged) ...
void run_jet_generator() {
    cout << "[Jet Generator " << getpid() << "]: Starting..." << endl;
    
    for (int i = 0; i < 5; i++) {
        int wait_time = (rand() % 2) + 1; 
        sleep(wait_time);

        int initial_fuel;
        
        if (i == 2) { 
            initial_fuel = 15; 
            cout << "[Jet Generator " << getpid() << "]: --- FORCING EMERGENCY JET ---" << endl;
        } 
        else if (i == 3) { 
            initial_fuel = 35; 
            cout << "[Jet Generator " << getpid() << "]: --- FORCING REFUEL JET ---" << endl;
        } else {
            initial_fuel = 60; 
        }

        JetMessage new_jet_request = { initial_fuel };
        cout << "[Jet Generator " << getpid() << "]: Sending request for new jet with fuel " << initial_fuel << endl;
             
        if (write(generator_pipe_write_end, &new_jet_request, sizeof(JetMessage)) == -1) {
            perror("Generator: Pipe write error");
        }
    }

    cout << "[Jet Generator " << getpid() << "]: Finished creating jets. Exiting." << endl;
    close(generator_pipe_write_end);
}

// ... (display_loop is unchanged) ...
void* display_loop(void* arg) {
    log_event("[ATC Display Thread]: Display started.\n");
    SchedulerState* s = (SchedulerState*)arg;
    while (keep_running) {
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
        sleep(1);
        scheduler_tick(s, log_file);
    }
    log_event("[Scheduler Thread]: Clock shutting down.\n");
    return NULL;
}

// ... (console_loop is unchanged) ...
void* console_loop(void* arg) {
    log_event("[Console Thread]: Ready for commands.\n");
    cout << "Commands: status, new_jet <fuel>, force_emergency <pid>, boost_priority <pid>, change_quantum <val>, pause_sim, resume_sim, exit" << endl;
    SchedulerState* s = (SchedulerState*)arg;
    
    while (keep_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        
        struct timeval timeout = { 1, 0 };
        int activity = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
            char buffer[256];
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) continue;
            buffer[strcspn(buffer, "\n")] = 0;
            
            int arg1;
            
            if (sscanf(buffer, "new_jet %d", &arg1) == 1) {
                if (arg1 > 0) {
                    log_event("[Console]: Requesting new jet with %d fuel.\n", arg1);
                    JetMessage new_jet_request = { arg1 };
                    if (write(console_pipe[1], &new_jet_request, sizeof(JetMessage)) == -1) {
                        log_event("[Console]: ERROR: Failed to send new_jet request to main thread.\n");
                    }
                } else {
                    log_event("[Console]: Fuel must be > 0.\n");
                }
            }
            else if (sscanf(buffer, "force_emergency %d", &arg1) == 1) {
                log_event("[Console]: Executing 'force_emergency %d'\n", arg1);
                pthread_mutex_lock(&s->lock);
                scheduler_handle_emergency_unsafe(s, (pid_t)arg1, 1, log_file);
                pthread_mutex_unlock(&s->lock);
            
            } else if (sscanf(buffer, "boost_priority %d", &arg1) == 1) {
                log_event("[Console]: Executing 'boost_priority %d'\n", arg1);
                pthread_mutex_lock(&s->lock);
                int q, idx;
                SchedulerJet* jet = scheduler_find_jet_unsafe(s, (pid_t)arg1, &q, &idx);
                if (jet) {
                    if (q == 3) scheduler_move_jet_unsafe(s, 3, idx, 2, log_file);
                    else if (q == 2) scheduler_move_jet_unsafe(s, 2, idx, 1, log_file);
                    else log_event("[Console]: Jet %d already in Q1.\n", arg1);
                } else {
                    log_event("[Console]: Jet %d not found.\n", arg1);
                }
                pthread_mutex_unlock(&s->lock);
            
            } else if (sscanf(buffer, "change_quantum %d", &arg1) == 1) {
                if (arg1 > 0) {
                    log_event("[Console]: Executing 'change_quantum %d'\n", arg1);
                    pthread_mutex_lock(&s->lock);
                    s->q2_rr_quantum = arg1;
                    pthread_mutex_unlock(&s->lock);
                } else {
                    log_event("[Console]: Quantum must be > 0.\n");
                }

            } else if (strcmp(buffer, "pause_sim") == 0) {
                log_event("[Console]: Executing 'pause_sim'\n");
                pthread_mutex_lock(&s->lock);
                s->is_paused = true;
                pthread_mutex_unlock(&s->lock);

            } else if (strcmp(buffer, "resume_sim") == 0) {
                log_event("[Console]: Executing 'resume_sim'\n");
                pthread_mutex_lock(&s->lock);
                s->is_paused = false;
                pthread_mutex_unlock(&s->lock);
            
            } else if (strcmp(buffer, "status") == 0) {
                log_event("[Console]: Forcing display refresh.\n");
                scheduler_print_queues(s, log_file);
            
            } else if (strcmp(buffer, "exit") == 0) {
                log_event("[Console]: Exit command received. Shutting down.\n");
                keep_running = false;
                
            } else if (strlen(buffer) > 0) {
                log_event("[Console]: Unknown command '%s'\n", buffer);
            }
        }
    }
    log_event("[Console Thread]: Shutting down.\n");
    return NULL;
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
    
    // --- THIS IS THE FIX ---
    char log_filename[100];
    snprintf(log_filename, 100, "%s_skywatch_log.txt", STUDENT_ROLLNO);
    log_file = fopen(log_filename, "a"); // "w" (write) changed to "a" (append)
    if (log_file == NULL) {
        perror("Failed to open log file"); return 1;
    }
    // --- END FIX ---
    
    log_event("\n--- Simulation Started by %s (%s) ---\n", STUDENT_NAME, STUDENT_ROLLNO);
    log_event("Seed set to %d.\n", roll_no_seed);
    
    scheduler_init(&scheduler);
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
                              atc_to_jet_pipe[1], initial_fuel);
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
                create_new_jet(received_jet_request.initial_fuel);
            }
        }
        
        // Check jet feedback (unchanged)
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
    pthread_cancel(console_thread_id);
    pthread_join(console_thread_id, NULL);
    
    waitpid(generator_pid, NULL, 0); 
    
    if (!generator_is_done) close(generator_pipe[0]);
    close(console_pipe[0]); 
    
    scheduler_destroy(&scheduler);
    if (log_file) fclose(log_file);

    cout << "[ATC Tower]: Simulation finished. Log file created. Exiting." << endl;
    return 0;
}