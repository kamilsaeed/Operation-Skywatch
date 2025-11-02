#include "utils.h"

// --- Student Information ---
// !! PLEASE FILL THESE OUT !!
const char* STUDENT_NAME = "Your Name";
const char* STUDENT_ROLLNO = "22i-xxxx";

// This is the write-end of the generator's pipe
// We make it global so the child process can find it,
// but we will close it in the parent.
int generator_pipe_write_end;

/**
 * @brief Data structure for the ATC Tower to track each active jet.
 */
struct ActiveJet {
    pid_t pid;
    int read_fd;  // ATC reads from this jet's pipe
    int write_fd; // ATC writes to this jet's pipe
    int fuel;
    bool is_active;
};

// We use a fixed-size array to store active jets (as requested)
const int MAX_JETS = 20;
ActiveJet active_jet_list[MAX_JETS];

/**
 * @brief The main function for the Jet Generator process.
 * * This function will run in a separate child process.
 * Its only job is to create new jets at random intervals
 * and report them to the ATC Tower via a pipe.
 */
void run_jet_generator() {
    // This process is ONLY the generator.
    // It loops a few times to create sample jets, then exits.
    cout << "[Jet Generator " << getpid() << "]: Starting..." << endl;
    
    for (int i = 0; i < 5; i++) {
        // Wait for a random time (1 to 3 seconds)
        int wait_time = (rand() % 3) + 1;
        sleep(wait_time);

        // --- Generator's ONLY job: ---
        // 1. Decide on a random fuel level
        int initial_fuel = (rand() % 90) + 10; // Random fuel (10-99)

        // 2. Report this "new jet request" to the ATC Tower
        JetMessage new_jet_request;
        new_jet_request.initial_fuel = initial_fuel;

        cout << "[Jet Generator " << getpid() << "]: Sending request for new jet with fuel " 
             << initial_fuel << endl;
             
        if (write(generator_pipe_write_end, &new_jet_request, sizeof(JetMessage)) == -1) {
            perror("Generator: Pipe write error");
        }
    }

    cout << "[Jet Generator " << getpid() << "]: Finished creating jets. Exiting." << endl;
    
    // Close the write-end of the pipe before exiting
    close(generator_pipe_write_end);
}

/**
 * @brief The main loop for the ATC Display thread.
 * * This thread runs inside the ATC Tower.
 * Its job is to continuously print the status
 * of the queues. For now, it's just a placeholder.
 */
void* display_loop(void* arg) {
    cout << "[ATC Display Thread]: Display started." << endl;
    
    // In later phases, this loop will read the queue states
    // (using a mutex) and print the "radar" display.
    // For now, it just loops and sleeps.
    for (int i = 0; i < 10; i++) {
        sleep(2);
        // We can add a simple status print here later
    }
    
    cout << "[ATC Display Thread]: Display shutting down." << endl;
    return NULL;
}


/**
 * @brief Helper function to add a new jet to our list.
 */
void add_jet_to_list(JetMessage& msg) {
    // This is a simple implementation. In Phase 3, we'll
    // add this jet directly to Queue 2.
}


/**
 * @brief Main function for the ATC Tower
 */
int main() {
    
    // --- Step 1: Print Header and Get Seed ---
    cout << "======================================" << endl;
    cout << "    OPERATION SKYWATCH ATC SIMULATOR" << endl;
    cout << "    Name: " << STUDENT_NAME << endl;
    cout << "    Roll No: " << STUDENT_ROLLNO << endl;
    cout << "======================================" << endl;

    int roll_no_seed;
    cout << "Enter your 4-digit roll number (e.g., 1234) to seed simulation: ";
    cin >> roll_no_seed;

    // Seed the random number generator
    srand(roll_no_seed);
    
    // Initialize the active jet list
    for (int i = 0; i < MAX_JETS; i++) {
        active_jet_list[i].is_active = false;
    }

    // --- Assignment Requirement: Explanation of Seed ---
    // Using the roll number as a seed (srand) ensures that
    // the sequence of random numbers (rand) is unique to me.
    // This makes my simulation's output (jet arrival times, fuel)
    // different from other students' but repeatable for debugging.

    
    // --- Step 2: Create Pipe for Jet Generator ---
    int generator_pipe[2]; // [0] = read end, [1] = write end
    
    if (pipe(generator_pipe) == -1) {
        perror("Failed to create generator pipe");
        return 1;
    }
    
    // Store the write end for the child
    generator_pipe_write_end = generator_pipe[1];

    
    // --- Step 3: Fork the Jet Generator Process ---
    pid_t generator_pid = fork();

    if (generator_pid < 0) {
        // Fork failed
        perror("Failed to fork Jet Generator");
        return 1;
        
    } else if (generator_pid == 0) {
        // --- CHILD PROCESS (Jet Generator) ---
        
        // The generator doesn't read from the pipe, so close read end
        close(generator_pipe[0]); 
        
        // Run the generator's main function
        run_jet_generator();
        
        // Exit the child process
        exit(0); 

    } else {
        // --- PARENT PROCESS (ATC Tower) ---
        
        cout << "[ATC Tower " << getpid() << "]: Jet Generator process started (PID: " 
             << generator_pid << ")." << endl;
             
        // The tower doesn't write to this pipe, so close write end
        close(generator_pipe[1]);

        
        // --- Step 4: Create the ATC Display Thread ---
        pthread_t display_thread_id;
        if (pthread_create(&display_thread_id, NULL, display_loop, NULL) != 0) {
            perror("Failed to create ATC Display thread");
            return 1;
        }

        
        // --- Step 5: Main Scheduler Loop (Simple Version) ---
        cout << "[ATC Tower]: Waiting for new jets from generator..." << endl;
        JetMessage received_jet_request;

        // Read from the pipe until the generator closes it
        // read() will return 0 when the write end is closed
        while (read(generator_pipe[0], &received_jet_request, sizeof(JetMessage)) > 0) {
            
            // --- A new jet request has arrived ---
            cout << "[ATC Tower]: RECEIVED New Jet Request with " 
                 << received_jet_request.initial_fuel << " fuel." 
                 << endl;
            
            // --- Step 1: Create pipes for the new jet (ATC must do this) ---
            int atc_to_jet_pipe[2]; // ATC writes, Jet reads
            int jet_to_atc_pipe[2]; // Jet writes, ATC reads

            if (pipe(atc_to_jet_pipe) == -1 || pipe(jet_to_atc_pipe) == -1) {
                perror("ATC: Failed to create jet pipes");
                continue; // Skip this jet
            }

            // --- Step 2: Fork the new Jet process (ATC must do this) ---
            pid_t jet_pid = fork();
            if (jet_pid < 0) {
                perror("ATC: Failed to fork jet process");
                continue;
            }

            if (jet_pid == 0) {
                // --- CHILD PROCESS (The new Jet) ---

                // This process is the jet. Close all pipes it doesn't need.
                close(generator_pipe[0]);       // Not the generator
                close(atc_to_jet_pipe[1]);      // Jet only READS from this pipe
                close(jet_to_atc_pipe[0]);      // Jet only WRITES to this pipe

                // Prepare arguments for execlp
                char read_fd_str[10];
                char write_fd_str[10];
                char fuel_str[10];
                snprintf(read_fd_str, 10, "%d", atc_to_jet_pipe[0]);
                snprintf(write_fd_str, 10, "%d", jet_to_atc_pipe[1]);
                snprintf(fuel_str, 10, "%d", received_jet_request.initial_fuel);

                // Replace this process with the 'drone' program
                execlp("./drone", "drone", read_fd_str, write_fd_str, fuel_str, (char*)NULL);

                // If execlp returns, it failed
                perror("ATC: execlp failed");
                exit(1);
            }

            // --- PARENT PROCESS (ATC Tower) ---
            
            // ATC closes the jet's side of the pipes
            close(atc_to_jet_pipe[0]);
            close(jet_to_atc_pipe[1]);
            
            cout << "[ATC Tower]: Forked new jet (PID " << jet_pid << ")" << endl;

            // TODO: Add this jet to our 'active_jet_list'
            // active_jet_list[i].pid = jet_pid;
            // active_jet_list[i].read_fd = jet_to_atc_pipe[0];
            // active_jet_list[i].write_fd = atc_to_jet_pipe[1];
            // active_jet_list[i].fuel = received_jet_request.initial_fuel;
            // active_jet_list[i].is_active = true;
            // TODO: Add this jet to Queue 2
            
            // Test: Send landing command
            cout << "[ATC Tower]: Sending landing command to PID " << jet_pid << endl;
                 
            AtcCommandMessage cmd;
            cmd.command = CMD_START_LANDING;
            // Write to the *correct* file descriptor, which is now local
            if (write(atc_to_jet_pipe[1], &cmd, sizeof(AtcCommandMessage)) == -1) {
                perror("ATC: Failed to send landing command");
            }
            
        }

        cout << "[ATC Tower]: Jet Generator has shut down." << endl;

        
        // --- Step 6: Cleanup ---
        cout << "[ATC Tower]: Cleaning up resources." << endl;
        
        // Wait for the display thread to finish
        pthread_join(display_thread_id, NULL);
        
        // Wait for the generator child process to finish
        waitpid(generator_pid, NULL, 0); 
        
        // Close the read end of the pipe
        close(generator_pipe[0]);
        
        // TODO: In final cleanup, loop through active_jet_list
        // and close all atc_to_jet_pipe[1] and jet_to_atc_pipe[0] FDs.
        // Then waitpid() for all active jets.


        // TODO: Wait for all jet processes to finish
        // We will need to loop through our 'active_jet_list'
        // and call waitpid() for each one.
        // Also need to close all their pipes.

        cout << "[ATC Tower]: Simulation finished. Exiting." << endl;
    }

    return 0;
}

