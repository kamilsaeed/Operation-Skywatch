#include "utils.h"

// --- Student Information ---
// !! PLEASE FILL THESE OUT !!
const char* STUDENT_NAME = "Kamil Saeed";
const char* STUDENT_ROLLNO = "23i-2035";

// This is the write-end of the generator's pipe
// We make it global so the child process can find it,
// but we will close it in the parent.
int generator_pipe_write_end;

/**
 * @brief The main function for the Jet Generator process.
 * * This function will run in a separate child process.
 * Its only job is to create new jets at random intervals
 * and report them to the ATC Tower via a pipe.
 */
void run_jet_generator() 
{
    // This process is ONLY the generator.
    // It loops a few times to create sample jets, then exits.
    cout << "[Jet Generator " << getpid() << "]: Starting..." << endl;
    
    for (int i = 0; i < 5; i++) {
        // Wait for a random time (1 to 3 seconds)
        int wait_time = (rand() % 3) + 1;
        sleep(wait_time);

        // --- Simulate creating a new jet ---
        // In Phase 2, we will actually fork() a jet process here.
        // For now, we just make up its details.
        
        JetMessage new_jet;
        new_jet.jet_id = 100 + i; // Simulated PID
        new_jet.fuel = (rand() % 90) + 10; // Random fuel (10-99)

        // Announce the new jet to the console
        cout << "[Jet Generator " << getpid() << "]: Creating new jet " << new_jet.jet_id << " with fuel " << new_jet.fuel << endl;

        // Send the new jet's info to the ATC Tower
        // We write the 'JetMessage' struct directly to the pipe
        if (write(generator_pipe_write_end, &new_jet, sizeof(JetMessage)) == -1) 
        {
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
void* display_loop(void* arg) 
{
    cout << "[ATC Display Thread]: Display started." << endl;
    
    // In later phases, this loop will read the queue states
    // (using a mutex) and print the "radar" display.
    // For now, it just loops and sleeps.
    for (int i = 0; i < 10; i++) 
    {
        sleep(2);
        // We can add a simple status print here later
    }
    
    cout << "[ATC Display Thread]: Display shutting down." << endl;
    return NULL;
}


/**
 * @brief Main function for the ATC Tower
 */
int main() 
{
    
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
    
    // --- Assignment Requirement: Explanation of Seed ---
    // Using the roll number as a seed (srand) ensures that
    // the sequence of random numbers (rand) is unique to me.
    // This makes my simulation's output (jet arrival times, fuel)
    // different from other students' but repeatable for debugging.

    
    // --- Step 2: Create Pipe for Jet Generator ---
    int generator_pipe[2]; // [0] = read end, [1] = write end
    
    if (pipe(generator_pipe) == -1) 
    {
        perror("Failed to create generator pipe");
        return 1;
    }
    
    // Store the write end for the child
    generator_pipe_write_end = generator_pipe[1];

    
    // --- Step 3: Fork the Jet Generator Process ---
    pid_t generator_pid = fork();

    if (generator_pid < 0) 
    {
        // Fork failed
        perror("Failed to fork Jet Generator");
        return 1;
        
    } else if (generator_pid == 0) 
    {
        // --- CHILD PROCESS (Jet Generator) ---
        
        // The generator doesn't read from the pipe, so close read end
        close(generator_pipe[0]); 
        
        // Run the generator's main function
        run_jet_generator();
        
        // Exit the child process
        exit(0); 

    } else 
    {
        // --- PARENT PROCESS (ATC Tower) ---
        
        cout << "[ATC Tower " << getpid() << "]: Jet Generator process started (PID: " << generator_pid << ")." << endl;
            
        // The tower doesn't write to this pipe, so close write end
        close(generator_pipe[1]);

        
        // --- Step 4: Create the ATC Display Thread ---
        pthread_t display_thread_id;
        if (pthread_create(&display_thread_id, NULL, display_loop, NULL) != 0) 
        {
            perror("Failed to create ATC Display thread");
            return 1;
        }

        
        // --- Step 5: Main Scheduler Loop (Simple Version) ---
        cout << "[ATC Tower]: Waiting for new jets from generator..." << endl;
        JetMessage received_jet;

        // Read from the pipe until the generator closes it
        // read() will return 0 when the write end is closed
        while (read(generator_pipe[0], &received_jet, sizeof(JetMessage)) > 0) 
        {
            
            // This is where we will add the jet to Queue 2 in the next phase
            cout << "[ATC Tower]: RECEIVED New Jet " << received_jet.jet_id << " with " << received_jet.fuel << " fuel. Placing in Q2." << endl;
            
            // TODO: Add to MLFQ (Phase 3)
        }

        cout << "[ATC Tower]: Jet Generator has shut down." << endl;

        
        // --- Step 6: Cleanup ---
        cout << "[ATC Tower]: Cleaning up resources." << endl;
        
        // Wait for the display thread to finish
        pthread_join(display_thread_id, NULL);
        
        // Wait for the generator child process to finish
        wait(NULL); 
        
        // Close the read end of the pipe
        close(generator_pipe[0]);

        cout << "[ATC Tower]: Simulation finished. Exiting." << endl;
    }

    return 0;
}
