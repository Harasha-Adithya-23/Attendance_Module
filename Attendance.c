#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdlib.h>
#include <time.h>
#include "A600.h"

typedef struct {
    int    user_id;
    time_t loginTime;
    double totalHoursToday;
    int    sessionCount;
    int    isEnrolled;
    int    isIN;
} attendence;

attendence Record[64];

/* ── helpers ──────────────────────────────────────────────────────────────── */

const char *getAttendanceStatus(double hours)
{
    if (hours >= 8.0) return "Overtime";
    if (hours >= 5.0) return "Normal";
    return "Half Day";
}

void fmtTime(time_t t, char *buf, size_t len)
{
    struct tm *tm_info = localtime(&t);
    strftime(buf, len, "%d-%m-%Y %H:%M:%S", tm_info);
}

int sameDay(time_t a, time_t b)
{
    struct tm *ta = localtime(&a);
    int da = ta->tm_yday, ya = ta->tm_year;
    struct tm *tb = localtime(&b);
    return (da == tb->tm_yday && ya == tb->tm_year);
}

/* ── init ─────────────────────────────────────────────────────────────────── */

void setDefault()
{
    for (int i = 0; i < 64; i++) {
        Record[i].user_id         = i;
        Record[i].isEnrolled      = 0;
        Record[i].isIN            = 0;
        Record[i].totalHoursToday = 0.0;
        Record[i].sessionCount    = 0;
    }
}

/* ── sensor helpers ───────────────────────────────────────────────────────── */

void getPrevData()
{
    strcpy(msg, "AT+GET+ENROLLED_LIST\r\n");
    int *list = sendEnrolled(msg);
    int   i   = 0;
    while (list[i] != -1)
        Record[list[i++]].isEnrolled = 1;
}

void updateData()
{
    strcpy(msg, "AT+GET+ENROLLED_LIST\r\n");
    int *list = sendEnrolled(msg);
    int   i   = 0;
    FILE *fp  = fopen("Enrolled.csv", "w");
    while (list[i] != -1) {
        fprintf(fp, "%d", list[i++]);
        if (list[i] != -1) fprintf(fp, ",");
    }
    fclose(fp);
}

/* ── enroll / delete ──────────────────────────────────────────────────────── */

void enroll()
{
    int id;
    while (1) {
        printf("Enter User ID to enroll: ");
        scanf("%d", &id);
        if (id < 0 || id > 63 || Record[id].isEnrolled)
            printf("ID Not Valid\n");
        else break;
    }
    enrollFinger(id);
    Record[id].isEnrolled = 1;
}

void deleteUser_menu()
{
    int id;
    while (1) {
        printf("Enter User ID to delete (-1 to cancel): ");
        scanf("%d", &id);
        if (id == -1) return;
        if (id < 0 || id > 63 || !Record[id].isEnrolled)
            printf("ID Not Valid\n");
        else break;
    }
    deleteUser(id);
    Record[id].isEnrolled = 0;
}

/* ── identify (login / logout) ────────────────────────────────────────────── */

void identify()
{
    int id = identifyFinger();

    if (id < 0 || id > 63) {
        printf("Not Verified\n");
        return;
    }
    if (!Record[id].isEnrolled) {
        printf("User %d not enrolled\n", id);
        return;
    }

    char loginBuf[100], logoutBuf[100];
    time_t now = time(NULL);

    /* ── LOGOUT ── */
    if (Record[id].isIN) {

        if (!sameDay(Record[id].loginTime, now)) {
            printf("Note: login was on a previous day — resetting daily total.\n");
            Record[id].totalHoursToday = 0.0;
            Record[id].sessionCount    = 0;
        }

        double sessionHours = difftime(now, Record[id].loginTime) / 3600.0;
        Record[id].totalHoursToday += sessionHours;
        Record[id].sessionCount++;

        fmtTime(Record[id].loginTime, loginBuf,  sizeof(loginBuf));
        fmtTime(now,                  logoutBuf, sizeof(logoutBuf));

        printf("\nUser %d — Session %d logout\n", id, Record[id].sessionCount);
        printf("  Login         : %s\n",   loginBuf);
        printf("  Logout        : %s\n",   logoutBuf);
        printf("  Session hours : %.2f h\n", sessionHours);
        printf("  Total today   : %.2f h\n", Record[id].totalHoursToday);

        /* No status column — just the raw session data */
        FILE *fp = fopen("Biometric.csv", "a");
        fprintf(fp, "%d,%s,%s,%.2f,%d\n",
                id, loginBuf, logoutBuf,
                sessionHours, Record[id].sessionCount);
        fclose(fp);

        Record[id].isIN = 0;
        return;
    }

    /* ── LOGIN ── */
    if (Record[id].sessionCount > 0 && !sameDay(Record[id].loginTime, now)) {
        Record[id].totalHoursToday = 0.0;
        Record[id].sessionCount    = 0;
    }

    Record[id].loginTime = now;
    Record[id].isIN      = 1;

    fmtTime(now, loginBuf, sizeof(loginBuf));
    printf("\nUser %d — Session %d login\n", id, Record[id].sessionCount + 1);
    printf("  Login         : %s\n",   loginBuf);
    printf("  Total so far  : %.2f h\n", Record[id].totalHoursToday);
}

/* ── end-of-day: append one summary row with status ──────────────────────── */

void endDay(int id)
{
    if (!Record[id].isEnrolled)      { printf("User %d not enrolled\n", id);          return; }
    if (Record[id].isIN)             { printf("User %d still IN — logout first\n", id); return; }
    if (Record[id].sessionCount == 0){ printf("No sessions today for user %d\n", id); return; }

    double      total = Record[id].totalHoursToday;
    const char *st    = getAttendanceStatus(total);

    char today[64];
    fmtTime(time(NULL), today, sizeof(today));

    /* Simply append the summary row — no rewriting needed */
    FILE *fp = fopen("Biometric.csv", "a");
    fprintf(fp, "TOTAL_U%d,%d sessions,%s,%.2f,%s\n",
            id, Record[id].sessionCount, today, total, st);
    fclose(fp);

    printf("\n=== End-of-Day Summary — User %d ===\n", id);
    printf("  Sessions : %d\n",     Record[id].sessionCount);
    printf("  Total    : %.2f h\n", total);
    printf("  Status   : %s\n",     st);

    Record[id].totalHoursToday = 0.0;
    Record[id].sessionCount    = 0;
}

void endDayMenu()
{
    int id;
    printf("Enter User ID for end-of-day (-1 to cancel): ");
    scanf("%d", &id);
    if (id == -1) return;
    if (id < 0 || id > 63) { printf("Invalid ID\n"); return; }
    endDay(id);
}

/* ── CSV reset ────────────────────────────────────────────────────────────── */

void resetLog()
{
    FILE *fp = fopen("Biometric.csv", "w");
    fprintf(fp, "User_id,Login_Time,Logout_Time,Session_Hours,Session_No\n");
    fclose(fp);
    printf("Biometric.csv cleared.\n");
}

/* ── menu ─────────────────────────────────────────────────────────────────── */

void AttendanceMenu()
{
    while (1) {
        int choice;
        printf("\n---------------------MENU--------------------------\n");
        printf("  1 Place FingerPrint (Login / Logout)\n");
        printf("  2 Enroll FingerPrint\n");
        printf("  3 Remove FingerPrint\n");
        printf("  4 End Day  (finalise daily total + status)\n");
        printf("  5 Reset/Clear Biometric.csv\n");
        printf("  0 Exit\n");
        printf("Enter choice: ");
        scanf("%d", &choice);

        switch (choice) {
            case 0: return;
            case 1: identify();        break;
            case 2: enroll();          break;
            case 3: deleteUser_menu(); break;
            case 4: endDayMenu();      break;
            case 5: resetLog();        break;
        }
    }
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main()
{
    if (startAndConfigUart(DEVICE) == -1)
        return 1;

    setDefault();
    verbose = 0;
    getPrevData();
    AttendanceMenu();
    updateData();
    close(port);
    return 0;
}
