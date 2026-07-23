
#include "os_utils.h"

#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <error.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <limits.h>

#include "logger.h"

int set_process_limits(size_t coredump_filesz_limit, size_t fd_limit)
{
  // core dump filesz
  {
    struct rlimit rlim;
    memset(&rlim, 0, sizeof(rlim));

    if (getrlimit(RLIMIT_CORE, &rlim) < 0)
      return -1;

    if (rlim.rlim_cur >= coredump_filesz_limit)
      return 0;
    rlim.rlim_cur = coredump_filesz_limit;
    rlim.rlim_max = coredump_filesz_limit;
    if (setrlimit(RLIMIT_CORE, &rlim) < 0)
      return -1;
  }

  // file descriptor limits (epoll & some network connections use these)
  {
    struct rlimit rlim;
    memset(&rlim, 0, sizeof(rlim));
    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
      return -1;

    if (rlim.rlim_cur >= fd_limit)
      return 0;
    rlim.rlim_cur = fd_limit;
    rlim.rlim_max = fd_limit;
    if (setrlimit(RLIMIT_NOFILE, &rlim) < 0)
      return -1;
  }

  return 0;
}

int process_exist(const char* fmt, ...)
{
  char name[64] = {};
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(name, sizeof(name), fmt, ap);
  va_end(ap);

  char path[PATH_MAX] = {};
  snprintf(path, sizeof(path), "/tmp/%s.lock", name);

  int fd = open(path, O_CREAT, 400);
  if (fd < 0)
    return -1;
  if (flock(fd, LOCK_EX | LOCK_NB) < 0)
    return 1;

  return 0;
}


int gSignalExit = 0;

static void signal_handler(int signo);

int init_signals()
{
  //@reference: https://man7.org/linux/man-pages/man7/signal.7.html
  auto setup_signal = [](int signalno, bool reset_handler, void (*handler_func)(int))
  {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_func;
    sa.sa_flags = reset_handler ? SA_RESETHAND : 0;
    sigemptyset(&sa.sa_mask);
    int ret = sigaction(signalno, &sa, NULL);
    if (ret < 0)
    {
      // log_vip("failed to set signal %d", signalno);
      return -1;
    }
    return 0;
  };

  // search the signal defines on the man page for a description
  setup_signal(SIGUSR1, false, signal_handler);
  setup_signal(SIGTTIN, false, signal_handler);
  setup_signal(SIGTTOU, false, signal_handler);  // terminal output
  setup_signal(SIGQUIT, false, signal_handler);
  setup_signal(SIGTERM, false, signal_handler);
  setup_signal(SIGSEGV, true, signal_handler);  // segfault
  setup_signal(SIGABRT, true, signal_handler);  // abort
  setup_signal(SIGPIPE, false, nullptr);        // broken pipe
  setup_signal(SIGCHLD, false, nullptr);        // child stopped/terminated/continued
  setup_signal(SIGINT, false, nullptr);
  return 0;
}

void signal_handler(int signo)
{
  if (signo == SIGUSR1)
  {
    // log_vip("[%d]signal: %d (%s) received", getpid(), signo, sig->signame);
    // signal_reload = 1;
  }
  else if (signo == SIGTTIN)
  {
    // dlog_level_up();
    // log_vip("[%d]signal: %d (%s) received, logging level up: %#x", getpid(), signo, sig->signame,
    //         default_dlog_flag);
  }
  else if (signo == SIGTTOU)
  {
    // dlog_level_down();
    // log_vip("[%d]signal: %d (%s) received, logging level down: %#x", getpid(), signo,
    // sig->signame,
    //         default_dlog_flag);
  }
  else if (signo == SIGQUIT)
  {
    // log_vip("[%d]signal: %d (%s) received, exiting", getpid(), signo, sig->signame);
    gSignalExit = 1;
  }
  else if (signo == SIGTERM)
  {
    // log_vip("[%d]signal: %d (%s) received, exiting", getpid(), signo, sig->signame);
    gSignalExit = 1;
  }
  else if (signo == SIGSEGV)
  {
    // log_exception("[%d]signal: %d (%s) received, core dumping", getpid(), signo, sig->signame);
    //     dlog_flush_all();
    //     raise(SIGSEGV);
  }
  else if (signo == SIGUSR1)
  {
    // log_exception("[%d]signal: %d (%s) received, core dumping", getpid(), signo, sig->signame);
    // dlog_flush_all();
    // raise(SIGABRT);
  }
  else
  {
    // log_vip("[%d]signal: %d (%s) received", getpid(), signo, sig->signame);
  }
}

int fork_process_and_keepalive()
{
  // forks the process. the child process will carry on, while the parent process observes the child
  // process
  while (true)
  {
    int pid = fork();
    if (pid < 0)
    {
      error(EXIT_FAILURE, 1, "Fork failed %d", pid);
      return -1;
    }
    else if (pid == 0)
    {
      // child process, initialize signals
      init_signals();
      return 0;
    }
    else
    {
      // parent process, wait for child's signal, if it was terminated, restart it (via looping to
      // fork() call) otherwise break/exit accordingly
      init_signals();
      // ignore/default these uneeded signals for parent
      signal(SIGCHLD, SIG_DFL);
      signal(SIGUSR1, SIG_IGN);
      signal(SIGTTIN, SIG_IGN);
      signal(SIGTTOU, SIG_IGN);

      int status = 0;
      int ret = waitpid(pid, &status, 0);
      if (ret < 0)
      {
        // errors
        if (gSignalExit)
          exit(EXIT_SUCCESS);
        else
        {
          // log_error("waitpid: %d error: %d: %s", pid, ret, strerror(errno));
          exit(EXIT_FAILURE);
        }
      }

      if (WIFEXITED(status))
        exit(EXIT_SUCCESS);
      else if (WIFSIGNALED(status))
      {
        // log_fatal("process: %d, name: %s terminated by signal: '%s'", pid,
        //           program_invocation_short_name, strsignal(WTERMSIG(status)));
        usleep(1000 * 1000);
        continue;
      }
      else
      {
        // log_error("process: %d terminated, waitpid status: %d\n", pid, status);
        exit(EXIT_FAILURE);
      }
    }
  }
}