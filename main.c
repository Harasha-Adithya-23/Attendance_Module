#include"A600.h"
#include"Attendance.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdlib.h>
#include <time.h>
int main()
{
	int code;
	printf("Enter 1 to for  A600 functions and 2 for Attendance Module and 0 to Exit:");
	scanf("%d",&code);

	if (startAndConfigUart(DEVICE) == -1)
		return 1;	

	if(code == 1)
	{		
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
	}


	if (code == 2)
	{
		setDefault();
		verbose = 0;
		getPrevData();
		AttendanceMenu();
		updateData();
	}

	close(port);
	return 0;	
}
