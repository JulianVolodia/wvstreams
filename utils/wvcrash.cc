/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 * 
 * Routines to generate a stack backtrace automatically when a program
 * crashes.
 */
#include "wvcrash.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#ifndef WVCRASH_USE_SIGALTSTACK
#define WVCRASH_USE_SIGALTSTACK 1
#endif

// FIXME: this file mostly only works in Linux
#ifdef __linux

# include <execinfo.h>
#include <unistd.h>

#ifdef __USE_GNU
static const char *argv0 = program_invocation_short_name;
#else
static const char *argv0 = "UNKNOWN";
#endif // __USE_GNU

#if WVCRASH_USE_SIGALTSTACK
static const size_t altstack_size = 1048576; // wvstreams can be a pig
static char altstack[altstack_size];
extern const void *__libc_stack_end;
#endif

// Reserve enough buffer for a screenful of programme.
static const int buffer_size = 2048 + wvcrash_ring_buffer_size;
static char desc[buffer_size];

// write a string 'str' to fd
static void wr(int fd, const char *str)
{
    write(fd, str, strlen(str));
}


// convert 'num' to a string and write it to fd.
static void wrn(int fd, int num)
{
    int tmp;
    char c;
    
    if (num < 0)
    {
	wr(fd, "-");
	num = -num;
    } 
    else if (num == 0)
    {
	wr(fd, "0");
	return;
    }
    
    tmp = 0;
    while (num > 0)
    {
	tmp *= 10;
	tmp += num%10;
	num /= 10;
    }
    
    while (tmp > 0)
    {
	c = '0' + (tmp%10);
	write(fd, &c, 1);
	tmp /= 10;
    }
}


// convert 'addr' to hex and write it to fd.
static void wra(int fd, const void *addr)
{
    char digits[] = "0123456789ABCDEF";
    
    write(fd, "0x", 2);
    for (int shift=28; shift>=0; shift-=4)
        write(fd, &digits[(((unsigned)addr)>>shift)&0xF], 1);
}


static void wvcrash_real(int sig, int fd, pid_t pid)
{
    static void *trace[64];
    static char *signame = strsignal(sig);
    
    wr(fd, argv0);
    if (desc[0])
    {
	wr(fd, " (");
	wr(fd, desc);
	wr(fd, ")");
    }
    wr(fd, " dying on signal ");
    wrn(fd, sig);
    if (signame)
    {
	wr(fd, " (");
	wr(fd, signame);
	wr(fd, ")\n");
    }

    // Write out the PID and PPID.
    static char pid_str[32];
    wr(fd, "\nProcess ID: ");
    snprintf(pid_str, sizeof(pid_str), "%d", getpid());
    pid_str[31] = '\0';
    wr(fd, pid_str);
    wr(fd, "\nParent's process ID: ");
    snprintf(pid_str, sizeof(pid_str), "%d", getppid());
    pid_str[31] = '\0';
    wr(fd, pid_str);
    wr(fd, "\n");

#if WVCRASH_USE_SIGALTSTACK
    // Determine if this has likely been a stack overflow
    const void *last_real_stack_frame;
    for (;;)
    {
        last_real_stack_frame = __builtin_frame_address(0);
        if (last_real_stack_frame == NULL
                || last_real_stack_frame < &altstack[0]
                || last_real_stack_frame >= &altstack[altstack_size])
            break;
        last_real_stack_frame = __builtin_frame_address(1);
        if (last_real_stack_frame == NULL
                || last_real_stack_frame < &altstack[0]
                || last_real_stack_frame >= &altstack[altstack_size])
            break;
        last_real_stack_frame = __builtin_frame_address(2);
        if (last_real_stack_frame == NULL
                || last_real_stack_frame < &altstack[3]
                || last_real_stack_frame >= &altstack[altstack_size])
            break;
        last_real_stack_frame = __builtin_frame_address(4);
        if (last_real_stack_frame == NULL
                || last_real_stack_frame < &altstack[0]
                || last_real_stack_frame >= &altstack[altstack_size])
            break;
        last_real_stack_frame = __builtin_frame_address(5);
        if (last_real_stack_frame == NULL
                || last_real_stack_frame < &altstack[0]
                || last_real_stack_frame >= &altstack[altstack_size])
            break;
        last_real_stack_frame = NULL;
        break;
    }
    if (last_real_stack_frame != NULL)
    {
        wr(fd, "\nLast real stack frame: ");
        wra(fd, last_real_stack_frame);
        wr(fd, "\nTop of stack: ");
        wra(fd, __libc_stack_end);
        rlim_t stack_size = rlim_t(__libc_stack_end) - rlim_t(last_real_stack_frame);
        wr(fd, "\nStack size: ");
        wrn(fd, int(stack_size));
        struct rlimit rl;
        if (getrlimit(RLIMIT_STACK, &rl) == 0)
        {
            wr(fd, "\nStack size rlimit: ");
            wrn(fd, int(rl.rlim_cur));
            if (stack_size > rl.rlim_cur)
                wr(fd, "  DEFINITE STACK OVERFLOW");
            else if (stack_size > rl.rlim_cur * 95 / 100)
                wr(fd, "  PROBABLE STACK OVERFLOW");
        }
        wr(fd, "\n");
    }
#endif
                

    // Write out the contents of the ring buffer
    {
        const char *ring;
        bool first = true;
        while ((ring = wvcrash_ring_buffer_get()) != NULL)
        {
            if (first)
            {
                first = false;
                wr(fd, "\nRing buffer:\n");
            }
            wr(fd, ring);
        }
    }
    
    // Write out the assertion message, as logged by __assert*_fail(), if any.
    {
	const char *assert_msg = wvcrash_read_assert();
	if (assert_msg && assert_msg[0])
	{
	    wr(fd, "\nAssert:\n");
	    wr(fd, assert_msg);
	}
    }

    // Write out the note, if any.
    {
	const char *will_msg = wvcrash_read_will();
	if (will_msg && will_msg[0])
	{
	    wr(fd, "\nLast Will and Testament:\n");
	    wr(fd, will_msg);
	    wr(fd, "\n");
	}
    }

    wr(fd, "\nBacktrace:\n");
    backtrace_symbols_fd(trace,
		 backtrace(trace, sizeof(trace)/sizeof(trace[0])), fd);
    
    if (pid > 0)
    {
        // Wait up to 10 seconds for child to write wvcrash file in case there
        // is limited space availible on the device; wvcrash file is more
        // useful than core dump
        int i;
        struct timespec ts = { 0, 100*1000*1000 };
        close(fd);
        for (i=0; i < 100; ++i)
        {
            if (waitpid(pid, NULL, WNOHANG) == pid)
                break;
            nanosleep(&ts, NULL);
        }
    }

    // we want to create a coredump, and the kernel seems to not want to do
    // that if we send ourselves the same signal that we're already in.
    // Whatever... just send a different one :)
    if (sig == SIGABRT)
	sig = SIGBUS;
    else if (sig != 0)
	sig = SIGABRT;
   
    signal(sig, SIG_DFL);
    raise(sig);
}


// Hint: we can't do anything really difficult here, because the program is
// probably really confused.  So we should try to limit this to straight
// kernel syscalls (ie. don't fiddle with FILE* or streams or lists, just
// use straight file descriptors.)
// 
// We fork a subprogram to do the fancy stuff like sending email.
// 
void wvcrash(int sig)
{
    int fds[2];
    pid_t pid;

    signal(sig, SIG_DFL);
    wr(2, "\n\nwvcrash: crashing!\n");
    
    // close some fds, just in case the reason we're crashing is fd
    // exhaustion!  Otherwise we won't be able to create our pipe to a
    // subprocess.  Probably only closing two fds is possible, but the
    // subproc could get confused if all the fds are non-close-on-exec and
    // it needs to open a few files.
    // 
    // Don't close fd 0, 1, or 2, however, since those might be useful to
    // the child wvcrash script.  Also, let's skip 3 and 4, in case someone
    // uses them for something.  But don't close fd numbers that are *too*
    // big; if someone ulimits the number of fds we can use, and *that's*
    // why we're crashing, there's no guarantee that high fd numbers are in
    // use even if we've run out.
    for (int count = 5; count < 15; count++)
	close(count);
    
    if (pipe(fds))
	wvcrash_real(sig, 2, 0); // just use stderr instead
    else
    {
	pid = fork();
	if (pid < 0)
	    wvcrash_real(sig, 2, 0); // just use stderr instead
	else if (pid == 0) // child
	{
	    close(fds[1]);
	    dup2(fds[0], 0); // make stdin read from pipe
	    fcntl(0, F_SETFD, 0);
	    
	    execlp("wvcrash", "wvcrash", NULL);
	    
	    // if we get here, we couldn't exec wvcrash
	    wr(2, "wvcrash: can't exec wvcrash binary "
	       "- writing to wvcrash.txt!\n");
	    execlp("dd", "dd", "of=wvcrash.txt", NULL);
	    
	    wr(2, "wvcrash: can't exec dd to write to wvcrash.txt!\n");
	    _exit(127);
	}
	else if (pid > 0) // parent
	{
	    close(fds[0]);
	    wvcrash_real(sig, fds[1], pid);
	}
    }
    
    // child (usually)
    _exit(126);
}


static void wvcrash_setup_alt_stack()
{
#if WVCRASH_USE_SIGALTSTACK
    stack_t ss;
    
    ss.ss_sp = altstack;
    ss.ss_flags = 0;
    ss.ss_size = altstack_size;
    
    if (ss.ss_sp == NULL || sigaltstack(&ss, NULL))
        fprintf(stderr, "Failed to setup sigaltstack for wvcrash: %s\n",
                strerror(errno)); 
#endif //WVCRASH_USE_SIGALTSTACK
}

void wvcrash_add_signal(int sig)
{
#if WVCRASH_USE_SIGALTSTACK
    struct sigaction act;
    
    act.sa_handler = wvcrash;
    sigfillset(&act.sa_mask);
    act.sa_flags = SA_ONSTACK | SA_RESTART;
    act.sa_restorer = NULL;
    
    if (sigaction(sig, &act, NULL))
        fprintf(stderr, "Failed to setup wvcrash handler for signal %d: %s\n",
                sig, strerror(errno));
#else //!WVCRASH_USE_SIGALTSTACK
    signal(sig, wvcrash);
#endif //WVCRASH_USE_SIGALTSTACK
}

// Secret symbol for initialising the will and assert buffers
extern void __wvcrash_init_buffers(const char *program_name);

void wvcrash_setup(const char *_argv0, const char *_desc)
{
    if (_argv0)
	argv0 = basename(_argv0);
    __wvcrash_init_buffers(argv0);
    if (_desc)
    {
	strncpy(desc, _desc, buffer_size);
	desc[buffer_size - 1] = '\0';
    }
    else
	desc[0] = '\0';
    
    wvcrash_setup_alt_stack();
    
    wvcrash_add_signal(SIGSEGV);
    wvcrash_add_signal(SIGBUS);
    wvcrash_add_signal(SIGABRT);
    wvcrash_add_signal(SIGFPE);
    wvcrash_add_signal(SIGILL);
}

#else // Not Linux

void wvcrash(int sig) {}
void wvcrash_add_signal(int sig) {}
void wvcrash_setup(const char *_argv0, const char *_desc) {}

#endif // Not Linux
