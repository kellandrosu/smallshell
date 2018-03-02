

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <signal.h>


#define true 1
#define false 0

#define BUF_LEN 2048
#define ARG_TOT 512


//GLOBAL
int foregroundOnly = false;
int bgProcessTerminated = false;
pid_t foregroundProcess = 0;
char* statStr = NULL;


//PROTOTYPES
void parseUserInput( char* userInput, char* commandArgs[], char* commandOpts[], int* numArgs, int* numOpts) ;
void ioRedirect( char* commandOpts[], int numOpts, int pauseShell) ;
void getExitStatus( char** statString, int childExitMethod) ;


//SIGNAL HANDLERS
	
	//catches child signals and kills if child is zombie
	void catchSIGCHLD(int signo, siginfo_t* info, void* vp) {
		
		char* newline = "\n";
		int childExitMethod = -5;
		pid_t spawnId = info->si_pid;	
		
		if ( spawnId == waitpid( spawnId, &childExitMethod, WNOHANG) ) {
			//if child  is zombie, and needs to die
			if ( WIFEXITED(childExitMethod) ) {	
				getExitStatus( &statStr, childExitMethod );
				bgProcessTerminated = true;
				kill(spawnId, SIGTERM);
			}
//			else if (WIFSIGNALED(childExitMethod)) {
//			}
		}	
	}

	//catches SIGINT and kills foreground process
	void catchSIGINT(int signo) {
		
		int childExitMethod;
		
		if (foregroundProcess != 0) {
			kill( foregroundProcess, SIGINT);
			if ( foregroundProcess == waitpid( foregroundProcess, &childExitMethod, 0) ) {
				getExitStatus( &statStr, childExitMethod);
			}
			foregroundProcess = 0;
		}
	}

	//catches SIGTSTP and sets foregroundOnly flag
	void catchSIGTSTP(int signo) {
		
		char* enterMsg = "\nEntering foreground-only mode (& is now ignored)";
		char* exitMsg = "\nExiting foreground-only mode";
		
		if ( foregroundOnly == false ) {
			foregroundOnly = true;
			write(STDOUT_FILENO, enterMsg, 49);
		}
		else {
			foregroundOnly = false;
			write(STDOUT_FILENO, exitMsg, 29);
		}
	}



/*-------------------------------   MAIN   ---------------------------------*/



int main (void) {

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
		SIGINT_action.sa_flags = 0;
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

		if (bgProcessTerminated == true ) {
			printf(statStr);
			fflush(stdout);
			bgProcessTerminated = false;
		}

		//prompt user input
	    printf(": ");
    	fflush(stdout);
        
		//get user input
		while(true) {
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
			if (statStr != NULL) {
				printf(statStr);
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
				ioRedirect( commandOpts, numOpts, pauseShell);
				execvp( commandArgs[0], commandArgs );
				perror("CHILD: exec failure: ");
				exit(2); break;
			//parent
			default:
				//make sure 0, 1, 2 file descriptors are reset
				dup2(default_0, 0);
				dup2(default_1, 1);
				dup2(default_2, 2);
		
				//halt program if not background
				if (pauseShell) {
					
					foregroundProcess = spawnId; 
					waitpid(spawnId, &childExitMethod, 0);
					foregroundProcess = 0;
					getExitStatus( &statStr, childExitMethod );
				}
				else {
					printf("%d\n", spawnId);
					fflush(stdout);
				}

				break;
		}	
    }

	if (statStr != NULL ) 
		free(statStr);
	
	if ( rawUserInput != NULL)
    	free (rawUserInput);
	
	if (pidString != NULL)
		free (pidString);

    return 0;
}



/* ------------------------------- FUNCTIONS ---------------------------------- */


/*
	stores the exit status string according to the childExitMethod
*/
void getExitStatus( char** statString, int childExitMethod) {

	int result;
	
	if( WIFEXITED(childExitMethod) ) {
		result = asprintf( statString, "exit value %d            \n", WEXITSTATUS( childExitMethod ) );
	}
	else if( WIFSIGNALED(childExitMethod) ) {
		result = asprintf( statString, "terminated by signal %d  \n", WTERMSIG(childExitMethod));
	}
	if (result <= 0 ) {
		if (statString != NULL) free(statString);
		statString = NULL;
	}
}


/*
	parse options for io redirect and background
	returns 0 (false) if there was an error opening files)
*/
void ioRedirect( char* commandOpts[], int numOpts, int pauseShell) {
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
				fprintf(stderr, "error opening %s for read\n", filename);
				fflush(stderr);
			} 
			else {
				dup2(file_d, 0);
			}
		}
		//set fd 1 to filename descriptor for writing
		else if ( strcmp( commandOpts[i], ">") == 0 ) {
		
			file_d = open( filename, O_WRONLY | O_CREAT | O_TRUNC, 0755 );
		
			if ( file_d < 0 ) {
				fprintf(stderr, "error opening %s for write\n", filename);
				fflush(stderr);
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
}


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
