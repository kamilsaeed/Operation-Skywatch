#include "utils.h"

// --- Student Information ---
const char* STUDENT_ROLLNO = "23i-2035";

// --- Global state for this jet ---
int my_fuel;
bool is_emergency = false;
bool keep_running = true;
bool is_landing = false; 

// Pipe FDs
int atc_read_fd;  
int atc_write_fd; 

const char* my_jet_id = "UNKNOWN-ID";

/**
 * @brief MODIFIED: Removed all cout statements
 * Only sends messages back to ATC tower
 */
void send_status(JetStatus status, int data = 0) 
{
    // --- REMOVED cout ---
         
    JetFeedbackMessage msg;
    msg.status = status;
    msg.data = data; 
    
    if (write(atc_write_fd, &msg, sizeof(JetFeedbackMessage)) == -1) 
    {
        perror("Jet: Pipe write error");
    }
}

// ... (fuel_thread_loop function is unchanged) ...
void* fuel_thread_loop(void* arg) 
{
    my_fuel = (int)(long)arg; 
    
    while (keep_running && my_fuel > 0) 
    {
        sleep(1);
        my_fuel--;
        
        if (is_landing) continue; 
        
        if (my_fuel == 20 && !is_emergency) 
        {
             send_status(STATUS_FUEL_LOW, my_fuel);
        }
        
        if (my_fuel == 25 && !is_emergency)
        {
            send_status(STATUS_WAITING_FUEL, my_fuel);
        }
        
        if (my_fuel <= 10 && !is_emergency) 
        {
            is_emergency = true;
            send_status(STATUS_EMERGENCY, my_fuel);
        }
    }
    
    // --- REMOVED cout ---
    return NULL;
}


/**
 * @brief The main loop for the jet process.
 * MODIFIED: Landing time is 12s, Refuel is 10s
 * MODIFIED: Removed all cout statements
 */
void run_jet_main_loop() 
{
    AtcCommandMessage command;

    while (keep_running) 
    {
        ssize_t bytes_read = read(atc_read_fd, &command, sizeof(AtcCommandMessage));
        
        if (bytes_read <= 0) 
        {
            if (bytes_read == 0) {
                // --- REMOVED cout ---
            } else {
                perror("Jet: Pipe read error");
            }
            keep_running = false;
            break; 
        }

        if (command.command == CMD_START_LANDING) 
        {
            is_landing = true;
            
            // --- REMOVED cout ---
            
            // --- MODIFIED: Landing now takes 12 seconds ---
            sleep(12); 
            
            // --- REMOVED cout ---
                 
            send_status(STATUS_LANDED);
            keep_running = false;
        }
        else if (command.command == CMD_REFUEL)
        {
            // --- REMOVED cout ---
                 
            send_status(STATUS_REFUELING); 
            
            // --- MODIFIED: Refuel now takes 10 seconds ---
            sleep(10);
            my_fuel += 75; 
            
            // --- REMOVED cout ---
            
            send_status(STATUS_REFUELED, my_fuel);
        }
    }
}


/**
 * @brief MODIFIED: Removed all cout statements
 */
int main(int argc, char* argv[]) 
{
    if (argc != 5) 
    {
        // Keep this one cout for critical argument errors
        cout << "Jet Process: Invalid arguments. " << "Expected: <read_fd> <write_fd> <fuel> <jet_id>" << endl;
        return 1;
    }
    
    atc_read_fd = atoi(argv[1]);
    atc_write_fd = atoi(argv[2]);
    int initial_fuel = atoi(argv[3]);
    my_jet_id = argv[4];
    
    // --- REMOVED cout ---

    pthread_t fuel_thread_id;
    if (pthread_create(&fuel_thread_id, NULL, fuel_thread_loop, (void*)(long)initial_fuel) != 0) 
    {
        perror("Jet: Failed to create fuel thread");
        return 1;
    }
    
    run_jet_main_loop();
    
    // --- REMOVED cout ---
        
    pthread_join(fuel_thread_id, NULL);
    
    close(atc_read_fd);
    close(atc_write_fd);
    
    return 0;
}

