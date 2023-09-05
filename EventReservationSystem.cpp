#include <iostream>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#include <stdlib.h>

using namespace std;

// Maximum handlable constaints
const int numOfEvents = 100;              // number of events
const int capacityOfAuditorium = 500;     // capacity of auditorium
const int numOfWorkerThreads = 20;        // number of worker threads
const int maxActiveQueries = 5;           // maximum number of active queries
const int runningTime = 30;              // total running time (seconds)
const int minTickets = 5;                 // Number of minimum tickets to book
const int maxTickets = 10;                // Number of maximum tickets to book


// Structure for query information
struct queryInfo {
    int eventId; // Event number
    int qType; // Query type. 0 - INQUIRE, 1 - BOOK, 2 - CANCEL
    int threadId; // Thread making the query
};

typedef struct queryInfo queryInfo;

// Shared table to hold the details
queryInfo sharedTable[maxActiveQueries];

// Seats available in each event
vector<int> availableSeats(numOfEvents+1, capacityOfAuditorium);

// Mutexes to handle the critical section
pthread_mutex_t tableMutex;
pthread_cond_t tableCondition = PTHREAD_COND_INITIALIZER;
pthread_mutex_t seatMutex;

// Required Functions to implement the reservation system 
void* workerThread(void* tid);
int getRandomNumberInRange(int m, int n);
void inquireEvent(int eId, int tId);
int bookEvent(int eId, int seatsToBook, int tId);
int cancelEvent(int tId, vector<pair<int, int> >& bookings);
bool canRead(int eventId);
bool canWrite(int eventId);
int findBlankEntry();

/**
 * The master thread that handles the initialization and thread creation part.
*/
int main(int argc, char const *argv[])
{
    cout << "\n ------------- Reservation simulation started -------------";
    // Initialize mutex and condition
    pthread_mutex_init(&tableMutex, NULL);
    pthread_mutex_init(&seatMutex, NULL);
    
    // Create worker threads
    pthread_t threadIds[numOfWorkerThreads]; // Threads
    for (int i = 0; i < numOfWorkerThreads; i++) {
        void* t = &i;
        pthread_create(&threadIds[i], NULL, workerThread, t);
    }

    // Sleep for T seconds allowing the threads to perform random queries for T seconds.
    sleep(runningTime);

    // Signal worker threads to exit
    for (int i = 0; i < numOfWorkerThreads; i++)
        pthread_cancel(threadIds[i]);

    // Wait for worker threads to exit
    for (int i = 0; i < numOfWorkerThreads; i++)
        pthread_join(threadIds[i], NULL);

    // Print reservation status
    cout << "\n\n------------- Final reservation status of all events -------------" << endl;
    for (int i = 1; i <= numOfEvents; i++) {
        float perc = (capacityOfAuditorium - availableSeats[i])/(float)capacityOfAuditorium * 100;
        printf("[Event - %d]: %.2f %% booked with %d seats leftover.\n", i, perc, availableSeats[i]);
    }

    // Destroy the created mutexes
    pthread_mutex_destroy(&tableMutex);
    pthread_mutex_destroy(&seatMutex);
    pthread_cond_destroy(&tableCondition);

    return 0;
}

/**
 * Returns the random number in the provided range [m, n]
*/
int getRandomNumberInRange(int m, int n) {
    return rand() % (n - m + 1) + m;
}

/**
 * The function that would be executed by all the slave threads
*/
void* workerThread(void* tid) {
    // Getting the thread id
    int threadId = *((int*) tid);
    srand(time(NULL) + threadId); // seeding for randomness

    // Local copy of all the bookings made by the thread.
    // It stores the pair of two values. (seats booked, event for which it was booked)
    vector<pair<int, int> > bookings;

    // Runs till master thread commands for exit
    while (true) {
        int queryType = getRandomNumberInRange(0, 3);    // randomly choose a type of query
        int eventNum = getRandomNumberInRange(1, numOfEvents); // randomnly chooses the event for which the query is to be executed 

        // Switch case to go to specific query
        switch (queryType)
        {
        case 0: { // query to INQUIRE details about an event
            inquireEvent(eventNum, threadId);
            sleep(getRandomNumberInRange(1, 3));   // waits for 1 - 3 seconds randomnly.
            break;
        }
        
        case 1: { // query to Book seats in an event 
            int seatsToBook = getRandomNumberInRange(minTickets, maxTickets);   // choose a random number of tickets to book from the given range
            int success = bookEvent(eventNum, seatsToBook, threadId); // Check if the booking is successful or not

            // If the booking is successful then store the booking in the local bookings vector
            if (success == 1) bookings.push_back(make_pair(seatsToBook, eventNum));

            sleep(getRandomNumberInRange(1, 3));   // waits for 1 - 3 seconds randomnly.
            break;
        }

        case 2: { // query to Cancel booked ticket for an event
            cancelEvent(threadId, bookings);
            sleep(getRandomNumberInRange(1, 3));   // waits for 1 - 3 seconds randomnly.
            break;
        }

        }
    }
}


/**
 * Inquiry for an event
*/
void inquireEvent(int eId, int tId) {

    // Locks the shared table to check if the query can be performed or not
    pthread_mutex_lock(&tableMutex);
    // Finding the blank entry to ensure the maximum queries
    int tableEntry = findBlankEntry();

    // Check if the thread can read the seats for this event or not.
    // If not then wait till signaled by other thread
    if (tableEntry == -1 || !canRead(eId)) {
        // Waiting till condition meets
        pthread_cond_wait(&tableCondition, &tableMutex);
    }

    tableEntry = findBlankEntry();
    if (tableEntry == -1) return;

    // Update the shared table before starting the query
    sharedTable[tableEntry].eventId = eId;
    sharedTable[tableEntry].qType = 0;
    sharedTable[tableEntry].threadId = tId;

    // Unlock the table
    pthread_mutex_unlock(&tableMutex);


    // Acquire the lock for seats as we would be reading the same
    pthread_mutex_lock(&seatMutex);
    
    if (availableSeats[eId] == 0)
        printf("\n\n[INQUIRE][Thread - %d][Event - %d]: The event is housefull..!", tId, eId);
    else
        printf("\n\n[INQUIRE][Thread - %d][Event - %d]: There are %d seats available.", tId, eId, availableSeats[eId]);

    // Unlock the seat vector
    pthread_mutex_unlock(&seatMutex);

    // Aquire the shared table mutex for reverting the table entry
    pthread_mutex_lock(&tableMutex);
    sharedTable[tableEntry].eventId = 0;
    // Unclock it
    pthread_mutex_unlock(&tableMutex);
    // Signal other threads that might be waiting for this thread to finish
    pthread_cond_signal(&tableCondition);

}

/**
 * Booking the seats if available for the event specified
*/
int bookEvent(int eId, int seatsToBook, int tId) {

    // Locks the shared table to check if the query can be performed or not
    pthread_mutex_lock(&tableMutex);
    // Finding the blank entry to ensure the maximum queries
    int tableEntry = findBlankEntry();

    // Check if the thread can read the seats for this event or not.
    // If not then wait till signaled by other thread
    if (tableEntry == -1 || !canWrite(eId)) {
        // Waiting till condition meets
        pthread_cond_wait(&tableCondition, &tableMutex);
    }

    tableEntry = findBlankEntry();
    // Return if number of active query exceeds the allowable limit
    if (tableEntry == -1) return -1;

    // Update the shared table before starting the query
    sharedTable[tableEntry].eventId = eId;
    sharedTable[tableEntry].qType = 1;
    sharedTable[tableEntry].threadId = tId;

    // Unlock the table
    pthread_mutex_unlock(&tableMutex);

    // Acquire seats lock
    pthread_mutex_lock(&seatMutex);

    if (seatsToBook > availableSeats[eId]) {
        printf("\n\n[BOOK][Thread - %d][Event - %d]: Enough seats are not available to book %d seats!", tId, eId, seatsToBook);
        pthread_mutex_unlock(&seatMutex); // Unlock before returning
        return -1; // Unable to book
    }


    printf("\n\n[BOOK][Thread - %d][Event - %d]: Booked %d seats for the event.", tId, eId, seatsToBook);

    // Updating the seats for the event
    availableSeats[eId] -= seatsToBook;

    // Unlocking the seat
    pthread_mutex_unlock(&seatMutex);


    // Aquire the shared table mutex for reverting the table entry
    pthread_mutex_lock(&tableMutex);
    sharedTable[tableEntry].eventId = 0;
    // Unclock it
    pthread_mutex_unlock(&tableMutex);
    // Signal other threads that might be waiting for this thread to finish
    pthread_cond_signal(&tableCondition);

    return 1; // No error in booking
}

/**
 * Cancel the booking for specific event
*/
int cancelEvent(int tId, vector<pair<int, int> >& bookings) {
    
    // Check if the thread has made any bookings or not. If not then return
    int totalBookings = bookings.size();
    if (totalBookings < 1) {
        printf("\n\n[CANCEL][Thread - %d]: No bookings found from the thread for any event.", tId);
        return -1;
    }
    
    // Find the random booking from the list of bookings made by the thread
    int pos = getRandomNumberInRange(0, totalBookings - 1);
    int numOfSeats = getRandomNumberInRange(1, bookings[pos].first); // Get the number of seats to cancel
    int eId = bookings[pos].second; // get the event id for which the cancelation is to be made

        // Locks the shared table to check if the query can be performed or not
    pthread_mutex_lock(&tableMutex);
    // Finding the blank entry to ensure the maximum queries
    int tableEntry = findBlankEntry();

    // Check if the thread can read the seats for this event or not.
    // If not then wait till signaled by other thread
    if (tableEntry == -1 || !canWrite(eId)) {
        // Waiting till condition meets
        pthread_cond_wait(&tableCondition, &tableMutex);
    }

    tableEntry = findBlankEntry();
    // Return if number of active query exceeds the allowable limit
    if (tableEntry == -1) return -1;

    // Update the shared table before starting the query
    sharedTable[tableEntry].eventId = eId;
    sharedTable[tableEntry].qType = 2;
    sharedTable[tableEntry].threadId = tId;

    // Unlock the table
    pthread_mutex_unlock(&tableMutex);

    // Lock the seat
    pthread_mutex_lock(&seatMutex);

    printf("\n\n[CANCEL][Thread - %d][Event - %d]: Canceled %d seats for the event.", tId, eId, numOfSeats);

    // Updating available seats
    availableSeats[eId] += numOfSeats;

    // Removing the booking from the threads list as it is cancelled
    bookings.erase(bookings.begin() + pos);
    // Unlocking the seats
    pthread_mutex_unlock(&seatMutex);

    // Resetting the shared table entry
    pthread_mutex_lock(&tableMutex);
    sharedTable[tableEntry].eventId = 0;
    pthread_mutex_unlock(&tableMutex);
    pthread_cond_signal(&tableCondition);

    return 1; //Successfully canceled
}

/**
 * Checks if the thread can read the details for the event specified
*/
bool canRead(int eventId) {
    // If the same event is there in write mode then return false
    for (int i = 0; i < maxActiveQueries; i++) {
        if (sharedTable[i].eventId == eventId && sharedTable[i].qType != 0)
            return false;
    }
    // true otherwise
    return true;
}

/**
 * Checks if the thread can update the details about specified event
*/
bool canWrite(int eventId) {
    // If the event is already accessed in read/write mode then return false
    for (int i = 0; i < maxActiveQueries; i++) {
        if (sharedTable[i].eventId == eventId)
            return false;
    }
    // True otherwise
    return true;
}

/**
 * Finds the blank entry to add the event query to the shared table
*/
int findBlankEntry() {
    for (int i = 0; i < maxActiveQueries; i++) {
        if (sharedTable[i].eventId == 0) {
            return i;
        }
    }
    return -1;
}