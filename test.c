

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

//Signal handlers
void catchSIGTSTP(int signum) {
		if ( foregroundOnly == false ) {
			foregroundOnly = true;
			printf("Entering foreground-only mode (& is now ignored)\n");
			fflush(stdout);
		}
		else {
			foregroundOnly = false;
			printf("Exiting foreground-only mode\n");
		}
}

//PROTOTYPES
void parseUserInput( char* userInput, char* commandArgs[], char* commandOpts[], int* numArgs, int* numOpts) ;


int main (void) {

	//VARIABLE DECLARATIONS
		int i;

		char* userInput = NULL;
		size_t maxUserInput = BUF_LEN;
		int inputLength;

		pid_t spawnId;
		int childExitMethod;
		int exitStatus;

		//store command arguments and options as array elements
		char* commandArgs[ARG_TOT];
		int numArgs;
		char* commandOpts[ARG_TOT];
		int numOpts;

		//flag to restart main loop
		int badCommand;
		
		//pointer to input/output filename
		char* filename;
		int file_d;

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

	//TODO: kill child zombie when child terminates

	//setup signal actions
	SIGINT_action.sa_handler = SIG_IGN;				//ignore SIGINT
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_SIGINFO;

	//TODO: bind signal actions to signals
	//sigaction(SIGINT, &SIGINT_action, NULL);
	//sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	//save this process id as int and string
	int thisPid = getpid();
	char* pidString = NULL;
	asprintf(&pidString, "%d", thisPid);	

	if ( getcwd(cwd, sizeof(cwd) ) == NULL ) {
		perror("could not get current working directory");
		exit(1);
	}

    while( true ) {
		//reset 0, 1, 2 file descriptors
		dup2(default_0, 0);
		dup2(default_1, 1);
		dup2(default_2, 2);
		
		//reset flags
		badCommand = false;
		pauseShell = true;

		memset(commandArgs, 0, sizeof(commandArgs));
		numArgs = 0;
		memset(commandOpts, 0, sizeof(commandArgs));
		numOpts = 0;


        //prompt user for input
        printf(": ");
        fflush(stdout);
		fflush(stdin);
		inputLength = getline( &userInput, &maxUserInput, stdin);
        userInput[strlen(userInput) - 1]  = '\0';
        
		if ( strcmp(userInput, "") != 0 ) {

			parseUserInput(userInput, commandArgs, commandOpts, &numArgs, &numOpts);

			//replace $$ with pid
			for( i=0; i < numArgs; i++) {
				if( strcmp(commandArgs[i], "$$") == 0 ) {
					commandArgs[i] = pidString;	
				}
			}
			
			//handle exit, cd, status, comments 
			if( strcmp(commandArgs[0], "exit") == 0 ) {
				break;
			}
			if( strcmp(commandArgs[0], "cd") == 0 ) {
				if ( chdir( commandArgs[1] ) != 0 ) {
					fprintf( stderr, "could not cd to %s\n", commandArgs[1]);
					fflush(stderr);
				}
			}
			if( strcmp(commandArgs[0], "status") == 0 ) {
				//TODO: print out either the exit status or the terminating signal of the last foreground process (not both, processes killed by signals do not have exit statuses!) ran by your shell.
			}
			if ( commandArgs[0][0] == '#' ) {
				continue;
			}

			
			//check options for file input/output
			for ( i=0; i<numOpts; i++ ) {
				
				filename = commandOpts[i+1];
				
				//set fd 0 to filename descriptor for reading
				if( strcmp( commandOpts[i], "<") == 0 ) {
				
					file_d = open(filename, O_RDONLY);
					
					if ( file_d < 0 ) {
						fprintf(stderr, "error opening %s for read", filename);
						fflush(stderr);
						badCommand = true;
					} 
					else {
						dup2(file_d, 0);
					}
				}
				//set fd 1 to filename descriptor for writing
				else if ( strcmp( commandOpts[i], ">") == 0 ) {
				
					file_d = open( filename, O_WRONLY | O_CREAT | O_TRUNC, 0755 );
				
					if ( file_d < 0 ) {
						fprintf(stderr, "error opening %s for write", filename);
						fflush(stderr);
						badCommand = true;
					} 
					else {
						dup2(file_d, 1);
					}
				}
			}

			if(badCommand) continue;		
			
			//get cwd
			if ( getcwd(cwd, sizeof(cwd) ) == NULL ) {
				perror("could not get current working directory");
				exit(1);
			}

			//if last option is &, make child process background (don't pause Shell)
			if(  foregroundOnly == false && numOpts > 0 && strcmp( commandOpts[numOpts - 1], "&" ) == 0 ){
				pauseShell = false;
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

			//when command received, fork current process and exec commend in child
			childExitMethod = -5;
			spawnId = fork();
		
			switch (spawnId) {
				//error
				case -1:                    
					perror("process fork error!");
					exit(1); break;
				//child
				case 0:
					execvp( commandArgs[0], commandArgs );
					perror("CHILD: exec failure!\n");
					exit(2); break;
				//parent
				default:
					//halt program if not background
					if (pauseShell) {
					   
					   waitpid(spawnId, &childExitMethod, 0);
						
						if( WIFEXITED(childExitMethod) ) {
							exitStatus = WEXITSTATUS( childExitMethod );
							if (exitStatus != 0) {
								fprintf(stderr, "child process exited with %d\n", exitStatus); 
								fflush(stderr);
							}
						}
						else if( WIFSIGNALED(childExitMethod) ) {
							perror("child exited by signal\n");
							fflush(stderr);
						}
					}
					else {
						printf("%d\n", spawnId);
					}

					break;
			}
		}
    }

    free (userInput);
	free (pidString);

    return 0;
}



/* ---------------------- FUNCTIONS ---------------------- */


/*
	tokenizes userInput and points argument and option arrays at tokenized strings
*/
void parseUserInput( char* userInput, char* commandArgs[], char* commandOpts[], int* numArgs, int* numOpts) {

	//tokize userInput store arg pointers in comamndArgs
	char* tok = strtok(userInput, " ");

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
