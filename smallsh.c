

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>


#define true 1
#define false 0

#define BUF_LEN 2048
#define ARG_TOT 512
#define BG_PROC 128

struct ProcessExitStatus {
	pid_t pid;
	int code;
	int termBySignal;
	int isBackground;
};


//GLOBAL
int foregroundOnly = false;				//global mode flag
int bgProcessTerminated = false;		//flag to tell main whether background process was terminated
pid_t bgProcesses[BG_PROC];
pid_t foreground_pid = 0;
pid_t deadZombie_pid;						
struct ProcessExitStatus exitStatus;



//PROTOTYPES
void parseUserInput( char* userInput, char* commandArgs[], char* commandOpts[], int* numArgs, int* numOpts) ;
int ioRedirect( char* commandOpts[], int numOpts, int pauseShell) ;
void printStatus ( struct ProcessExitStatus* process ) ;


//BG process array functions
void addBgProcess( pid_t pid) {
	int i;
	for (i=0; i < BG_PROC; i++) {
		if ( bgProcesses[i] == 0) {
			bgProcesses[i] = pid;
			break;
} } }
int removeBgProcess( pid_t pid) {
	int i;
	for (i=0; i < BG_PROC; i++) {
		if ( bgProcesses[i] == pid) {
			bgProcesses[i] = 0;
			return true;
		}
	}
	return false;
}



//SIGNAL HANDLERS
// catches child signals
// records status if child is background processes
// kills child if zombie

void catchSIGCHLD(int signo, siginfo_t* info, void* vp) {

	int childExitMethod = -5;
	pid_t spawnId = info->si_pid;	
	int result;

	//if the calling process was a killed zombie, or not background, ignore
	if( spawnId != deadZombie_pid && spawnId != foreground_pid && removeBgProcess(spawnId) ) {
	
		
		//get the status of the calling process
		do {
			errno = 0;
			result = waitpid( spawnId, &childExitMethod, WNOHANG); 
		} while ( errno == EINTR && result == -1);


		if ( result == spawnId)  {
			
			exitStatus.pid = spawnId;
			
			//if child created signal, but exited, then is zombie and needs to die
			if ( WIFEXITED(childExitMethod) ) {
				
				exitStatus.code = WEXITSTATUS(childExitMethod);
				exitStatus.termBySignal = false;
				exitStatus.isBackground = true;

				bgProcessTerminated = true;

				//so we ignore kill call for zombie processes
				deadZombie_pid = spawnId;	
				
				kill(spawnId, SIGTERM);
			}

			//otherwise if it was killed by signal, just get the signal info
			else if ( WIFSIGNALED(childExitMethod) ) {
				exitStatus.code = WTERMSIG(childExitMethod);
				exitStatus.termBySignal = true;
			
				if ( foregroundOnly == true ) {
					exitStatus.isBackground = false;
				}			
				else {
					bgProcessTerminated = true;
					exitStatus.isBackground = true;
				}
			}
		}
	}
}


//catches SIGINT and kills foreground process
void catchSIGINT(int signo) {
	
	int childExitMethod;
	int result;

	if (foreground_pid != 0) {
		
		do {
			errno = 0;
			kill( foreground_pid, SIGTERM);
			result = waitpid( foreground_pid, &childExitMethod, WNOHANG);
		} while (errno == EINTR && result == -1);

		if ( foreground_pid == result ) {
			exitStatus.code = SIGINT;
			exitStatus.pid = foreground_pid;
			exitStatus.isBackground = false;
			exitStatus.termBySignal = true;
			foreground_pid = 0;
		}
		else if ( result == -1 && errno == EINTR ) {
			char* output = "error\n";
			write(STDOUT_FILENO, output, 6); 
		}
	}
}


//catches SIGTSTP and sets foregroundOnly flag
void catchSIGTSTP(int signo) {
	
	char* enterMsg = "\nEntering foreground-only mode (& is now ignored)";
	char* exitMsg = "\nExiting foreground-only mode";
	char* newline = "\n: ";
	if ( foregroundOnly == false ) {
		foregroundOnly = true;
		write(STDOUT_FILENO, enterMsg, 49);
	}
	else {
		foregroundOnly = false;
		write(STDOUT_FILENO, exitMsg, 29);
	}
	write(STDOUT_FILENO, newline, 3);
}



/*-------------------------------   MAIN   ---------------------------------*/

int main (void) {

	exitStatus.pid = 0;

	//VARIABLE DECLARATIONS
		int i,  result;

		char* rawUserInput = NULL;
		size_t maxUserInput = BUF_LEN;
		int inputLength;
		char fixedUserInput[BUF_LEN];
		char* strCpyPtr;
		char* pidSwapPtr;

		pid_t spawnId;
		int childExitMethod;

		//store command arguments and options as array elements
		char* commandArgs[ARG_TOT];
		int numArgs;
		char* commandOpts[ARG_TOT];
		int numOpts;

		//working directory string
		char cwd[BUF_LEN];
		int pauseShell;
		
		//save default file descriptors
		int default_0 = dup(0);
		int default_1 = dup(1);
		int default_2 = dup(2);

		//signal catchers
		struct sigaction SIGINT_action = {0};
		struct sigaction SIGTSTP_action = {0};
		struct sigaction SIGCHLD_action = {0};
	

	//setup signal actions
		sigfillset(&SIGINT_action.sa_mask);
		SIGINT_action.sa_flags = SA_RESTART;
		SIGINT_action.sa_handler = catchSIGINT;
		
		sigfillset(&SIGTSTP_action.sa_mask);
		SIGTSTP_action.sa_flags = SA_RESTART;
		SIGTSTP_action.sa_handler = catchSIGTSTP;

		
		// kill child zombie when child terminates
		SIGCHLD_action.sa_flags = SA_SIGINFO;
		sigfillset(&SIGCHLD_action.sa_mask);
		SIGCHLD_action.sa_sigaction = catchSIGCHLD;


		// bind signal actions to signals
		sigaction(SIGINT, &SIGINT_action, NULL);
		sigaction(SIGTSTP, &SIGTSTP_action, NULL);
		sigaction(SIGCHLD, &SIGCHLD_action, NULL);

	//set bgprocesses array to 0
	for (i = 0; i<BG_PROC; i++) {
		bgProcesses[i]=0;
	}

	//save this process id as int and string
	int thisPid = getpid();
	char* pidString = NULL;
	asprintf(&pidString, "%d", thisPid);	

	//get current working directory
	if ( getcwd(cwd, sizeof(cwd) ) == NULL ) {
		perror("could not get current working directory");
		exit(1);
	}

    while( true ) {
		
		//reset flags
		pauseShell = true;

		//reset command argument and options pointers
		memset(commandArgs, 0, sizeof(commandArgs));
		numArgs = 0;
		memset(commandOpts, 0, sizeof(commandArgs));
		numOpts = 0;


		//get user input
		while(true) {
			if (bgProcessTerminated == true ) {
				printStatus(&exitStatus);
				fflush(stdout);
				bgProcessTerminated = false;
			}
			
			//prompt user input
			printf(": ");
			fflush(stdout);
        
			fflush(stdin);
	
			inputLength = getline( &rawUserInput, &maxUserInput, stdin);
    
			if ( inputLength == -1 ) {
				clearerr(stdin);
				continue;
			}
			else {
				rawUserInput[strlen(rawUserInput) - 1]  = '\0';
				if ( strcmp(rawUserInput, "") != 0 ) {
    	    		break;
				}
			}
		}
		
		if (bgProcessTerminated == true ) {
			printStatus(&exitStatus);
			fflush(stdout);
			bgProcessTerminated = false;
		}


	//replace all instances of $$ with pid
		fixedUserInput[0] = '\0';
		strCpyPtr = rawUserInput;
		pidSwapPtr = strstr( strCpyPtr, "$$" );
		
		while ( pidSwapPtr != NULL ) {		
			strncat(fixedUserInput, strCpyPtr, pidSwapPtr - strCpyPtr);
			strcat(fixedUserInput, pidString);
			strCpyPtr = pidSwapPtr + strlen("$$");
		
			pidSwapPtr = strstr( strCpyPtr, "$$" );
		} 
		//cat remainder
		strcat(fixedUserInput, strCpyPtr);

	//fills commandArgs and commandOpts with pointers to tokenized fixedUserInput string
		parseUserInput(fixedUserInput, commandArgs, commandOpts, &numArgs, &numOpts);

	//handle exit, cd, status, comments 
		if( strcmp(commandArgs[0], "exit") == 0 ) {
			break;
		}
		if( strcmp(commandArgs[0], "cd") == 0 ) {
			if ( commandArgs[1] == NULL ) {
				chdir(getenv("HOME"));
			}		
			else if ( chdir( commandArgs[1] ) == -1 ) {
				fprintf( stderr, "could not cd to %s\n", commandArgs[1]);
				fflush(stderr);
			}
			continue;
		}
		if( strcmp(commandArgs[0], "status") == 0 ) {
			if ( exitStatus.pid != 0 ) {
				printStatus(&exitStatus);
				fflush(stdout);
			}
			continue;
		}
		if ( commandArgs[0][0] == '#' )	continue;	
		
	//get cwd
		if ( getcwd(cwd, sizeof(cwd) ) == NULL ) {
			perror("could not get current working directory");
			exit(1);
		}

		if ( foregroundOnly == false  && numOpts > 0) {
			pauseShell = ( strcmp(commandOpts[numOpts-1], "&") != 0 );
		}

	//when command received, fork current process and exec commend in child
		childExitMethod = -5;
		spawnId = fork();
		switch (spawnId) {
			//error
			case -1:                    
				perror("process fork error: ");
				exit(1); break;
			//child
			case 0:
				if ( ioRedirect( commandOpts, numOpts, pauseShell) == true ) {
					execvp( commandArgs[0], commandArgs );
					perror(commandOpts[0]);
				}
				exit(1); break;
			//parent
			default:
				//make sure 0, 1, 2 file descriptors are reset
				dup2(default_0, 0);
				dup2(default_1, 1);
				dup2(default_2, 2);
		
				//halt program if not background
				if (pauseShell) {
					
					foreground_pid = spawnId; 
				
					//save foreground status
					exitStatus.pid = waitpid(spawnId, &childExitMethod, 0);
					exitStatus.isBackground = false;

					if ( WIFEXITED(childExitMethod) )	{
						exitStatus.code = WEXITSTATUS(childExitMethod);
						exitStatus.termBySignal = false;
					}
					else if (WIFSIGNALED(childExitMethod) ) {
						exitStatus.code = WTERMSIG(childExitMethod);
						exitStatus.termBySignal = true;
						printStatus(&exitStatus);
						fflush(stdout);
					}


					foreground_pid = 0;
				}
				else {
					addBgProcess(spawnId);
					printf("background pid is %d\n", spawnId);
					fflush(stdout);
				}

				break;
		}	
    }
	
	if ( rawUserInput != NULL)
    	free (rawUserInput);
	
	if (pidString != NULL)
		free (pidString);

    return 0;
}



/* ------------------------------- FUNCTIONS ---------------------------------- */



/*
	prints out the exit status of process that exited
*/
void printStatus ( struct ProcessExitStatus* process ) {

	char* signalMsg = "terminated by signal";
	char* exitMsg = "exit value";
	char* printMsg;
	
	if ( process->termBySignal ) {
		printMsg = signalMsg;
	} else {
		printMsg = exitMsg;
	}

	if ( bgProcessTerminated == true ) {
		printf("background pid %d is done: ", process->pid);
		fflush(stdout);
	}
	printf("%s %d\n", printMsg, process->code);
	fflush(stdout);
}

/*------------------------------------------------------------------------------*/

/*
	parse options for io redirect and background
	returns 0 (false) if there was an error opening files)
*/
int ioRedirect( char* commandOpts[], int numOpts, int pauseShell) {
	char* filename;
	int file_d;
	int i;
	
	//check options for file input/output
	for ( i=0; i<numOpts; i++ ) {
		
		filename = commandOpts[i+1];
		
		//set fd 0 to filename descriptor for reading
		if( strcmp( commandOpts[i], "<") == 0 ) {
		
			file_d = open(filename, O_RDONLY);
			
			if ( file_d < 0 ) {
				fprintf(stderr, "could not open %s for input\n", filename);
				fflush(stderr);
				return false;
			} 
			else {
				dup2(file_d, 0);
			}
		}
		//set fd 1 to filename descriptor for writing
		else if ( strcmp( commandOpts[i], ">") == 0 ) {
		
			file_d = open( filename, O_WRONLY | O_CREAT | O_TRUNC, 0755 );
		
			if ( file_d < 0 ) {
				fprintf(stderr, "could not open %s for output\n", filename);
				fflush(stderr);
				return false;
			} 
			else {
				dup2(file_d, 1);
			}
		}
	}

	//if last option is &, make child process background (don't pause Shell)
	if( pauseShell == false ) {
		// redirect bg process i/o to dev/null
		if(STDIN_FILENO == fileno(stdin)){
			file_d = open("/dev/null", O_RDONLY);	
			dup2(file_d, 0);
		}
		if(STDOUT_FILENO == fileno(stdout)) {
			file_d = open("/dev/null", O_WRONLY);	
			dup2(file_d, 1);
		}
	}
	return true;
}


/*------------------------------------------------------------------------------*/

/*
	tokenizes fixedUserInput and points argument and option arrays at tokenized strings
*/
void parseUserInput( char* fixedUserInput, char* commandArgs[], char* commandOpts[], int* numArgs, int* numOpts) {

	//tokize fixedUserInput store arg pointers in comamndArgs
	char* tok = strtok(fixedUserInput, " ");

	while( tok != NULL ) {
		//break if find <, > or &
		if ( strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0 || 
			strcmp(tok, "&") == 0 ) {
			break;
		}
		//have commandArgs[] point to tokenized substring string
		commandArgs[ *numArgs ] = tok;	
		*numArgs += 1;

		tok = strtok(NULL, " " );
	}


	//store remaining options in commandOpts
	while( tok != NULL) {
		commandOpts[ *numOpts ] = tok;
		*numOpts += 1;
		tok = strtok(NULL, " ");
	}
}
