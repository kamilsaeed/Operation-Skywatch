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

// A simple message structure for the Jet Generator to
// send info about a new jet to the ATC Tower.
struct JetMessage {
    // We will use the actual PID of the Jet process later,
    // but for now, we'll just send a simulated ID.
    int jet_id; 
    
    // This will represent the fuel, which is used
    // as the "burst time" for the SRTF queue.
    int fuel;
};


#endif // UTILS_H
