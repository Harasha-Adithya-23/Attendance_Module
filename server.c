/*
 * server.c  —  A600 Attendance HTTP Server
 *
 * Hosts a minimal HTTP/1.1 server on port 8080.
 * The browser (laptop) calls REST endpoints; this process
 * drives the A600 fingerprint sensor over UART.
 *
 * Endpoints
 * ---------
 *  POST /identify          — place finger (login / logout)
 *  POST /enroll   ?id=N    — enroll user N
 *  POST /delete   ?id=N    — delete user N
 *  POST /endday   ?id=N    — end-of-day summary for user N
 *  POST /reset             — wipe Biometric.csv
 *  GET  /log               — return Biometric.csv contents as JSON
 *  GET  /status            — enrolled list as JSON
 *  GET  /                  — serve index.html
 *  GET  /app.js            — serve app.js
 *
 * Build (on the board):
 *   gcc -O2 -Wall -o attendance_server server.c -lm
 *
 * Run:
 *   ./attendance_server
 */

/* ── includes ─────────────────────────────────────────────────────────────── */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <math.h>
#include "A600.h"
#include "Attendance.h"

/* Network */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

/* ── config ───────────────────────────────────────────────────────────────── */
#define HTTP_PORT        8080
#define DEVICE           "/dev/ttyUSB0"
#define BAUD             B115200
#define PROBE_TIMEOUT_S  15
#define MAX_USERS        64
#define CSV_FILE         "Biometric.csv"
#define ENROLLED_FILE    "Enrolled.csv"

/* ── globals (UART, shared with A600 logic) ───────────────────────────────── */
static int           port_fd = -1;


/* ── attendance record ────────────────────────────────────────────────────── */
typedef struct {
    int    user_id;
    time_t loginTime;
    double totalHoursToday;
    int    sessionCount;
    int    isEnrolled;
    int    isIN;
} Attendance;


/* ══════════════════════════════════════════════════════════════════════════
 *  SECTION 1 — UART / A600 driver  (ported from A600.c)
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── identify handler — writes result into out_buf ──────────────────────── */
static void do_identify(char *out_buf, int out_size)
{
    int id = identifyFinger();

    if (id < 0 || id >= MAX_USERS) {
        snprintf(out_buf, out_size,
                 "{\"ok\":false,\"msg\":\"Finger not recognised\"}");
        return;
    }
    if (!Record[id].isEnrolled) {
        snprintf(out_buf, out_size,
                 "{\"ok\":false,\"msg\":\"User %d is not enrolled\"}",  id);
        return;
    }

    time_t now = time(NULL);
    char loginBuf[64], logoutBuf[64];

    /* ── LOGOUT ── */
    if (Record[id].isIN) {
        if (!sameDay(Record[id].loginTime, now)) {
            Record[id].totalHoursToday = 0.0;
            Record[id].sessionCount    = 0;
        }
        double sessionHours = difftime(now, Record[id].loginTime) / 3600.0;
        Record[id].totalHoursToday += sessionHours;
        Record[id].sessionCount++;

        fmtTime(Record[id].loginTime, loginBuf,  sizeof(loginBuf));
        fmtTime(now,                  logoutBuf, sizeof(logoutBuf));

        FILE *fp = fopen(CSV_FILE, "a");
        if (fp) {
            fprintf(fp, "%d,%s,%s,%.2f,%d\n",
                    id, loginBuf, logoutBuf,
                    sessionHours, Record[id].sessionCount);
            fclose(fp);
        }
        Record[id].isIN = 0;

        snprintf(out_buf, out_size,
                 "{\"ok\":true,\"action\":\"logout\","
                 "\"user_id\":%d,\"session\":%d,"
                 "\"login\":\"%s\",\"logout\":\"%s\","
                 "\"session_hours\":%.2f,\"total_today\":%.2f}",
                 id, Record[id].sessionCount,
                 loginBuf, logoutBuf,
                 sessionHours, Record[id].totalHoursToday);
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

    snprintf(out_buf, out_size,
             "{\"ok\":true,\"action\":\"login\","
             "\"user_id\":%d,\"session\":%d,"
             "\"login\":\"%s\",\"total_so_far\":%.2f}",
             id, Record[id].sessionCount + 1,
             loginBuf, Record[id].totalHoursToday);
}

/* ── enroll handler ───────────────────────────────────────────────────────── */
static void do_enroll(int id, char *out_buf, int out_size)
{
    if (id < 0 || id >= MAX_USERS) {
        snprintf(out_buf, out_size,
                 "{\"ok\":false,\"msg\":\"ID %d out of range (0-63)\"}",  id);
        return;
    }
    if (Record[id].isEnrolled) {
        snprintf(out_buf, out_size,
                 "{\"ok\":false,\"msg\":\"User %d already enrolled\"}",  id);
        return;
    }
    
       int user_id = id;
    
        unsigned char buf[16];
        char cmd[64];
    
        probeJPEG();
        
    
        snprintf(out_buf, out_size,"Enrolling user ID %d\n", user_id);
        snprintf(out_buf,out_size,"You will need to press your finger 3 times\n");
        snprintf(out_buf,out_size,"----------------------------------------------\n");
    
        for (int press = 1; press <= 3; press++) {
        	snprintf(cmd, sizeof(cmd), "AT+ENROLL+50+%d+%d\r\n",press, user_id);
            printf("\n[%d/3] Place finger on sensor...\n", press);
            sleep(1);
    
            tcflush(port, TCIFLUSH);
            write(port, cmd, strlen(cmd));
            snprintf(out_buf,out_size,"Sent: %s", cmd);
    
            int n = 0;
            for (int attempt = 0; attempt < PROBE_TIMEOUT_S; attempt++) {
                sleep(1);
                n = read(port, buf, sizeof(buf));
                if (n > 0) break;
                snprintf(out_buf,out_size,"Waiting... (%d/%d)\n", attempt + 1, PROBE_TIMEOUT_S);
            }
    
            if (n <= 0) {
                snprintf(out_buf,out_size,"No response on press %d — enroll failed\n", press);
                return;
            }
    
            snprintf(out_buf,out_size,"RAW (%d bytes):", n);
            for (int i = 0; i < n; i++)
                printf(" %02X", buf[i]);
            snprintf(out_buf,out_size,"\n");
    
            unsigned char resp = buf[0];
            switch (resp) {
                case 0x00:
                    snprintf(out_buf,out_size,"[%d/3] Fingerprint captured successfully\n", press);
                    break;
                case 0x01:
                    snprintf(out_buf,out_size,"[%d/3] Communication error (0x01) — retrying\n", press);
                    press--;
                    break;
                case 0x1C:
                    snprintf(out_buf,out_size,"[%d/3] Timeout — finger not detected (0x1C)\n", press);
                    printf(out_buf,out_size,"Enroll aborted — restart and try again\n");
                    return;
                case 0x06:
                    snprintf(out_buf,out_size,"[%d/3] Image too messy (0x06) — clean finger and retry\n", press);
                    press--;
                    break;
                default:
                    snprintf(out_buf,out_size,"[%d/3] Error response: 0x%02X — enroll failed\n", press, resp);
                    return;
            }
    
            if (press < 3) {
                snprintf(out_buf,out_size,"Lift finger now...\n");
                sleep(2);
            }
        }
    
        snprintf(out_buf,out_size,"\n----------------------------------------------\n");
        snprintf(out_buf,out_size,"SUCCESS — User ID %d enrolled!\n", user_id);
  }
    

/* ── delete handler ───────────────────────────────────────────────────────── */
static void do_delete(int id, char *out_buf, int out_size)
{
    if (id < 0 || id >= MAX_USERS || !Record[id].isEnrolled) {
        snprintf(out_buf, out_size,
                 "{\"ok\":false,\"msg\":\"User %d not enrolled or invalid\"}",  id);
        return;
    }
    deleteUser(id);
    Record[id].isEnrolled = 0;
    Record[id].isIN       = 0;
    snprintf(out_buf, out_size,
             "{\"ok\":true,\"msg\":\"User %d deleted\"}",  id);
}

/* ── end-day handler ──────────────────────────────────────────────────────── */
static void do_endday(int id, char *out_buf, int out_size)
{
    if (id < 0 || id >= MAX_USERS || !Record[id].isEnrolled) {
        snprintf(out_buf, out_size,
                 "{\"ok\":false,\"msg\":\"User %d not enrolled or invalid\"}",  id);
        return;
    }
    if (Record[id].isIN) {
        snprintf(out_buf, out_size,
                 "{\"ok\":false,\"msg\":\"User %d is still checked in — logout first\"}",  id);
        return;
    }
    if (Record[id].sessionCount == 0) {
        snprintf(out_buf, out_size,
                 "{\"ok\":false,\"msg\":\"No sessions today for user %d\"}",  id);
        return;
    }

    double      total = Record[id].totalHoursToday;
    const char *st    = getAttendanceStatus(total);
    char today[64];
    fmtTime(time(NULL), today, sizeof(today));

    FILE *fp = fopen(CSV_FILE, "a");
    if (fp) {
        fprintf(fp, "TOTAL_U%d,%d sessions,%s,%.2f,%s\n",
                id, Record[id].sessionCount, today, total, st);
        fclose(fp);
    }

    snprintf(out_buf, out_size,
             "{\"ok\":true,\"user_id\":%d,\"sessions\":%d,"
             "\"total_hours\":%.2f,\"status\":\"%s\",\"date\":\"%s\"}",
             id, Record[id].sessionCount, total, st, today);

    Record[id].totalHoursToday = 0.0;
    Record[id].sessionCount    = 0;
}

/* ── reset handler ────────────────────────────────────────────────────────── */
static void do_reset(char *out_buf, int out_size)
{
    FILE *fp = fopen(CSV_FILE, "w");
    if (fp) {
        fprintf(fp, "User_id,Login_Time,Logout_Time,Session_Hours,Session_No\n");
        fclose(fp);
    }
    snprintf(out_buf, out_size,
             "{\"ok\":true,\"msg\":\"" CSV_FILE " has been reset\"}");
}

/* ── log handler — read CSV and return as JSON lines ─────────────────────── */
static void do_log(char *out_buf, int out_size)
{
    FILE *fp = fopen(CSV_FILE, "r");
    if (!fp) {
        snprintf(out_buf, out_size, "{\"ok\":false,\"msg\":\"Cannot open " CSV_FILE "\"}");
        return;
    }

    /* Build a JSON array of strings, one per CSV line */
    char line[256];
    int  pos  = 0;
    int  first = 1;

    pos += snprintf(out_buf + pos, out_size - pos, "{\"ok\":true,\"lines\":[");

    while (fgets(line, sizeof(line), fp) && pos < out_size - 64) {
        /* strip newline */
        line[strcspn(line, "\r\n")] = '\0';

        /* JSON-escape backslash and double-quote */
        char escaped[512];
        int  ei = 0;
        for (int li = 0; line[li] && ei < (int)sizeof(escaped) - 2; li++) {
            if (line[li] == '"' || line[li] == '\\')
                escaped[ei++] = '\\';
            escaped[ei++] = line[li];
        }
        escaped[ei] = '\0';

        pos += snprintf(out_buf + pos, out_size - pos,
                        "%s\"%s\"", first ? "" : ",", escaped);
        first = 0;
    }
    fclose(fp);
    pos += snprintf(out_buf + pos, out_size - pos, "]}");
}

/* ── status handler — return enrolled list ───────────────────────────────── */
static void do_status(char *out_buf, int out_size)
{
    int pos   = 0;
    int first = 1;
    pos += snprintf(out_buf + pos, out_size - pos, "{\"ok\":true,\"enrolled\":[");
    for (int i = 0; i < MAX_USERS; i++) {
        if (Record[i].isEnrolled) {
            pos += snprintf(out_buf + pos, out_size - pos,
                            "%s{\"id\":%d,\"in\":%s,\"total\":%.2f,\"sessions\":%d}",
                            first ? "" : ",",
                            i,
                            Record[i].isIN ? "true" : "false",
                            Record[i].totalHoursToday,
                            Record[i].sessionCount);
            first = 0;
        }
    }
    pos += snprintf(out_buf + pos, out_size - pos, "]}");
}

/* ══════════════════════════════════════════════════════════════════════════
 *  SECTION 3 — HTTP server
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── serve a static file ──────────────────────────────────────────────────── */
static void serve_file(int client, const char *filename, const char *mime)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        const char *err =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 9\r\n\r\nNot Found";
        write(client, err, strlen(err));
        return;
    }

    /* Read whole file into memory */
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);
    char *fbuf = malloc(fsize + 1);
    if (!fbuf) { fclose(fp); return; }
    fread(fbuf, 1, fsize, fp);
    fclose(fp);

    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Connection: close\r\n\r\n",
             mime, fsize);
    write(client, header, strlen(header));
    write(client, fbuf, fsize);
    free(fbuf);
}

/* ── send a JSON response ─────────────────────────────────────────────────── */
static void send_json(int client, const char *json)
{
    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Connection: close\r\n\r\n",
             strlen(json));
    write(client, header, strlen(header));
    write(client, json, strlen(json));
}

/* ── parse ?id=N from a query string ─────────────────────────────────────── */
static int parse_id(const char *qs)
{
    if (!qs) return -1;
    const char *p = strstr(qs, "id=");
    if (!p) return -1;
    return atoi(p + 3);
}

/* ── handle one HTTP request ──────────────────────────────────────────────── */
static void handle_request(int client)
{
    char req[2048];
    memset(req, 0, sizeof(req));
    int n = read(client, req, sizeof(req) - 1);
    if (n <= 0) return;

    /* ── parse first line: METHOD /path?query HTTP/x.x ─────────────────── */
    char method[8], path[256], query[256];
    method[0] = path[0] = query[0] = '\0';

    char *line_end = strstr(req, "\r\n");
    if (!line_end) return;
    char first_line[512];
    int ll = (int)(line_end - req);
    if (ll >= (int)sizeof(first_line)) return;
    memcpy(first_line, req, ll);
    first_line[ll] = '\0';

    /* split on spaces */
    char *tok = strtok(first_line, " ");
    if (tok) strncpy(method, tok, sizeof(method) - 1);
    tok = strtok(NULL, " ");
    if (tok) {
        char *qm = strchr(tok, '?');
        if (qm) {
            strncpy(query, qm + 1, sizeof(query) - 1);
            *qm = '\0';
        }
        strncpy(path, tok, sizeof(path) - 1);
    }

    /* ── handle OPTIONS (CORS preflight) ────────────────────────────────── */
    if (strcmp(method, "OPTIONS") == 0) {
        const char *cors =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\n\r\n";
        write(client, cors, strlen(cors));
        return;
    }

    /* ── static files ────────────────────────────────────────────────────── */
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        serve_file(client, "index.html", "text/html");
        return;
    }
    if (strcmp(path, "/app.js") == 0) {
        serve_file(client, "app.js", "application/javascript");
        return;
    }

    /* ── API endpoints ───────────────────────────────────────────────────── */
    char json_out[4096];
    json_out[0] = '\0';

    if (strcmp(path, "/identify") == 0 && strcmp(method, "POST") == 0) {
        do_identify(json_out, sizeof(json_out));

    } else if (strcmp(path, "/enroll") == 0 && strcmp(method, "POST") == 0) {
        int id = parse_id(query);
        do_enroll(id, json_out, sizeof(json_out));

    } else if (strcmp(path, "/delete") == 0 && strcmp(method, "POST") == 0) {
        int id = parse_id(query);
        do_delete(id, json_out, sizeof(json_out));

    } else if (strcmp(path, "/endday") == 0 && strcmp(method, "POST") == 0) {
        int id = parse_id(query);
        do_endday(id, json_out, sizeof(json_out));

    } else if (strcmp(path, "/reset") == 0 && strcmp(method, "POST") == 0) {
        do_reset(json_out, sizeof(json_out));

    } else if (strcmp(path, "/log") == 0 && strcmp(method, "GET") == 0) {
        do_log(json_out, sizeof(json_out));

    } else if (strcmp(path, "/status") == 0 && strcmp(method, "GET") == 0) {
        do_status(json_out, sizeof(json_out));

    } else {
        const char *err =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 27\r\n\r\n"
            "{\"ok\":false,\"msg\":\"Not found\"}";
        write(client, err, strlen(err));
        return;
    }

    send_json(client, json_out);
}

/* ── main server loop ─────────────────────────────────────────────────────── */
int main(void)
{
    /* 1. UART init */
    if (startAndConfigUart(DEVICE) != 0) {
        fprintf(stderr, "Failed to open UART — is %s available?\n", DEVICE);
        return 1;
    }

    /* 2. Attendance init */
    setDefault();
    getPrevData();

    /* Ensure CSV has a header if it doesn't exist yet */
    {
        FILE *fp = fopen(CSV_FILE, "r");
        if (!fp) {
            fp = fopen(CSV_FILE, "w");
            if (fp) {
                fprintf(fp, "User_id,Login_Time,Logout_Time,Session_Hours,Session_No\n");
                fclose(fp);
            }
        } else {
            fclose(fp);
        }
    }

    /* 3. TCP socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;   /* listen on all interfaces */
    addr.sin_port        = htons(HTTP_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    listen(server_fd, 8);

    fprintf(stderr, "\n=== A600 Attendance HTTP Server ===\n");
    fprintf(stderr, "Listening on http://0.0.0.0:%d\n", HTTP_PORT);
    fprintf(stderr, "Open http://<board-ip>:%d on your laptop\n\n", HTTP_PORT);

    /* 4. Accept loop (single-threaded — fingerprint ops block anyway) */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(server_fd,
                            (struct sockaddr *)&client_addr, &client_len);
        if (client < 0) {
            perror("accept");
            continue;
        }
        fprintf(stderr, "Connection from %s\n", inet_ntoa(client_addr.sin_addr));
        handle_request(client);
        close(client);
    }

    /* cleanup (unreachable, but good practice) */
    updateData();
    close(server_fd);
    close(port_fd);
    return 0;
}
