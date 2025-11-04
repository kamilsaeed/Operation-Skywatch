#ifndef UTILS_H
#define UTILS_H

// --- Standard C++ Libraries ---
#include <iostream>
#include <string>
#include <cstring> // For C-style string functions
#include <cstdio>  // For printf/perror
#include <cstdlib> // For exit, srand, rand
#include <ctime>   // For time

// --- POSIX/Linux Libraries ---
#include <unistd.h>     // For fork, pipe, read, write, sleep
#include <pthread.h>    // For POSIX threads
#include <sys/wait.h>   // For wait
#include <sys/types.h>  // For pid_t

// Using the standard namespace as requested
using namespace std;

const int MAX_JETS = 20;

/**
* @brief Message from Jet Generator OR Console to ATC Tower.
*/
struct JetMessage 
{
    int initial_fuel;
};

// --- Enums for Jet <-> ATC Communication ---

/**
* @brief Commands sent FROM the ATC Tower TO a Jet.
*/
enum AtcCommand
{
    CMD_START_LANDING,
    CMD_REFUEL,       // <-- NEW
    CMD_SHUTDOWN
};

/**
 * @brief Status messages sent FROM a Jet TO the ATC Tower.
 * --- MODIFIED: Re-ordered so STATUS_IN_QUEUE is 0 for clearer logging ---
 */
enum JetStatus 
{
    STATUS_IN_QUEUE,    // Jet is waiting in a queue (NOW 0)
    STATUS_FUEL_LOW,    // Warning
    STATUS_EMERGENCY,   // Critical, move to Q1
    STATUS_LANDED,      // Jet is done, process can be
    STATUS_WAITING_FUEL,// Jet is waiting to refuel (for Q3)
    
    STATUS_LANDING_CMD, // Command sent, waiting for STATUS_LANDED
    
    // --- NEW FOR REFUELING ---
    STATUS_REFUELING,   // Jet is currently refueling (runway busy)
    STATUS_REFUELED     // Jet has finished refueling
};

// --- Message Structs for Jet <-> ATC Pipes ---

struct AtcCommandMessage 
{
    AtcCommand command;
};

struct JetFeedbackMessage 
{
    JetStatus status;
    int data; // e.g., current fuel level
};

#endif // UTILS_H

