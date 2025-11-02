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

/**
* @brief Message from Jet Generator to ATC Tower.
* Sent once when a new jet is created.
*/
struct JetMessage 
{
    // pid_t jet_pid;      // The REAL PID of the forked jet
    // int atc_read_fd;    // The FD for ATC to read from (Jet's write-end)
    // int atc_write_fd;   // The FD for ATC to write to (Jet's read-end)
    // int initial_fuel;   // The starting fuel
    int initial_fuel;
};

// --- Enums for Jet <-> ATC Communication ---

/**
* @brief Commands sent FROM the ATC Tower TO a Jet.
*/
enum AtcCommand
{
    CMD_START_LANDING,
    CMD_REFUEL,
    CMD_SHUTDOWN
};

/**
 * @brief Status messages sent FROM a Jet TO the ATC Tower.
 */
enum JetStatus 
{
    STATUS_FUEL_LOW,    // Warning
    STATUS_EMERGENCY,   // Critical, move to Q1
    STATUS_LANDED,      // Jet is done, process can be terminated
    STATUS_WAITING_FUEL // Jet is waiting to refuel (for Q3)
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
