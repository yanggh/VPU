#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "vpu.h"

static volatile sig_atomic_t sigflag = 0;




static void SignalHandlerSigusr1 (__attribute__((unused))int signo) {
    sigflag = 1;
}



/**
 * \brief Tell the parent process the child is ready
 *
 * \param pid pid of the parent process to signal
 */
static void TellWaitingParent (pid_t pid) {
    kill(pid, SIGUSR1);
}

/**
 * \brief Set the parent on stand-by until the child is ready
 *
 * \param pid pid of the child process to wait
 */
static void WaitForChild (pid_t pid) {
    int status;
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "Daemon: Parent waiting for child to be ready...");
    /* Wait until child signals is ready */
    while (sigflag == 0) {
        if (waitpid(pid, &status, WNOHANG)) {
            /* Check if the child is still there, otherwise the parent should exit */
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "Child died unexpectedly");
                exit(EXIT_FAILURE);
            }
        }
        /* sigsuspend(); */
        sleep(1);
    }
}
/**
 * \brief Close stdin, stdout, stderr.Redirect logging info to syslog
 *
 */
static void SetupLogging (void) {
    /* Redirect stdin, stdout, stderr to /dev/null  */
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0)
        return;
    if (dup2(fd, 0) < 0)
        return;
    if (dup2(fd, 1) < 0)
        return;
    if (dup2(fd, 2) < 0)
        return;
    close(fd);
}

/**
 * \brief Daemonize the process
 *
 */
void daemonize (void) {
    pid_t pid, sid;

    signal(SIGUSR1, SignalHandlerSigusr1);
    /** \todo We should check if wie allow more than 1 instance
              to run simultaneously. Maybe change the behaviour
              through conf file */

    /* Creates a new process */
    pid = fork();

    if (pid < 0) {
        /* Fork error */
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "Error forking the process");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        /* Child continues here */
        umask(027);

        sid = setsid();
        if (sid < 0) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "Error creating new session");
            exit(EXIT_FAILURE);
        }

        if (chdir("/") < 0) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "Error changing to working directory '/'");
        }

        SetupLogging();

        /* Child is ready, tell its parent */
        TellWaitingParent(getppid());

        /* Daemon is up and running */
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "Daemon is running");
        return;
    }
    /* Parent continues here, waiting for child to be ready */
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "Parent is waiting for child to be ready");
    WaitForChild(pid);

    /* Parent exits */
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "Child is ready, parent exiting");
    exit(EXIT_SUCCESS);
}


