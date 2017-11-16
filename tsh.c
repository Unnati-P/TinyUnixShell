/* 
 * tsh - A tiny shell program with job control
 * 
 * 	Name: Unnati Parekh
 *	ID: 201501406@daiict.ac.in
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */

int check_if_fg; /* to check if the process is in the foreground state. */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
	
	sigset_t set;
	if(strcmp(cmdline,"\n")==0)	/* entering blank lines would return prompt again */
		return;
	char* argv[MAXARGS];
	int is_bg = parseline(cmdline,argv);
	int builtin=builtin_cmd(argv);
	if(!builtin)	/* for a non-builtin command */
	{
		sigemptyset(&set);
		sigaddset(&set,SIGCHLD);	
		if(sigprocmask(SIG_BLOCK,&set,NULL) < 0)	/* error handling */
			unix_error("sigprocmask error\n");		
		pid_t pid;
		if((pid=fork())==0)
		{
			setpgid(0, 0);
			
			/* unblocking SIGCHLD signal */
			if(sigprocmask(SIG_UNBLOCK,&set,NULL) < 0)	/* error handling */
				unix_error("sigprocmask error\n");			
			
			execvp(argv[0],argv);
			printf("%s : Command not found\n",argv[0]);	/* if execvp fails, print error message and terminate the child process */
			exit(0);
		}
		//waitfg(pid);
		if(!is_bg) 
		{ 
		
	/* 
	If the job is a foreground job, then add it to the joblist with state 'FG' and unblock the SIGCHLD signal.	
	*/
				addjob(jobs,pid,FG,cmdline); /* add job to the joblist */
				
				/* unblocking SIGCHLD signal */
				if(sigprocmask(SIG_UNBLOCK,&set,NULL) < 0)	/* error checking */
					unix_error("sigprocmask error\n");
				waitfg(pid); /* ensuring only 1 foreground process is there */
		} 
		else 
		{
		
	/* 
	If the job is a background job, then add it to the joblist with state 'BG' and unblock the SIGCHLD signal. 
	*/
			addjob(jobs,pid,BG,cmdline); /* add job to the joblist */
			printf("[%d] (%d) %s", pid2jid(pid),pid,cmdline); 
			
			/* unblocking SIGCHLD signal */
			if(sigprocmask(SIG_UNBLOCK,&set,NULL) < 0)	/* error checking */
				unix_error("sigprocmask error\n");
	/* 
	There can be multible jobs running in the background. Hence, we do not have to wait for the job to terminate before adding another background job. 
	*/
		}
	}
	
			
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
	if(strcmp(argv[0],"quit")==0)	/* typing 'quit' on the command line would terminate the shell */
	{
		for(int i=0 ; i<MAXJOBS; i++)
		{
			if(jobs[i].state==ST)	/* if stopped jobs are present, don't quit but return 1 */
			{
				printf("There are stopped jobs\n");
				return 1;
			}
		}
		exit(0);	/* quit if no stopped jobs present */
	}
	
	else if((strcmp(argv[0],"fg")==0) || (strcmp(argv[0],"bg")==0))	/* typing 'fg' or 'bg' on the command line calls do_bgfg function */
	{
		do_bgfg(argv);
		return 1;
	}
	
	else if(strcmp(argv[0],"jobs")==0)	/* typing 'jobs' on the command line prints the job table */
	{
		listjobs(jobs);
		return 1;
	}
	return 0;     /* if not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
	/* error handling section */
	if(!argv[1])	/* if no arguement is provided after fg/bg */
	{
		printf("%s command requires PID or %%jobid argument\n",argv[0]); 
		return;
	}
	else
	{
		int i;
		int num=0;
		int len=strlen(argv[1]);
		if(argv[1][0]=='%')
		{
			i=1;
			while(i<len)
			{	
				if(argv[1][i]<'0' || argv[1][i]>'9')	/* error checking if argument is not a number */
				{
					printf("%s: argument must be a PID or %%jobid\n",argv[0]);
					return;
				}
				num=num*10+(argv[1][i]-'0');	/* inplace of using atoi function */
				i++;
			}
			struct job_t *p=getjobjid(jobs,num);	/* checking for a job specified in the argument in the job table */
			if(p==NULL)
			{
				printf("%s : No such job\n",argv[1]);
				return;
			}
		}
		else
		{
			i=0;
			while(i<len)
			{
				if(argv[1][i]<'0' || argv[1][i]>'9')	/* error checking if argument is not a number */
				{
					printf("%s: argument must be a PID or %%jobid\n",argv[0]);
					return;
				}
				num=num*10+(argv[1][i]-'0');	/* inplace of using atoi function */
				i++;
			}
			struct job_t *p=getjobpid(jobs,num);	/* checking for a process specified in the argument in the job table */
			if(p==NULL)
			{
				printf("(%s) : No such process\n", argv[1]);		
				return;
			}
		}
	}
	/* end of error handling section. */
	
	/*
		When the bg command is executed,the stopped process resumes execution on receiveing the SIGCONT signal and runs in the background.
		
		bg maybe followed by job id or process id, so that needs to be checked first before changing the status of any job.
	*/
	if(!strcmp(*argv,"bg")) {
		int jid = atoi(*(argv+1)+1); /* converting argument to int */
		
		/* if jid of the job is mentioned as the argument */
		if(argv[1][0] == '%') 
		{
			kill(-(jobs[jid-1].pid),SIGCONT);	/* sending SIGCONT to the job */
			jobs[jid-1].state = BG;		/* change status of job to 'BG' */
			printf("[%d] (%d) %s",jid,jobs[jid-1].pid,jobs[jid-1].cmdline);			
		}
		
		/* if pid of the job is mentioned as the argument */
		else 
		{
			pid_t pid = jid;
			jid = pid2jid(pid);	/* obtaining jid from the given pid of process */
			kill(-pid,SIGCONT);	/* sending SIGCONT to the job */
			jobs[jid-1].state = BG;		/* change status of job to 'BG' */
			printf("[%d] (%d) %s",jid,pid,jobs[jid-1].cmdline);
		}
	}
	
	/*
		When the fg command is executed,the stopped process resumes execution on receiveing the SIGCONT signal and runs in the foreground.
		
		fg maybe followed by job id or process id, so that needs to be checked first before changing the status of any job.
		
	*/
	else if(!strcmp(*argv,"fg")) {
		int jid = atoi(*(argv+1)+1); /* converting argument into int */
		pid_t pid = 0;
		
		/* if jid of the job is mentioned as the argument */
		if(argv[1][0] == '%') 
		{
			pid = jobs[jid-1].pid;	/* obtain pid of the job using job id jid */
			kill(-(jobs[jid-1].pid),SIGCONT);	/* sending SIGCONT to the job */ 
			jobs[jid-1].state = FG;		/* change status of job to 'FG' */
		}
		
		/* if pid of the job is mentioned as the argument */
		else 
		{
			pid_t pid = jid;
			jid = pid2jid(pid);	/* obtaining jid from the given pid of process */
			kill(-pid,SIGCONT);	/* sending SIGCONT to the job */
			jobs[jid-1].state = FG;		/* change status of job to 'FG'*/
		}	
		waitfg(pid); /* calling waitfg function ensures that there is only one foreground process running at one time */
	}	
	return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */

void waitfg(pid_t pid)
{
  
    struct job_t *p;
    p = getjobpid(jobs,pid);	/* pinter to the entry in the job table of the job corresponding to pid */
    while(p!=NULL&&(p->state==FG))	/* looping until the state is no longer FG */ 
        {
          sleep(1);	/* sleep for 1 sec after each check */
          /*printf("Sleeping...")*/
        }
        return;
   
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
	pid_t pid;
	check_if_fg=0;
	int jid;	/* jid of the job being considered */
	int status;	/* status contains information about the status of the job that is stopped or terminated */
	
	/*
	waitpid checks if any child process is terminated or stopped without pausing the parent process and will reap all its child processes.
	*/
	while((pid = waitpid(-1,&status,WNOHANG|WUNTRACED)) > 0) 
	{		
		
		jid = pid2jid(pid);	/* obtain jid of the job from pid */
		if(jobs[jid-1].state == FG) 	/* if the is in foreground state */
			check_if_fg = 1;

		/* 	
			WIFEXITED checks if the job terminated normally. It is then deleted from the joblist.
		 */
		if(WIFEXITED(status)) 
		{
			deletejob(jobs,pid);
		}

		/*
			WIFSTOPPED checks if the job is stopped on receiving a signal. The state of the job is then changed to ST
		*/
		
		else if(WIFSTOPPED(status)) 
		{			
			getjobpid(jobs,pid)->state = ST;
			printf("Job [%d] (%d) stopped by signal %d\n", jid, pid, SIGTSTP);
		}
		
		/*
			WIFSIGNALED checks if the job terminated on receiving a signal. It is then deleted from the joblist.
		*/
		else if(WIFSIGNALED(status)) 
		{
			if(WTERMSIG(status) == SIGINT)	/* termination due to SIGINT signal */		
			{
				deletejob(jobs,pid);	
				printf("Job [%d] (%d) terminated by signal %d\n",jid,pid,SIGINT);		
			}
		}

	}
	return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
	pid_t pid = fgpid(jobs);	/* pid of foreground job */
	/* 
	SIGINT is sent to process group of the foreground job 
	*/
	if(kill(-pid, SIGINT) < 0)
		unix_error("kill error\n"); 

	return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
	pid_t pid = fgpid(jobs);	/* pid of foreground job */
        /* 
	SIGTSTP is sent to process group of the foreground job 
	*/
	if(kill(-pid,SIGTSTP) < 0)
		unix_error("kill error\n"); 
	//jobs[pid2jid(pid)-1].state = ST;	/* state of job is changed to stopped */	
	    
	return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
	printf("Terminating after receipt of SIGQUIT signal\n");
	exit(1);
}



