#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <string.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <strings.h>
#include <signal.h>
#include <errno.h>

#include "lxtouch.h"


#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#undef DEBUG

mtpoints currentPointData;
tsinfo tsInfo;
static int debugFlags = 0;
#define DEBUG_POINTS	0x0001


#ifndef RTI_NDDS
static int point_sock;
static struct sockaddr_in point_sock_in;
static char point_buffer[ sizeof(tsinfo) + sizeof(mtpoints) ];
#endif

void initPoints( mtpoints *pointData )
{
	int i;
	memset( pointData, 0, sizeof(mtpoints) );
	for ( i = 0 ; i < MAX_POINTS ; i++ )
	{
		pointData->points[i].id = -1;
	}
}
#ifdef DEBUG
void hexDump( char *label, char *data, int size )
{
	int i;
	printf( "\n%s", label );
	for( i = 0 ; i < size ; i++ )
	{
		if ( i % 8 == 0 )
			printf( "\n  %04x: ", i );
		printf( "%02x ", ( data[i] & 0x0FF ) );
	}
	printf( "\n" );
}
#endif
#ifndef RTI_NDDS
void ddsSendPoints( mtpoints *pointData, tsinfo *info )
{
	int count;
	int pointSize;
	int sent;
	if ( point_sock == 0 || (debugFlags & DEBUG_POINTS) != 0 )
	{
		printf( "Report\n" );
		for ( count = 0 ; count < pointData->count ; count++ )
		{
			printf( "  %02d: %02d %c %04d %04d\n", count, pointData->points[count].id, (pointData->points[count].pressed ? 'y' : 'n'), pointData->points[count].x, pointData->points[count].y);
		}
		printf( "\n" );
	}
	
	if ( point_sock )
	{
		memcpy( &point_buffer[0], &tsInfo, sizeof(tsinfo));
		pointSize = sizeof(int) + (sizeof(point) * pointData->count);
#ifdef DEBUG
		hexDump( "Point Data", (char *)pointData, pointSize );
#endif
		memcpy( &point_buffer[sizeof(tsinfo)], pointData, pointSize );
		pointSize += sizeof(tsinfo);
#ifdef DEBUG
		hexDump( "UDP Buffer", (char *)point_buffer, pointSize );
#endif
		sent = sendto(point_sock, point_buffer, pointSize, 0, (struct sockaddr *)&point_sock_in, sizeof(struct sockaddr));
		if (sent < 0)
		{
			perror("sendto");
			exit(1);
		}
	}
}
#endif

/* TODO: Close fd on SIGINT (Ctrl-C), if it's open */
int publishMultitouchData()
{
	mtpoints sendPointData;
	int sendIndex, currnetIndex;
	/* So, here is the idea.  Any ID in "current" is being "pressed".
	 * Any id that is in "last", but no longer in "current" gets
	 * one final report, with the "pressed" flag set to false.
	 */
	sendIndex = 0;
	for ( currnetIndex = 0 ; currnetIndex < MAX_POINTS ; currnetIndex++ )
	{
		/* Not a valid ID, skip */
		if ( currentPointData.points[currnetIndex].id == -1 )
			continue;

		/* Copy */
		memcpy( &sendPointData.points[sendIndex], &currentPointData.points[currnetIndex], sizeof( point ) );
		sendIndex++;

		/* Point was released, mark it as invalid for next time */
		if ( currentPointData.points[currnetIndex].pressed == 0 )
			currentPointData.points[currnetIndex].id = -1;

	}
	if ( sendIndex ) {
		sendPointData.count = sendIndex;
		ddsSendPoints(&sendPointData,&tsInfo);
	}
	return sendIndex;
}
int attemptToOpen( void )
{
	int fd = 0;
	DIR *inputDir;
	char devname[20];
	struct dirent *dirEnt;
	long events[NBITS(EV_MAX)];
	long keys[NBITS(KEY_MAX)];
	long abs[NBITS(ABS_MAX)];

	fd = 0;
	while( fd == 0 )
	{
		inputDir = opendir("/dev/input");
		while ( (dirEnt = readdir( inputDir )) != NULL )
		{
			if ( (dirEnt->d_type & DT_CHR) )
			{
				sprintf( devname, "/dev/input/%s", dirEnt->d_name );
				printf( "Checking %s\n", devname );
				fd = open(devname, O_RDONLY);
				if (fd > 0 )
				{
					ioctl(fd, EVIOCGBIT(0, EV_MAX), events);
					if ( test_bit( EV_ABS, events ) && test_bit( EV_KEY, events ) )
					{
						ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs) ), abs);
						ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
						if ( test_bit( BTN_TOUCH, keys ) && test_bit( ABS_MT_TRACKING_ID, abs ) )
						{
							printf( "Found a MultiTouch device %s\n", devname );
							if ( test_bit( BTN_LEFT, keys ) ||
									test_bit( BTN_RIGHT, keys ) ||
									test_bit( BTN_MIDDLE, keys ) )
							{
								printf( "But, it's probably a Mouse or a TouchPad...\n" );
							}
							else
							{
								break;
							}
						}

					}
					close( fd );
					fd = 0;
				}
			}
		}
		closedir( inputDir );
		sleep(5);
	}
	/* Print Device Name */


	ioctl(fd, EVIOCGNAME(sizeof(tsInfo.name)), &tsInfo.name[0]);
	printf("Input device name: \"%s\"\n", tsInfo.name);

	ioctl(fd, EVIOCGABS(0), abs);
	tsInfo.xmax = abs[1];
	tsInfo.xmin = abs[2];
	ioctl(fd, EVIOCGABS(1), abs);
	tsInfo.ymax = abs[1];
	tsInfo.ymin = abs[2];
	printf( "Touch Resolution (%d,%d) (%d,%d)\n", tsInfo.xmin, tsInfo.ymin, tsInfo.xmax, tsInfo.ymax );

	return fd;
}

void touchLoop()
{
	struct input_event ev;
	struct timeval timeout;
	fd_set fds;
	int fd;
	int needsTimeout = 0;
	int currentSlot;
	int result;
	timeout.tv_sec = 0;
	timeout.tv_usec = 16667;

	fd = attemptToOpen();
	currentSlot = 0;
	initPoints( &currentPointData );


	for (;;) {
		const size_t ev_size = sizeof(struct input_event);
		ssize_t size;

		/* TODO: use select() */
		if ( needsTimeout )
		{
			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			result = select((fd+1),&fds,NULL,NULL,&timeout);
		
			if ( result == 0 )
			{
				needsTimeout = publishMultitouchData();
				continue;
			}
			else if ( result == -1 )
			{
				close(fd);
				fd = attemptToOpen();
				currentSlot = 0;
				initPoints( &currentPointData );
				needsTimeout = 0;
			}
		}
		
		size = read(fd, &ev, ev_size);
		if (size < ev_size) {
			close(fd);
			fd = attemptToOpen();
			currentSlot = 0;
			initPoints( &currentPointData );
			needsTimeout = 0;
		}
		else if (ev.type == EV_SYN )
		{
			if ( ev.code == SYN_REPORT )
			{
				needsTimeout = publishMultitouchData();
			}
			else
			{
				fprintf(stderr, "Unexpected SYN code %x / val %x\n", ev.code, ev.value );
			}
		}
		else if (ev.type == EV_ABS )
		{
			switch (ev.code)
			{
			case ABS_MT_SLOT:
				currentSlot = ev.value;
			case ABS_MT_TRACKING_ID:
				if ( ev.value == -1 )
				{
					currentPointData.points[currentSlot].pressed = 0;
				}
				else
				{
					currentPointData.points[currentSlot].pressed = 1;
					currentPointData.points[currentSlot].id = ev.value;
				}
				break;
			case ABS_MT_POSITION_X:
				currentPointData.points[currentSlot].x = ev.value;
				break;
			case ABS_MT_POSITION_Y:
				currentPointData.points[currentSlot].y = ev.value;
				break;
			default:
				/* Other things like ABS_X and ABS_Y are reported, but we don't care */
				break;
			}
		}
	}

}

#ifndef RTI_NDDS
/* SIGINT handler */
static void appQuit(int dummy)
    {
    printf ("exit.\n");
    exit(0);
    }


int main(int argc, char *argv[])
{
    struct hostent     *host;
    char *debug;

	if ((getuid ()) != 0) {
		fprintf(stderr, "You are not root! This may not work...\n");
		return EXIT_SUCCESS;
	}
	
	debug = getenv("DEBUG");
	if ( debug != NULL )
	{
		debugFlags = strtol(debug, NULL, 16);
	}
	
	printf( "sizeof(tsinfo) = %d, sizeof(mtpoints) = %d\n", sizeof(tsinfo), sizeof(mtpoints) );
			

	if ( argc == 2 )
	{

		if ((point_sock = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
		{
			perror("socket");
			exit (1);
		}

		signal(SIGINT, appQuit);
		if ((host = gethostbyname (argv[1])) == NULL)
		{
			fprintf (stderr, "%s: unknown host\n", argv [1]);
			exit (1);
		}
		point_sock_in.sin_addr   = *((struct in_addr *)host->h_addr);
		point_sock_in.sin_port   = htons (TOUCH_PORT);
		point_sock_in.sin_family = AF_INET;
		bzero((char *)&point_sock_in.sin_zero, 8);  /* zero the rest of the struct */

	}
	else
	{
		printf( "No IP address given, using screen output mode.\n");
		point_sock = 0;
	}

	touchLoop();

	return EXIT_SUCCESS;

}
#endif
