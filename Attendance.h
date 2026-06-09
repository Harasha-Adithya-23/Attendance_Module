#ifndef ATTENDANCE_H
#define ATTENDANCE_H

#include <time.h>

/* Structure to store daily attendance parameters for each user */
typedef struct {
    int    user_id;         // Unique identifier for the user (0 to 63)
    time_t loginTime;       // Timestamp of the user's most recent login session
    double totalHoursToday; // Cumulative working hours accumulated throughout the day
    int    sessionCount;    // Number of completed login-logout cycles for the day
    int    isEnrolled;      // Flag indicating if the user's fingerprint is registered
    int    isIN;            // Flag indicating if the user is currently clocked in
} attendence;

/* Global record tracking array for up to 64 hardware users */
extern attendence Record[64];

/* ── Helper Functions ────────────────────────────────────────────────────── */

// Evaluates the cumulative hours to return a structural status string ("Overtime", "Normal", "Half Day").
const char *getAttendanceStatus(double hours);

// Converts a raw time_t timestamp into a human-readable "DD-MM-YYYY HH:MM:SS" string.
void fmtTime(time_t t, char *buf, size_t len);

// Verifies if two distinct time_t timestamps share the same day and year.
int sameDay(time_t a, time_t b);

/* ── Initialization & Sensor Setup ───────────────────────────────────────── */

// Populates the primary Record array with baseline default values for all 64 slots.
void setDefault(void);

// Requests and updates internal enrollment states based on the active fingerprint database.
void getPrevData(void);

// Syncs and overwrites local data to "Enrolled.csv" containing a comma-separated list of active user IDs.
void updateData(void);

/* ── User Enrollment Management ──────────────────────────────────────────── */

// Prompt-driven interface to safely register a new fingerprint ID into the system.
void enroll(void);

// Interactive terminal menu ensuring legal verification and deletion of a target fingerprint ID.
void deleteUser_menu(void);

/* ── Verification & Core Processing ───────────────────────────────────────── */

// Executes fingerprint hardware scanning to seamlessly toggle an user's state between Login and Logout.
void identify(void);

/* ── End-of-Day Management ───────────────────────────────────────────────── */

// Finalizes structural summaries, computes concluding status metrics, and writes them to the CSV.
void endDay(int id);

// Selection menu to fetch and submit a verified User ID for global end-of-day execution.
void endDayMenu(void);

/* ── Utility Logging ─────────────────────────────────────────────────────── */

// Hard resets "Biometric.csv" with fresh database column headers, clearing previous records.
void resetLog(void);

/* ── Main Routing Systems ────────────────────────────────────────────────── */

// Runs a perpetual control loop presenting the hardware user interface menu options.
void AttendanceMenu(void);

#endif /* ATTENDANCE_H */
