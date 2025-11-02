#include "utils.h"

// --- Student Information ---
// !! PLEASE FILL THIS OUT !!
const char* STUDENT_ROLLNO = "23i-2035";

// --- Global state for this jet ---
int my_fuel;
bool is_emergency = false;
bool keep_running = true;

// Pipe FDs for this jet
int atc_read_fd;  // Jet reads from this (ATC's write-end)
int atc_write_fd; // Jet writes to this (ATC's read-end)

/**
 * @brief Sends a status message back to the ATC Tower.
 * This is a helper function to make communication easier.
 */
void send_status(JetStatus status, int data = 0) 
{
    // This is the UAV Tag as required by the assignment
    cout << "[UAV-" << STUDENT_ROLLNO << "] (PID " << getpid() 
         << "): Sending Status " << status << endl;
         
    JetFeedbackMessage msg;
    msg.status = status;
    msg.data = data; // e.g., current fuel
    
    if (write(atc_write_fd, &msg, sizeof(JetFeedbackMessage)) == -1) 
    {
        perror("Jet: Pipe write error");
    }
}

/**
 * @brief The function for the jet's internal fuel thread.
 * * It decrements fuel and sends an emergency message when low.
 */
void* fuel_thread_loop(void* arg) 
{
    
    // The initial fuel is passed as the argument
    my_fuel = (int)(long)arg; 
    
    while (keep_running && my_fuel > 0) 
    {
        // Wait for 1 second
        sleep(1);
        my_fuel--;
        
        // Send a low fuel warning (but not emergency yet)
        if (my_fuel == 20 && !is_emergency) 
        {
             send_status(STATUS_FUEL_LOW, my_fuel);
        }
        
        // Send the EMERGENCY status when fuel is critical
        if (my_fuel <= 10 && !is_emergency) 
        {
            is_emergency = true;
            // This message will trigger the ATC to move this
            // jet to the high-priority Q1.
            send_status(STATUS_EMERGENCY, my_fuel);
        }
    }
    
    cout << "[UAV-" << STUDENT_ROLLNO << "] (PID " << getpid() 
         << "): Fuel thread exiting." << endl;
    return NULL;
}

/**
 * @brief The main loop for the jet process.
 * * It listens for commands from the ATC Tower.
 */
void run_jet_main_loop() 
{
    AtcCommandMessage command;

    // Loop while waiting for commands from the ATC
    while (keep_running) 
    {
        
        // This read() will block until the ATC sends a command
        ssize_t bytes_read = read(atc_read_fd, &command, sizeof(AtcCommandMessage));
        
        if (bytes_read <= 0) 
        {
            // ATC closed the pipe, so we should exit
            if (bytes_read == 0) 
            {
                cout << "[UAV-" << STUDENT_ROLLNO << "] (PID " << getpid() 
                     << "): ATC closed pipe. Exiting." << endl;
            } else 
            {
                perror("Jet: Pipe read error");
            }
            keep_running = false;
            break; 
        }

        // Process the command from the ATC
        if (command.command == CMD_START_LANDING) 
        {
            cout << "[UAV-" << STUDENT_ROLLNO << "] (PID " << getpid() 
                 << "): Received landing command. Simulating landing." << endl;
            
            // Simulate landing (takes 3 seconds)
            sleep(3); 
            
            cout << "[UAV-" << STUDENT_ROLLNO << "] (PID " << getpid() 
                 << "): Landing complete." << endl;
                 
            // Tell the ATC we have landed
            send_status(STATUS_LANDED);
            
            // Our job is done
            keep_running = false;
        }
        // We can add more commands here later (e.g., refuel)
    }
}


/**
 * @brief Main function for the Jet (Drone) process
 * * This program is executed by the Jet Generator.
 * It expects 3 command-line arguments:
 * argv[1]: ATC-to-Jet pipe (read end for jet)
 * argv[2]: Jet-to-ATC pipe (write end for jet)
 * argv[3]: Initial fuel
 */
int main(int argc, char* argv[]) 
{
    
    if (argc != 4) 
    {
        cout << "Jet Process: Invalid arguments. " << "Expected: <read_fd> <write_fd> <fuel>" << endl;
        return 1;
    }
    
    // Parse the pipe file descriptors and fuel from arguments
    atc_read_fd = atoi(argv[1]);
    atc_write_fd = atoi(argv[2]);
    int initial_fuel = atoi(argv[3]);
    
    cout << "[UAV-" << STUDENT_ROLLNO << "] (PID " << getpid() << "): Process started. Fuel: " << initial_fuel << endl;

    // --- Create the internal fuel thread ---
    pthread_t fuel_thread_id;
    // We pass the initial fuel as the argument to the thread
    if (pthread_create(&fuel_thread_id, NULL, fuel_thread_loop, (void*)(long)initial_fuel) != 0) 
    {
        perror("Jet: Failed to create fuel thread");
        return 1;
    }
    
    // --- Run the main jet loop ---
    run_jet_main_loop();
    
    // --- Cleanup ---
    cout << "[UAV-" << STUDENT_ROLLNO << "] (PID " << getpid() << "): Shutting down." << endl;
        
    // Wait for the fuel thread to finish
    pthread_join(fuel_thread_id, NULL);
    
    // Close pipes
    close(atc_read_fd);
    close(atc_write_fd);
    
    return 0;
}
