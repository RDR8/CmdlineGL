#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#include "Global.h"
#include "GlHeaders.h"
#include "Server.h"
#include "ProcessInput.h"

bool Shutdown= false;

typedef struct {
	char *FifoName;
	bool TerminateOnEOF;
	bool ShowCmds, ShowConsts;
	bool WantHelp;
	bool NeedHelp;
} CmdlineOptions;

void ReadParams(char **args, CmdlineOptions *Options);
void PrintUsage();
void CheckInput();
void DelayedInit(int value);

long microseconds(struct timeval *time);

void handleResize(int w, int h);
void display() {}
void mouse(int btn, int state, int x, int y);
void mouseMotion(int x, int y);
void asciiKeyDown(unsigned char key, int x, int y);
void asciiKeyUp(unsigned char key, int x, int y);
void specialKeyDown(int key, int x, int y);
void specialKeyUp(int key, int x, int y);

char Buffer[1024];
int InputFD= 0;

long StartTime;

CmdlineOptions Options;

int main(int Argc, char**Args) {
	struct timeval curtime;

	memset(&Options, 0, sizeof(Options));
	ReadParams(Args, &Options);
	
	if (Options.WantHelp || Options.NeedHelp) {
		PrintUsage();
		return Options.WantHelp? 0 : -1;
	}
	if (Options.ShowCmds) {
		DumpCommandList(stdout);
		return 0;
	}
	if (Options.ShowConsts) {
		DumpConstList(stdout);
		return 0;
	}

	if (Options.FifoName) {
//		if (CreateListenSocket(Options.SocketName, &InputFD))
		if (mkfifo(Options.FifoName, 0x1FF) < 0) {
			perror("mkfifo");
			return 2;
		}
		InputFD= open(Options.FifoName, O_RDONLY | O_NONBLOCK);
		if (InputFD < 0) {
			perror("open(fifo, read|nonblock)");
			return 3;
		}
		close(0); // Done with stdin.
	}
	else {
		DEBUGMSG(("Enabling nonblocking mode on stdin\n"));
		if (fcntl(InputFD, F_SETFL, O_NONBLOCK) < 0) {
			perror("fnctl(stdin, F_SETFL, O_NONBLOCK)");
			return 3;
		}
	}

	DEBUGMSG(("Initializing glut\n"));

	glutInit(&Argc, Args);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
	glutCreateWindow("CmdlineGL");

	DEBUGMSG(("Assigning functions\n"));
	glutReshapeFunc(handleResize);
	glutMouseFunc(mouse);
	glutMotionFunc(mouseMotion);
	glutKeyboardFunc(asciiKeyDown);
	glutKeyboardUpFunc(asciiKeyUp);
	glutSpecialFunc(specialKeyDown);
	glutSpecialUpFunc(specialKeyUp);
	glutDisplayFunc(display);
	glutIgnoreKeyRepeat(GLUT_KEY_REPEAT_OFF);

	DEBUGMSG(("Resizing window\n"));
	glutInitWindowSize(500, 501);

	DEBUGMSG(("Recording time\n"));
	gettimeofday(&curtime, NULL);
	StartTime= microseconds(&curtime);

	DEBUGMSG(("Scheduling delayed initialization (bug workaround)\n"));
	glutTimerFunc(1, DelayedInit, 0);
	
	DEBUGMSG(("Entering glut loop\n"));
	glutMainLoop();
}

void DelayedInit(int value) {
	glutIdleFunc(CheckInput);
}

void ReadParams(char **Args, CmdlineOptions *Options) {
	char **NextArg= Args+1;
	bool ReadingArgs= true;
	// Note: this function takes advantage of the part of the program argument specification
	//   that says all argv lists must end with a NULL pointer.
	while (ReadingArgs && *NextArg && (*NextArg)[0] == '-') {
		switch ((*NextArg)[1]) {
		case 'f': Options->FifoName= *++NextArg; break; // '-f <blah>' = read from FIFO "blah"
		case 't': Options->TerminateOnEOF= true; break;
		case 'h':
		case '?': Options->WantHelp= true; break;
		case '\0': ReadingArgs= false; break; // "-" = end of arguments
		case '-': // "--" => long-option
			if (strcmp(*NextArg, "--help") == 0) { Options->WantHelp= true; break; }
			if (strcmp(*NextArg, "--showcmds") == 0) { Options->ShowCmds= true; break; }
			if (strcmp(*NextArg, "--showconsts") == 0) { Options->ShowConsts= true; break; }
		default:
			fprintf(stderr, "Unrecognized argument: %s", *NextArg);
			Options->NeedHelp= true;
			return;
		}
		NextArg++;
	}
}

void PrintUsage() {
	fprintf(stderr, "%s",
	"Usage:\n"
	"  <command-source> | CmdlineGL [options] | <user-input-reader>\n"
	"     Reads commands from stdin, and writes user input to stdout.\n"
	"\n"
	"Options:\n"
	"  -h            Display this help message.\n"
	"  -t            Terminate after receiving EOF\n"
	"  -f <fifo>     Create the named fifo (file path+name) and read from it.\n"
	"  --showcmds    List all the available commands in this version of CmdlineGL.\n"
	"  --showconsts  List all the constants (GL_xxxx) that are available.\n"
	"\n"
	"Note: Each line of input is broken on space characters and treated as a\n"
 	"      command.  There is currently no escaping mechanism, although I'm not\n"
 	"      opposed to the idea.\n\n");
}

long microseconds(struct timeval *time) {
	return time->tv_usec + ((long) time->tv_sec)*1000000;
}

PUBLISHED(cglExit,DoQuit) {
	Shutdown= true;
	return 0;
}

PUBLISHED(cglGetTime,DoGetTime) {
	struct timeval curtime;
	if (argc != 0) return ERR_PARAMCOUNT;
	gettimeofday(&curtime, NULL);
	printf("t=%d\n", microseconds(&curtime)/1000);
	return 0;
}

PUBLISHED(cglSync,DoSync) {
	struct timeval curtime;
	long target, t;
	char *endptr;
	
	if (argc != 1) return ERR_PARAMCOUNT;
	target= strtol(argv[0], &endptr, 10) * 1000;
	if (*endptr != '\0') return ERR_PARAMPARSE;
	
	gettimeofday(&curtime, NULL);
	t= microseconds(&curtime) - StartTime;
	if (target - t > 0)
		usleep(target - t);
	return 0;
}

PUBLISHED(cglSleep, DoSleep) {
	long t;
	char *endptr;
	
	if (argc != 1) return ERR_PARAMCOUNT;
	t= strtol(argv[0], &endptr, 10) * 1000;
	if (*endptr != '\0' || t <= 0) return ERR_PARAMPARSE;
	usleep(t);
	return 0;
}

PUBLISHED(cglEcho,DoEcho) {
	int i;
	if (argc == 0)
		printf("\n");
	else {
		for (i=0; i<argc-1; i++)
			printf("%s ", argv[i]);
		printf("%s\n", argv[argc-1]);
	}
	fflush(stdout);
	return 0;
}

void CheckInput() {
	int result, TokenCount, CmdCount;
	char *Line, *TokenPointers[MAX_GL_PARAMS+1];

	errno= 0;
	CmdCount= 0;
	while ((CmdCount < MAX_COMMAND_BATCH || IsGlBegun) && (Line= ReadLine(InputFD))) {
		DEBUGMSG(("%s\n", Line));
		if (ParseLine(Line, &TokenCount, TokenPointers))
			ProcessCommand(TokenPointers, TokenCount);
		else
			DEBUGMSG(("Empty line ignored\n"));
		CmdCount++;
	}
	if (CmdCount < MAX_COMMAND_BATCH && errno != EAGAIN) {
		DEBUGMSG(("Received EOF.\n"));
 		if (Options.TerminateOnEOF)
			Shutdown= 1;
	}

	if (Shutdown) {
		DEBUGMSG(("Shutting down.\n"));
		close(InputFD);
		// this might be a socket
		if (Options.FifoName) {
			unlink(Options.FifoName);
		}
		exit(0);
	}
}

/*
bool CreateListenSocket(char *SocketName, int *ListenSocket) {
	int sock;
	struct sockaddr_un Addr;

	// build the address
	Addr.sun_family= AF_UNIX;
	if (strlen(SocketName) >= sizeof(Addr.sun_path)) {
		fprintf(stderr, "Socket name too long.");
		return false;
	}
	strcpy(Addr.sun_path, SocketName);

	sock= socket(PF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("create socket");
		return false;
	}

	if (bind(sock, (struct sockaddr*) &Addr, sizeof(Addr))) {
		perror("bind socket");
		return false;
	}

	*ListenSocket= sock;
}
*/
void handleResize(int w, int h) {
	// Use the entire window for rendering.
	glViewport(0, 0, w, h);
	// Recalculate the projection matrix.
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	GLfloat top, bottom, left, right;
	if (w<h) {
		// if the width is less than the height, make the viewable width
		// 20 world units wide, and calculate the viewable height assuming
		// as aspect ratio of "1".
		left= -10;
		right= 10;
		bottom= -10.0 * ((GLfloat)h) / w;
		top= 10.0 * ((GLfloat)h) / w;
	}
	else {
		// if the height is less than the width, make the viewable height
		// 20 world units tall, and calculate the viewable width assuming
		// as aspect ratio of "1".
		left= -10.0 * ((GLfloat)w) / h;
		right= 10.0 * ((GLfloat)w) / h;
		bottom= -10;
		top= 10;
	}

	// In perspective mode, use 1/10 the world width|height at the near
	//  clipping plane.
	glFrustum(left/10, right/10, bottom/10, top/10, 1.0, 100.0);

	glMatrixMode(GL_MODELVIEW);
}

void mouse(int btn, int state, int x, int y) {
	const char* BtnName;
	mouseMotion(x,y);
	switch (btn) {
		case GLUT_LEFT_BUTTON: BtnName= "MOUSE_LEFT"; break;
 		case GLUT_RIGHT_BUTTON: BtnName= "MOUSE_RIGHT"; break;
		case GLUT_MIDDLE_BUTTON: BtnName= "MOUSE_MIDDLE"; break;
		default: BtnName= "MOUSE_UNDEFINED";
	}
	printf("%c%s\n", (state == GLUT_UP)? '-':'+', BtnName);
	fflush(stdout);
}
void mouseMotion(int x, int y) {
	printf("@%i,%i\n", x, y);
	fflush(stdout);
}
void asciiKeyDown(unsigned char key, int x, int y) {
	printf("+%c\n", key);
	fflush(stdout);
}
void asciiKeyUp(unsigned char key, int x, int y) {
	printf("-%c\n", key);
	fflush(stdout);
}
const char* GetSpecialKeyName(int key);
void specialKeyDown(int key, int x, int y) {
	printf("+%s\n", GetSpecialKeyName(key));
	fflush(stdout);
}
void specialKeyUp(int key, int x, int y) {
	printf("-%s\n", GetSpecialKeyName(key));
	fflush(stdout);
}
const char* GetSpecialKeyName(int key) {
	switch (key) {
	case GLUT_KEY_F1 : return "F1";
	case GLUT_KEY_F2 : return "F2";
	case GLUT_KEY_F3 : return "F3";
	case GLUT_KEY_F4 : return "F4";
	case GLUT_KEY_F5 : return "F5";
	case GLUT_KEY_F6 : return "F6";
	case GLUT_KEY_F7 : return "F7";
	case GLUT_KEY_F8 : return "F8";
	case GLUT_KEY_F9 : return "F9";
	case GLUT_KEY_F10: return "F10";
	case GLUT_KEY_F11: return "F11";
	case GLUT_KEY_F12: return "F12";
	case GLUT_KEY_LEFT     : return "LEFT";
	case GLUT_KEY_RIGHT    : return "RIGHT";
	case GLUT_KEY_UP       : return "UP";
	case GLUT_KEY_DOWN     : return "DOWN";
	case GLUT_KEY_PAGE_UP  : return "PAGEUP";
	case GLUT_KEY_PAGE_DOWN: return "PAGEDOWN";
	case GLUT_KEY_HOME     : return "HOME";
	case GLUT_KEY_END      : return "END";
	case GLUT_KEY_INSERT   : return "INSERT";
	default: return "KEY_UNKNOWN";
	}
}

