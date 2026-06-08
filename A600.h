#ifndef A600_H
#define A600_H

/* --------------------------------------------------
   Standard libraries needed by declarations
-------------------------------------------------- */
#include <time.h>

/* --------------------------------------------------
   UART / Device configuration macros
   These become available wherever A600.h is included
-------------------------------------------------- */

#define DEVICE          "/dev/ttyUSB0"
#define BAUD            B115200
#define PACKET_SIZE     36864
#define MAX_PACKETS     256
#define PROBE_TIMEOUT_S 15
#define PKT_TIMEOUT_S   10


/* --------------------------------------------------
   Global variables

   extern means:
   "This variable exists somewhere else (.c file),
   don't create another copy."

   Actual definitions remain in A600.c:
       int port;
       unsigned char msg[255];
-------------------------------------------------- */

extern int verbose;
extern int port;
extern unsigned char msg[255];



/* ==================================================
                    UART FUNCTIONS
================================================== */

/*
Open UART device and configure serial settings

Input:
    device -> "/dev/ttyUSB0"

Returns:
    0  = success
   -1  = failure
*/
int startAndConfigUart(const char *device);


/*
Read exactly 'need' bytes with timeout

Returns:
    total bytes received
*/
int read_exact(unsigned char *buf,
               int need,
               int timeout_sec);


/*
Receive UART response and print
HEX + ASCII
*/
void rcv_msg(void);


/*
Receive raw UART response

Returns:
    number of bytes received
*/
int rcv_msg_raw(unsigned char *buf,
                int buf_size);


/*
Generic AT command sender
*/
void send_cmd(unsigned char *mesg);


/*
Special command:
Shows enrolled fingerprint IDs
*/
int* sendEnrolled(unsigned char *mesg);



/* ==================================================
                JPEG / IMAGE FUNCTIONS
================================================== */

/*
Probe sensor

Gets JPEG size before download
*/
void probeJPEG(void);


/*
Download fingerprint image

Saves:
    fingerprint.jpeg
*/
void getJPEG(void);



/* ==================================================
            FINGERPRINT FUNCTIONS
================================================== */

/*
Enroll fingerprint

Input:
    user_id = 0..63

Requires:
    3 presses
*/
void enrollFinger(int user_id);


/*
Verify fingerprint against ID
*/
void verifyFinger(int user_id);


/*
Identify scanned finger

Returns:
    matched ID
*/
int identifyFinger(void);


/*
List all enrolled IDs
*/
void getEnrolledList(void);


/*
Delete ALL enrolled users
*/
void deleteAll(void);


/*
Delete one user

Input:
    id = fingerprint ID
*/
void deleteUser(int id);



/* ==================================================
                TEMPLATE FUNCTIONS
================================================== */

/*
Get total enrolled templates
*/
void getValidtempCount(void);


/*
Download template for user

Saves:
    Template_X.bin
*/
void getUsertemplate(int id);


/*
Download fingerprint features

Saves:
    features.bin
*/
void getFeature(void);



/* ==================================================
                DEVICE INFO
================================================== */

/*
Get serial number
*/
void getSN(void);


/*
Get firmware version
*/
void getFwver(void);



/* ==================================================
                LED FUNCTIONS
================================================== */

/*
Control LEDs

led:
    2 = green
    3 = red

mode:
    0 = OFF
    1 = ON
*/
void setLed(int led,
            int mode);


/*
Get LED status
*/
void getLedStatus(void);



/* ==================================================
                SETTINGS
================================================== */

/*
Set JPEG compression ratio
*/
void setCompression(void);



/* ==================================================
                    MENU
================================================== */

/*
Interactive UART menu
*/
void choiceMenu(void);



#endif
