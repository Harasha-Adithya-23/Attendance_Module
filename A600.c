#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdlib.h>
#include <time.h>
#include "A600.h"

#define DEVICE          "/dev/ttyUSB0"
#define BAUD            B115200
#define PACKET_SIZE     36864
#define MAX_PACKETS     256
#define PROBE_TIMEOUT_S 15
#define PKT_TIMEOUT_S   10

int port;
unsigned char msg[255];

static unsigned long g_jpeg_length = 0;


int verbose = 1;

//  UART initialisation: 115200 8N1, no flow control               

int startAndConfigUart(const char *device)
{
    port = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (port < 0) {
        printf("Error %i from open: %s\n", errno, strerror(errno));
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(port, &tty) != 0) {
        printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
        close(port);
        return -1;
    }

    cfsetispeed(&tty, BAUD);
    cfsetospeed(&tty, BAUD);
    cfmakeraw(&tty);


    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |=  CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |=  CREAD | CLOCAL;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_lflag  = 0;
    tty.c_oflag  = 0;
    tty.c_cc[VTIME] = 20;
    tty.c_cc[VMIN]  = 0;

    if (tcsetattr(port, TCSANOW, &tty) != 0) {
        printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
        return -1;
    }

    printf("UART initialized successfully\n");
    return 0;
}

//  Read exactly 'need' bytes with a wall-clock timeout   


static int read_bytes(unsigned char *buf, int want, int timeout_sec)
{
    int    total    = 0;
    struct timespec deadline, last_rx, now;

    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout_sec;
    clock_gettime(CLOCK_MONOTONIC, &last_rx);

    while (total < want) {
        clock_gettime(CLOCK_MONOTONIC, &now);

        /* Absolute deadline */
        if (now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec &&
             now.tv_nsec >= deadline.tv_nsec)) {
            printf("Timeout: wanted %d bytes, got %d\n", want, total);
            break;
        }

        /* Inactivity gap: if we already have data and nothing arrived
           for 200 ms, the module has finished sending this packet.    */
        if (total > 0) {
            double idle = (now.tv_sec  - last_rx.tv_sec) +
                          (now.tv_nsec - last_rx.tv_nsec) / 1e9;
            if (idle >= 0.20)
                break;
        }
        int n = read(port, buf + total, want - total);
        if (n > 0) {
            total += n;
            clock_gettime(CLOCK_MONOTONIC, &last_rx);
        } else {
            usleep(2000); /* 2 ms poll — keeps CPU sane */
        }
    }
    return total;
}







int read_exact(unsigned char *buf, int need, int timeout_sec)
{
    int    total = 0;
    time_t start = time(NULL);

    while (total < need) {
        if ((int)(time(NULL) - start) >= timeout_sec) {
            printf("Timeout: wanted %d bytes, got %d\n", need, total);
            return total;
        }
        int n = read(port, buf + total, need - total);
        if (n < 0) {
            printf("Read error: %s\n", strerror(errno));
            return total;
        }
        total += n;
    }
    return total;
}

/*  Generic receive – hex dump + printable ASCII                       */
int rcv_msg_raw(unsigned char *buf, int buf_size)
{
    memset(buf, 0, buf_size);
    int total = 0;
    struct timespec last_rx, now;
    clock_gettime(CLOCK_MONOTONIC, &last_rx);

    while (total < buf_size - 1) {
        int n = read(port, buf + total, buf_size - 1 - total);
        if (n > 0) {
            total += n;
            clock_gettime(CLOCK_MONOTONIC, &last_rx);
        } else {
            usleep(2000);
            clock_gettime(CLOCK_MONOTONIC, &now);
            double idle = (now.tv_sec - last_rx.tv_sec) +
                          (now.tv_nsec - last_rx.tv_nsec) / 1e9;
            if (total > 0 && idle >= 0.15)  // 150ms gap = done
                break;
            // If nothing received yet, wait up to 2s
            if (total == 0) {
                double waited = idle;
                if (waited > 2.0) break;
            }
        }
    }
	if(verbose){
	    printf("HEX (%d bytes):\n", total);
	    for (int i = 0; i < total; i++)
	        printf("%02X ", buf[i]);
	    printf("\n");

	    printf("Text: ");
	    for (int i = 0; i < total; i++)
	        if (buf[i] >= 32 && buf[i] <= 126)
	            printf("%c", buf[i]);
	    printf("\n");
	}
    return total;
}

void rcv_msg(void)
{
    unsigned char buf[256];
    rcv_msg_raw(buf, sizeof(buf));
}

int* sendEnrolled(unsigned char* mesg)
{
	tcflush(port,TCIFLUSH);
    int bw = write(port, mesg, strlen((char *)mesg)); // Fix #1: mesg not msg
    if (bw < 0) {
        printf("Write error: %s\n", strerror(errno));
        return NULL;
    }
    printf("Sent: %s", mesg);
    sleep(1);

    unsigned char buf[256];
    int buf_size = 256;
    memset(buf, 0, buf_size);
    int total = 0;
    struct timespec last_rx, now;
    clock_gettime(CLOCK_MONOTONIC, &last_rx);

    while (total < buf_size - 1) {
        int n = read(port, buf + total, buf_size - 1 - total);
        if (n > 0) {
            total += n;
            clock_gettime(CLOCK_MONOTONIC, &last_rx);
        } else {
            usleep(2000);
            clock_gettime(CLOCK_MONOTONIC, &now);
            double idle = (now.tv_sec - last_rx.tv_sec) +
                          (now.tv_nsec - last_rx.tv_nsec) / 1e9;
            if (total > 0 && idle >= 0.15)
                break;
            if (total == 0) {
                if (idle > 2.0) break;
            }
        }
    } // Fix #4: while loop properly closed here
	
	if(verbose){
	    printf("HEX (%d bytes):\n", total);
	    for (int i = 0; i < total; i++)
	        printf("%02X ", buf[i]);
	    printf("\n");

	    printf("\nDevices List:\n");
	    for (int i = 1; i < total; i++) // buf[0] assumed to be header
	    {
	        unsigned char byte = buf[i];
	        for (int j = 0; j < 8; j++) // Fix #2: j++ not i++
	        {
	            if (byte & (1 << j)) // Fix #3: test bit j in byte value
	            {
	                printf("ID: %d\n", ((i-1) * 8 + (j)));
	            }
	        }
	    }
	}

		static int list[64];
		int n = 0;
		    for (int i = 1; i < total; i++) // buf[0] assumed to be header
		    {
		        unsigned char byte = buf[i];
		        for (int j = 0; j < 8; j++) // Fix #2: j++ not i++
		        {
		            if (byte & (1 << j)) // Fix #3: test bit j in byte value
		            {
		               list[n++] =  ((i-1) * 8 + (j));
		            }
		        }
		    }
		    list[n] = -1;	    
		return list;
}

//  Generic send + receive                   


void send_cmd(unsigned char *mesg)
{
    int bw = write(port, mesg, strlen((char *)mesg));
    if (bw < 0) {
        printf("Write error: %s\n", strerror(errno));
        return ;
    }
    printf("Sent: %s", mesg);
    rcv_msg();
    sleep(1);
}

// ------------------------------------------------------------------ 


// UART initialization above





void probeJPEG(void)
{
    unsigned char buf[16];

    /* P1=0  → wait-for-finger mode
       P2=50 → 50 × 200ms = 10s finger timeout
       P3=1  → 1152 bytes per packet                */

    /* Flush any stale bytes in RX buffer */
  	 tcflush(port, TCIFLUSH);
  	 
	char cmd[] = "AT+PROBE+JPEGFINGER+0+50+6\r\n";
    write(port, cmd, strlen(cmd));
    printf("Sent: %s", cmd);
    printf("Place your finger on the sensor...\n"); 
    

    /* Poll for 4-byte response */
    int n = 0;
    for (int attempt = 0; attempt < PROBE_TIMEOUT_S; attempt++)
    {
        sleep(1);
        n = read(port, buf, sizeof(buf));
        if (n > 0) {
            printf("Got response after ~%d second(s)\n", attempt + 1);
            break;
        }
        printf("Waiting... (%d/%d)\n", attempt + 1, PROBE_TIMEOUT_S);
    }

    if (n == 0) {
        printf("No response — finger not detected or module offline\n");
        return;
    }

    /* Dump raw bytes */
    printf("HEX (%d bytes):", n);
    for (int i = 0; i < n; i++)
        printf(" %02X", buf[i]);
    printf("\n");

    if (n < 4) {
        printf("Response too short: %d bytes (need 4)\n", n);
        return;
    }

    /* Check response code */
    if (buf[0] == 0x02) {
        printf("No finger detected (0x02) — try again\n");
        return;
    }
    if (buf[0] != 0x00) {
        printf("Error response: 0x%02X\n", buf[0]);
        return;
    }

    /* Little-endian 24-bit length: buf[1]=LSB, buf[2]=MID, buf[3]=MSB */
    g_jpeg_length = ((unsigned long)buf[3] << 16)
                  | ((unsigned long)buf[2] <<  8)
                  |  (unsigned long)buf[1];

	
    unsigned long full_pkts = g_jpeg_length / PACKET_SIZE;
    unsigned long remainder = g_jpeg_length % PACKET_SIZE;

    printf("JPEG size : %lu bytes (%.1f KB)\n",
           g_jpeg_length, g_jpeg_length / 1024.0);
    printf("Packets   : %lu × %d bytes", full_pkts, PACKET_SIZE);
    if (remainder)
        printf(" + 1 partial of %lu bytes", remainder);
    printf("\n");

    if (g_jpeg_length == 0 || g_jpeg_length > 150000UL) {
        printf("WARNING: suspicious size — resetting\n");
        g_jpeg_length = 0;
        return;
    }

    printf("Ready — run option 2 to download\n"); 
}



void getJPEG(void)
{
    if (g_jpeg_length == 0) {
        printf("Run probeJPEG first\n");
        return;
    }


    FILE *fp = fopen("fingerprint.jpeg", "wb");
    if (!fp) { perror("fopen"); return; }

    unsigned long total      = 0;
    int           pkt        = 0;
    unsigned long total_pkts = (g_jpeg_length + PACKET_SIZE - 1) / PACKET_SIZE;

    printf("\nDownloading JPEG — %lu bytes in %lu packets\n",
           g_jpeg_length, total_pkts);

    while (total < g_jpeg_length && pkt < MAX_PACKETS)
    {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "AT+GET+IMAGE+%d\r\n", pkt);
        printf("[TX] %s", cmd);

        write(port, cmd, strlen(cmd));

        /* How many bytes this packet */
        unsigned long remain   = g_jpeg_length - total;
        int           expected = (remain >= PACKET_SIZE)
                                 ? PACKET_SIZE
                                 : (int)remain;

        /* Read raw payload — NO response byte, module sends data directly */
        unsigned char rx[PACKET_SIZE];
        int n = read_exact(rx, expected, PKT_TIMEOUT_S);

        if (n <= 0) {
            printf("Timeout on pkt %d\n", pkt);
            break;
        }

        /* Dump first 4 bytes for debugging */
        printf("Pkt %3d | first bytes: ", pkt);
        for (int i = 0; i < (n < 4 ? n : 4); i++)
            printf("%02X ", rx[i]);

        /* Verify JPEG SOI on first packet */
        if (pkt == 0) {
            if (n >= 2 && rx[0] == 0xFF && rx[1] == 0xD8)
                printf("| JPEG SOI OK ");
            else
                printf("| WARNING: expected FF D8, got %02X %02X ",
                       rx[0], n > 1 ? rx[1] : 0x00);
        }

        fwrite(rx, 1, n, fp);
        total += n;

        int pct = (int)((100UL * total) / g_jpeg_length);
        printf("| %4d bytes | %3d%% (%lu/%lu)\n",
               n, pct, total, g_jpeg_length);

        pkt++;
        usleep(50000); /* 50ms between packets */
    }

    fclose(fp);
    printf("\n---------------------------------\n");
    printf("Saved %lu / %lu bytes\n", total, g_jpeg_length);

    if (total == g_jpeg_length)
        printf("SUCCESS → fingerprint.jpeg\n");
    else
        printf("INCOMPLETE — expected %lu, got %lu\n",
               g_jpeg_length, total);


    system("open fingerprint.jpeg");

}



// probe and get functions above



/*  enrollFinger                                                        
	AT+ENROLL+P1+P2+P3                                                 
 	P1 = timeout (units of 200 ms), P2 = presses (3), P3 = user ID    */

void enrollFinger(int user_id)
{
    if (user_id < 0 || user_id > 63) {
        printf("Invalid user ID — must be 0-63\n");
        return;
    }

    unsigned char buf[16];
    char cmd[64];

    probeJPEG();
    

    printf("Enrolling user ID %d\n", user_id);
    printf("You will need to press your finger 3 times\n");
    printf("----------------------------------------------\n");

    for (int press = 1; press <= 3; press++) {
    	snprintf(cmd, sizeof(cmd), "AT+ENROLL+50+%d+%d\r\n",press, user_id);
        printf("\n[%d/3] Place finger on sensor...\n", press);
        sleep(1);

        tcflush(port, TCIFLUSH);
        write(port, cmd, strlen(cmd));
        printf("Sent: %s", cmd);

        int n = 0;
        for (int attempt = 0; attempt < PROBE_TIMEOUT_S; attempt++) {
            sleep(1);
            n = read(port, buf, sizeof(buf));
            if (n > 0) break;
            printf("Waiting... (%d/%d)\n", attempt + 1, PROBE_TIMEOUT_S);
        }

        if (n <= 0) {
            printf("No response on press %d — enroll failed\n", press);
            return;
        }

        printf("RAW (%d bytes):", n);
        for (int i = 0; i < n; i++)
            printf(" %02X", buf[i]);
        printf("\n");

        unsigned char resp = buf[0];
        switch (resp) {
            case 0x00:
                printf("[%d/3] Fingerprint captured successfully\n", press);
                break;
            case 0x01:
                printf("[%d/3] Communication error (0x01) — retrying\n", press);
                press--;
                break;
            case 0x1C:
                printf("[%d/3] Timeout — finger not detected (0x1C)\n", press);
                printf("Enroll aborted — restart and try again\n");
                return;
            case 0x06:
                printf("[%d/3] Image too messy (0x06) — clean finger and retry\n", press);
                press--;
                break;
            default:
                printf("[%d/3] Error response: 0x%02X — enroll failed\n", press, resp);
                return;
        }

        if (press < 3) {
            printf("Lift finger now...\n");
            sleep(2);
        }
    }

    printf("\n----------------------------------------------\n");
    printf("SUCCESS — User ID %d enrolled!\n", user_id);
}

/* ------------------------------------------------------------------ */
/*  verifyFinger                                                        */
/*  AT+VERIFY+P1+P2                                                    */
/*  Response: [RESP(1)][SCORE_HI(1)][SCORE_LO(1)]                     */
/*  0x00 = match, 0x08 = no match                                      */
/* ------------------------------------------------------------------ */

void verifyFinger(int user_id)
{
    if (user_id < 0 || user_id > 63) {
        printf("Invalid user ID — must be 0-63\n");
        return;
    }

    unsigned char buf[16];
    char cmd[64];

    snprintf(cmd, sizeof(cmd), "AT+VERIFY+50+%d\r\n", user_id);

    printf("Verifying against user ID %d\n", user_id);
    printf("Place finger on sensor...\n");

    tcflush(port, TCIFLUSH);
    write(port, cmd, strlen(cmd));
    printf("Sent: %s", cmd);

    int n = 0;
    for (int attempt = 0; attempt < PROBE_TIMEOUT_S; attempt++) {
        sleep(1);
        n = read(port, buf, sizeof(buf));
        if (n > 0) break;
        printf("Waiting... (%d/%d)\n", attempt + 1, PROBE_TIMEOUT_S);
    }

    if (n <= 0) {
        printf("No response — finger not detected or module offline\n");
        return;
    }

    printf("RAW (%d bytes):", n);
    for (int i = 0; i < n; i++)
        printf(" %02X", buf[i]);
    printf("\n");

    unsigned char resp = buf[0];
    switch (resp) {
        case 0x00: {
            int score = 0;
            if (n >= 3)      score = ((int)buf[1] << 8) | buf[2];
            else if (n >= 2) score = buf[1];
            printf("\nUser ID %d verified!\n", user_id);
            printf("  Comparison score: %d\n", score);
            break;
        }
        case 0x08:
            printf("\nFinger does not match user ID %d\n", user_id);
            break;
        case 0x01:
            printf("Communication error (0x01)\n");
            break;
        case 0x1C:
            printf("Timeout — finger not detected (0x1C)\n");
            break;
        case 0x06:
            printf("Image too messy (0x06) — clean finger and retry\n");
            break;
        default:
            printf("Unknown response: 0x%02X\n", resp);
            break;
    }
}

int identifyFinger()
{
	strcpy(msg,"AT+IDENTIFY+50\r\n");

	tcflush(port, TCIFLUSH);
	write(port, msg, strlen(msg));
	printf("Sent: %s", msg);
	unsigned char buf[16];

    int n = 0;
    for (int attempt = 0; attempt < PROBE_TIMEOUT_S; attempt++) {
        sleep(1);
        n = read(port, buf, sizeof(buf));
        if (n > 0) break;
        printf("Waiting... (%d/%d)\n", attempt + 1, PROBE_TIMEOUT_S);
    }

    if (n <= 0) {
        printf("No response — finger not detected or module offline\n");
        return -1;
    }

    printf("RAW (%d bytes):", n);
    for (int i = 0; i < n; i++)
        printf(" %02X", buf[i]);
    printf("\n");

	if(buf[0] != 0x01 && n == 4){
		printf("The fingerprints match to id %d\n",buf[1]);
    	return buf[1];
    }
    else return -1;
}

void getEnrolledList()
{
	strcpy(msg,"AT+GET+ENROLLED_LIST\r\n");
	sendEnrolled(msg);	
}


void deleteAll()
{
	strcpy(msg,"AT+DEL_ALL_USER\r\n");
	send_cmd(msg);	
}

void deleteUser(int id)
{
	char cmd[64];
	snprintf(cmd,sizeof(cmd),"AT+DEL_THE_USER+%d\r\n",id);
	send_cmd(cmd);	
}

// biometric commands above



//  Other commands     


void setCompression()
{
	strcpy(msg,"AT+SET+JPEGCOMPRESSIONRATIO+0\r\n");
	send_cmd(msg);
}         


void getValidtempCount()
{
	strcpy(msg,"AT+GET+VALID_TEMP_COUNT\r\n");
	send_cmd(msg);	
}

void getUsertemplate(int id)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd),
             "AT+GET+USER_TEMP+1+%d\r\n", id);

    tcflush(port, TCIFLUSH);

    write(port, cmd, strlen(cmd));
    printf("Sent: %s", cmd);

    char filename[64];
    snprintf(filename, sizeof(filename),
             "Template_%d.bin", id);

    FILE *fp = fopen(filename, "wb");

    if(fp == NULL)
    {
        printf("Cannot open file\n");
        return;
    }

    unsigned char buf[1024];
    int total = 0;

    while(1)
    {
        int n = read(port, buf, sizeof(buf));

        if(n <= 0)   // timeout or error
            break;

        fwrite(buf, 1, n, fp);
        total += n;
    }

    fclose(fp);

    printf("Saved %d bytes to %s\n",
           total, filename);
           
    snprintf(cmd, sizeof(cmd), "xxd %s", filename);
    system(cmd);
}

void getFeature(void)
{
    strcpy(msg, "AT+GET+FEATURE+50\r\n");

	tcflush(port, TCIFLUSH);
	write(port, msg, strlen(msg));
	printf("Sent: %s", msg);
	unsigned char buf[1024];

    int n = 0;
    for (int attempt = 0; attempt < PROBE_TIMEOUT_S; attempt++) {
        sleep(1);
        n = read(port, buf, sizeof(buf));
        if (n > 0) break;
        printf("Waiting... (%d/%d)\n", attempt + 1, PROBE_TIMEOUT_S);
    }

    if (n <= 0) {
        printf("No response — finger not detected or module offline\n");
        return;
    }


    FILE *fp = fopen("features.bin","wb");

    if(fp == NULL)
    {
        printf("Cannot open file\n");
        return;
    }

    fwrite(buf,1,n,fp);

    fclose(fp);

    system("xxd features.bin");
    printf("Saved %d bytes\n", n);
}

void getSN(void)
{
    strcpy(msg, "AT+GET+SN\r\n");
    send_cmd(msg);
}

void getFwver(void)
{
    strcpy(msg, "AT+FWVER\r\n");

    tcflush(port, TCIFLUSH);
    write(port, msg, strlen(msg));
    printf("Sent: %s", msg);

    unsigned char buf[64];
    int total = 0;

    while (1)
    {
        int n = read(port, buf + total, sizeof(buf) - total);

        if (n <= 0)
            break;

        total += n;
    }

    printf("HEX (%d bytes):\n", total);
    for (int i = 0; i < total; i++)
        printf("%02X ", buf[i]);
    printf("\n");

    if (total >= 3 && buf[0] == 0x00)
    {
        unsigned char byte = buf[2];
        float ver = (float)byte / 10;

        printf("Firmware number: %.1f\n", ver);
    }
}

void setLed(int led,int mode)
{
	char cmd[64];
	snprintf(cmd,sizeof(cmd),"AT+SET+LEDSTATUS+%d+%d\r\n",led,mode);
	send_cmd(cmd);
}

void getLedStatus(void)
{
    strcpy(msg, "AT+GET+LEDSTATUS+2\r\n"); send_cmd(msg);
    strcpy(msg, "AT+GET+LEDSTATUS+3\r\n"); send_cmd(msg);
}











/* ------------------------------------------------------------------ */
/*  Menu                                                                */
/* ------------------------------------------------------------------ */
void choiceMenu(void)
{
    int choice;
    while (1) {
        printf("\n========== Menu ==========\n");
        printf("  1  Probe FIR finger   \n");
        printf("  2  Download FIR image \n");
        printf("  3  Enroll finger   (save to ID 0-63)\n");
        printf("  4  Verify finger   (match against ID)\n");
        printf("  5  Get Features\n");
        printf("  6  Identify Finger\n");
        printf("  7  Get Valid Templates Count\n");
        printf("  8  Get User Template\n");
		printf("  9  Get Enrolled List\n");
		printf(" 10  Delete All Users\n");
		printf(" 11  Delete User\n");
		printf(" 12  Set Compression Ratio\n");
        printf(" 20  Get serial number\n");
        printf(" 21  Get Firmware number\n");
        printf(" 22  Set LED status\n");
        printf(" 23  Get LED status\n");
        printf("  0  Exit\n");
        printf("==========================\n");
        printf("Enter choice: ");
        scanf("%d", &choice);

        switch (choice) {
            case 1:  probeJPEG();   break;
            case 2: getJPEG(); 
            	    break;
           //  Probe and Get
            case 3: {
                int id;
                printf("Enter user ID (0-63): ");
                scanf("%d", &id);
                enrollFinger(id);
                break;
            }
            case 4: {
                int id;
                printf("Enter user ID to verify against (0-63): ");
                scanf("%d", &id);
                verifyFinger(id);
                break;
            }

            case 5:  getFeature();  break;
			case 6: identifyFinger(); break;

			case 9: getEnrolledList(); break;

			case 10: deleteAll(); break;
			case 11:{
				int id;
				printf("Enter the id:");
				scanf("%d",&id);
				deleteUser(id);
			}
			break;
			// Biometric functions

			case 12: setCompression(); break;
			
			case 7: getValidtempCount(); break;
			case 8: {
				int id;
				printf("Enter User ID:");
				scanf("%d",&id);
				

				getUsertemplate(id); break;
            }

            
            case 20: getSN();       break;
            case 21: getFwver(); break;
            case 22: int led;
               		 printf("Enter 2 for green and 3 for red:");
					 scanf("%d",&led);
					 int mode;
					 printf("Enter 0 to turn off and 1 to turn on:");
					 scanf("%d",&mode);
					 setLed(led,mode);
            		 break;
            case 23: getLedStatus(); break;
            case 0:  return;
            default: printf("Invalid choice\n");
        }
    }
}



/*  main        
int main(void)
{
    if (startAndConfigUart(DEVICE) == -1)
        return 1;

while(1)
{
    char choice;

    printf("------------Menu----------\n");
    printf("1 UART functions\n");
    printf("2 Attendance biometric\n");
    printf("0 Exit\n");
    printf("Enter your choice: ");

    scanf(" %c", &choice);   // space before %c skips whitespace/newline

    if(choice == '0')
    {
        break;
    }

    switch(choice)
    {
        case '1':
            choiceMenu();
            break;

        case '2':
            // Attendance biometric function
            break;

        case '0':
            break;

        default:
            printf("Invalid choice\n");
    }
}	
    close(port);
    return 0;
}
*/
