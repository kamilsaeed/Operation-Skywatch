# Operation-Skywatch
=======================================
Operating Systems (CS-2006) - Assignment 02
Operation Skywatch (ChronoArena)
=======================================

Student Name: Student Name
Roll No: 23i-2035

-------------------
1. DESCRIPTION
-------------------
This project is a C++ simulation of an Air Traffic Control (ATC) tower managing jet processes. It implements a three-layer Multi-Level Feedback Queue (MLFQ) as required by the assignment.

The simulation uses POSIX threads (pthreads) for concurrent tasks (display, scheduler clock, console) and inter-process communication (pipes) to manage multiple jet processes forked from the main ATC tower.

-------------------
2. FILES INCLUDED
-------------------
- main.cpp (The main ATC Tower process)
- scheduler.cpp (The MLFQ scheduler logic)
- scheduler.h
- drone.cpp (The Jet process)
- utils.h
- ReadMe.txt (this file)
- 23i-2035_skywatch_log.txt (Generated log file, appends on each run)
- 23i-2035_Report.pdf (Assignment report - *you must create this*)

-------------------
3. HOW TO COMPILE
-------------------

1. Compile the ATC Tower (`main`):
g++ main.cpp scheduler.cpp -o main -lpthread

2. Compile the Jet Process (`drone`):
g++ drone.cpp -o drone -lpthread

-------------------
4. HOW TO RUN
-------------------

After compiling both executables, run the main ATC Tower:

./main

The program will first ask for your 4-digit roll number to seed the simulation.
Example:
Enter your 4-digit roll number (e.g., 2035) to seed simulation: 2035

The simulation will then start. The `main` program will automatically call `./drone` using `fork()` and `execlp()` for each new jet created.

-------------------
5. FEATURES & CONSOLE COMMANDS
-------------------
This simulation implements all required features from the PDF:

- Q1 (SRTF): Handles emergency jets (fuel <= 10) and preemption.
- Q2 (RR): Handles new arrivals.
- Q3 (FCFS): Handles demoted jets (from RR quantum expiry) and refueling requests.
- Aging: Jets that wait in Q3 for 10 seconds are promoted back to Q2.
- Logging: All events are logged to `23i-2035_skywatch_log.txt`.
- Jet Naming: Jets are named using the roll number (e.g., 35-01, 35-02).

You can interact with the scheduler using these commands:

- status: Force a refresh of the radar display.
- new_jet <fuel>: Manually create a new jet with <fuel>.
- force_emergency <pid>: Force a jet to declare an emergency.
- boost_priority <pid>: Manually promote a jet (Q3->Q2 or Q2->Q1).
- change_quantum <val>: Change the time quantum for Q2.
- pause_sim: Pause the scheduler clock.
- resume_sim: Resume the scheduler clock.
- exit: Gracefully shut down the simulation.