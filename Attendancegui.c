/*
 * AttendanceGUI.c
 *
 * Simple GTK3 GUI for the A600 Fingerprint Attendance System.
 *
 * Compile:
 *   gcc AttendanceGUI.c A600.c -o AttendanceGUI \
 *       $(pkg-config --cflags --libs gtk+-3.0) -lm
 *
 * Run:
 *   ./AttendanceGUI
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "A600.h"

/* ─────────────────────────────────────────────
   Attendance record (mirrors Attendance.c)
───────────────────────────────────────────── */
typedef struct {
    int    user_id;
    time_t loginTime;
    double totalHoursToday;
    int    sessionCount;
    int    isEnrolled;
    int    isIN;
} Attendance;

static Attendance Record[64];

/* ─────────────────────────────────────────────
   GUI widgets we need to access globally
───────────────────────────────────────────── */
static GtkWidget *statusLabel;   /* big status line at the top  */
static GtkWidget *logView;       /* scrollable log text area    */
static GtkTextBuffer *logBuffer;

/* ─────────────────────────────────────────────
   Utility: append a line to the log area
───────────────────────────────────────────── */
static void log_msg(const char *text)
{
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(logBuffer, &end);
    gtk_text_buffer_insert(logBuffer, &end, text, -1);
    gtk_text_buffer_insert(logBuffer, &end, "\n", -1);

    /* auto-scroll to bottom */
    GtkTextMark *mark = gtk_text_buffer_get_mark(logBuffer, "insert");
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(logView), mark);
}

/* ─────────────────────────────────────────────
   Utility: set the big status label
───────────────────────────────────────────── */
static void set_status(const char *text)
{
    gtk_label_set_text(GTK_LABEL(statusLabel), text);
}

/* ─────────────────────────────────────────────
   Attendance helpers (same logic as Attendance.c)
───────────────────────────────────────────── */
static void fmt_time(time_t t, char *buf, size_t len)
{
    struct tm *tm_info = localtime(&t);
    strftime(buf, len, "%d-%m-%Y %H:%M:%S", tm_info);
}

static int same_day(time_t a, time_t b)
{
    struct tm *ta = localtime(&a);
    int da = ta->tm_yday, ya = ta->tm_year;
    struct tm *tb = localtime(&b);
    return (da == tb->tm_yday && ya == tb->tm_year);
}

static const char *attendance_status(double hours)
{
    if (hours >= 8.0) return "Overtime";
    if (hours >= 5.0) return "Normal";
    return "Half Day";
}

static void set_default(void)
{
    for (int i = 0; i < 64; i++) {
        Record[i].user_id         = i;
        Record[i].isEnrolled      = 0;
        Record[i].isIN            = 0;
        Record[i].totalHoursToday = 0.0;
        Record[i].sessionCount    = 0;
    }
}

static void get_prev_data(void)
{
    strcpy(msg, "AT+GET+ENROLLED_LIST\r\n");
    int *list = sendEnrolled(msg);
    if (!list) return;
    int i = 0;
    while (list[i] != -1)
        Record[list[i++]].isEnrolled = 1;
}

static void update_data(void)
{
    strcpy(msg, "AT+GET+ENROLLED_LIST\r\n");
    int *list = sendEnrolled(msg);
    if (!list) return;
    int i = 0;
    FILE *fp = fopen("Enrolled.csv", "w");
    if (!fp) return;
    while (list[i] != -1) {
        fprintf(fp, "%d", list[i++]);
        if (list[i] != -1) fprintf(fp, ",");
    }
    fclose(fp);
}

/* ─────────────────────────────────────────────
   Core attendance actions
───────────────────────────────────────────── */
static void do_identify(void)
{
    set_status("Waiting for finger...");
    log_msg("--- Place finger on sensor ---");

    int id = identifyFinger();

    if (id < 0 || id > 63) {
        set_status("Not Verified");
        log_msg("Result: Finger NOT recognised.");
        return;
    }
    if (!Record[id].isEnrolled) {
        char buf[80];
        snprintf(buf, sizeof(buf), "User %d is not enrolled.", id);
        set_status(buf);
        log_msg(buf);
        return;
    }

    char loginBuf[100], logoutBuf[100], line[256];
    time_t now = time(NULL);

    /* ── LOGOUT ── */
    if (Record[id].isIN) {
        if (!same_day(Record[id].loginTime, now)) {
            log_msg("Note: login was on a previous day — daily total reset.");
            Record[id].totalHoursToday = 0.0;
            Record[id].sessionCount    = 0;
        }
        double sessionHours = difftime(now, Record[id].loginTime) / 3600.0;
        Record[id].totalHoursToday += sessionHours;
        Record[id].sessionCount++;

        fmt_time(Record[id].loginTime, loginBuf,  sizeof(loginBuf));
        fmt_time(now,                  logoutBuf, sizeof(logoutBuf));

        snprintf(line, sizeof(line),
                 "LOGOUT  User %d  Session %d",
                 id, Record[id].sessionCount);
        set_status(line);

        snprintf(line, sizeof(line),
                 "User %d | Session %d LOGOUT\n"
                 "  Login   : %s\n  Logout  : %s\n"
                 "  Session : %.2f h  |  Total today: %.2f h",
                 id, Record[id].sessionCount,
                 loginBuf, logoutBuf,
                 sessionHours, Record[id].totalHoursToday);
        log_msg(line);

        FILE *fp = fopen("Biometric.csv", "a");
        if (fp) {
            fprintf(fp, "%d,%s,%s,%.2f,%d\n",
                    id, loginBuf, logoutBuf,
                    sessionHours, Record[id].sessionCount);
            fclose(fp);
        }
        Record[id].isIN = 0;
        return;
    }

    /* ── LOGIN ── */
    if (Record[id].sessionCount > 0 && !same_day(Record[id].loginTime, now)) {
        Record[id].totalHoursToday = 0.0;
        Record[id].sessionCount    = 0;
    }
    Record[id].loginTime = now;
    Record[id].isIN      = 1;

    fmt_time(now, loginBuf, sizeof(loginBuf));

    snprintf(line, sizeof(line),
             "LOGIN   User %d  Session %d",
             id, Record[id].sessionCount + 1);
    set_status(line);

    snprintf(line, sizeof(line),
             "User %d | Session %d LOGIN\n  Login : %s\n  Total so far: %.2f h",
             id, Record[id].sessionCount + 1,
             loginBuf, Record[id].totalHoursToday);
    log_msg(line);
}

static void do_enroll(int id)
{
    if (id < 0 || id > 63 || Record[id].isEnrolled) {
        log_msg("ERROR: Invalid ID or already enrolled.");
        set_status("Enroll failed.");
        return;
    }
    char line[80];
    snprintf(line, sizeof(line), "Enrolling User %d — place finger 3 times...", id);
    set_status(line);
    log_msg(line);

    enrollFinger(id);
    Record[id].isEnrolled = 1;

    snprintf(line, sizeof(line), "User %d enrolled successfully.", id);
    set_status(line);
    log_msg(line);
}

static void do_delete_user(int id)
{
    if (id < 0 || id > 63 || !Record[id].isEnrolled) {
        log_msg("ERROR: Invalid ID or not enrolled.");
        set_status("Delete failed.");
        return;
    }
    deleteUser(id);
    Record[id].isEnrolled = 0;

    char line[80];
    snprintf(line, sizeof(line), "User %d deleted.", id);
    set_status(line);
    log_msg(line);
}

static void do_end_day(int id)
{
    if (!Record[id].isEnrolled)      { log_msg("ERROR: User not enrolled.");            return; }
    if (Record[id].isIN)             { log_msg("ERROR: User still IN — logout first."); return; }
    if (Record[id].sessionCount == 0){ log_msg("ERROR: No sessions today.");            return; }

    double      total = Record[id].totalHoursToday;
    const char *st    = attendance_status(total);
    char        today[64], line[256];
    fmt_time(time(NULL), today, sizeof(today));

    FILE *fp = fopen("Biometric.csv", "a");
    if (fp) {
        fprintf(fp, "TOTAL_U%d,%d sessions,%s,%.2f,%s\n",
                id, Record[id].sessionCount, today, total, st);
        fclose(fp);
    }

    snprintf(line, sizeof(line),
             "=== End-of-Day  User %d ===\n"
             "  Sessions : %d\n  Total    : %.2f h\n  Status   : %s",
             id, Record[id].sessionCount, total, st);
    log_msg(line);
    set_status(st);

    Record[id].totalHoursToday = 0.0;
    Record[id].sessionCount    = 0;
}

static void do_reset_log(void)
{
    FILE *fp = fopen("Biometric.csv", "w");
    if (fp) {
        fprintf(fp, "User_id,Login_Time,Logout_Time,Session_Hours,Session_No\n");
        fclose(fp);
    }
    log_msg("Biometric.csv cleared and header written.");
    set_status("CSV reset.");
}

/* ─────────────────────────────────────────────
   Spin-box + OK dialog helper
   Returns the chosen integer, or -1 on cancel.
───────────────────────────────────────────── */
static int ask_user_id(GtkWindow *parent, const char *prompt)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        prompt, parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_OK",     GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label   = gtk_label_new(prompt);
    GtkWidget *spin    = gtk_spin_button_new_with_range(0, 63, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 0);

    gtk_container_add(GTK_CONTAINER(content), label);
    gtk_container_add(GTK_CONTAINER(content), spin);
    gtk_widget_show_all(dialog);

    int result = -1;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
        result = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));

    gtk_widget_destroy(dialog);
    return result;
}

/* ─────────────────────────────────────────────
   Button callbacks
───────────────────────────────────────────── */
static void on_identify(GtkButton *btn, gpointer win)
{
    (void)btn; (void)win;
    do_identify();
}

static void on_enroll(GtkButton *btn, gpointer win)
{
    (void)btn;
    int id = ask_user_id(GTK_WINDOW(win), "Enter User ID to Enroll (0–63):");
    if (id >= 0) do_enroll(id);
}

static void on_delete(GtkButton *btn, gpointer win)
{
    (void)btn;
    int id = ask_user_id(GTK_WINDOW(win), "Enter User ID to Delete (0–63):");
    if (id >= 0) do_delete_user(id);
}

static void on_end_day(GtkButton *btn, gpointer win)
{
    (void)btn;
    int id = ask_user_id(GTK_WINDOW(win), "Enter User ID for End-of-Day (0–63):");
    if (id >= 0) do_end_day(id);
}

static void on_reset_log(GtkButton *btn, gpointer win)
{
    (void)btn;

    /* Confirm before wiping */
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(win),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_YES_NO,
        "This will clear all records in Biometric.csv.\nAre you sure?");
    int resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (resp == GTK_RESPONSE_YES) do_reset_log();
}

static void on_clear_log(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    gtk_text_buffer_set_text(logBuffer, "", -1);
    log_msg("Log cleared.");
}

/* ─────────────────────────────────────────────
   Device-not-detected error window
───────────────────────────────────────────── */
static void show_device_error(void)
{
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE,
        "Device Not Found\n\n"
        "Could not open " DEVICE "\n\n"
        "Please check:\n"
        "  • USB cable is plugged in\n"
        "  • CP210x driver is loaded  (lsmod | grep cp210x)\n"
        "  • Permission on device      (ls -l /dev/ttyUSB0)\n"
        "    Run:  sudo chmod 666 /dev/ttyUSB0\n\n"
        "The application will now close.");

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* ─────────────────────────────────────────────
   Build the main window
───────────────────────────────────────────── */
static GtkWidget *build_window(void)
{
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "A600 Attendance System");
    gtk_window_set_default_size(GTK_WINDOW(window), 120, 20);
    gtk_container_set_border_width(GTK_CONTAINER(window), 8);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* Root layout */
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(window), root);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>A600 Fingerprint Attendance</span>");
    gtk_box_pack_start(GTK_BOX(root), title, FALSE, FALSE, 0);

    /* Status */
    statusLabel = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(statusLabel), 0.0);
    gtk_box_pack_start(GTK_BOX(root), statusLabel, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 2);

    /* ===== BUTTON GRID AT TOP ===== */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

    #define ADD_BTN(label, col, row, cb) \
    { \
        GtkWidget *_b = gtk_button_new_with_label(label); \
        gtk_widget_set_size_request(_b, 140, 40); \
        gtk_grid_attach(GTK_GRID(grid), _b, col, row, 1, 1); \
        g_signal_connect(_b, "clicked", G_CALLBACK(cb), window); \
    }

    ADD_BTN("Place Finger", 0, 0, on_identify);
    ADD_BTN("Enroll Finger", 1, 0, on_enroll);
    ADD_BTN("Remove Finger", 2, 0, on_delete);

    ADD_BTN("End Day", 0, 1, on_end_day);
    ADD_BTN("Reset CSV", 1, 1, on_reset_log);
    ADD_BTN("Clear Log", 2, 1, on_clear_log);

    #undef ADD_BTN

    gtk_box_pack_start(GTK_BOX(root), grid, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 2);

    /* ===== LOG BELOW ===== */
    GtkWidget *logLabel = gtk_label_new("Activity Log:");
    gtk_label_set_xalign(GTK_LABEL(logLabel), 0.0);
    gtk_box_pack_start(GTK_BOX(root), logLabel, FALSE, FALSE, 0);

    logBuffer = gtk_text_buffer_new(NULL);
    logView = gtk_text_view_new_with_buffer(logBuffer);

    gtk_text_view_set_editable(GTK_TEXT_VIEW(logView), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logView),
                                GTK_WRAP_WORD_CHAR);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);

    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);

    gtk_container_add(GTK_CONTAINER(scroll), logView);

    /* Log takes remaining space */
    gtk_box_pack_start(GTK_BOX(root), scroll, TRUE, TRUE, 0);

    return window;
}

/* ─────────────────────────────────────────────
   main
───────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    /* ── Try to open the UART device ── */
    if (startAndConfigUart(DEVICE) == -1) {
        show_device_error();
        return 1;
    }

    /* ── Initialise attendance records ── */
    set_default();
    verbose = 0;
    get_prev_data();

    /* Reset CSV header if file does not exist */
    FILE *fp = fopen("Biometric.csv", "r");
    if (!fp) {
        fp = fopen("Biometric.csv", "w");
        if (fp) {
            fprintf(fp, "User_id,Login_Time,Logout_Time,Session_Hours,Session_No\n");
            fclose(fp);
        }
    } else {
        fclose(fp);
    }

    /* ── Build and show window ── */
    GtkWidget *window = build_window();
    gtk_widget_show_all(window);

    /* Write a welcome line into the log */
    log_msg("System started. Device: " DEVICE);
    log_msg("Enrolled IDs loaded from sensor.");
    set_status("Ready — place a finger or choose an action.");

    gtk_main();

    /* ── Cleanup ── */
    update_data();
    close(port);
    return 0;
}
