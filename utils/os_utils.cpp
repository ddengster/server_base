
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
  //@note: libuv's version of the handler runs during uv_run(), this is asynchronous
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
      LOG_WARN("failed to set signal %d", signalno);
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
    LOG_WARN("[%d]signal: %d SIGUSR1 received", getpid(), signo);
    // signal_reload = 1;
  }
  else if (signo == SIGTTIN)
  {
    LOG_WARN("[%d]signal: %d (SIGTTIN) received", getpid(), signo);
  }
  else if (signo == SIGTTOU)
  {
    LOG_WARN("[%d]signal: %d (SIGTTOU) received", getpid(), signo);
  }
  else if (signo == SIGQUIT)
  {
    LOG_FATAL("[%d]signal: %d (SIGQUIT) received, exiting", getpid(), signo);
    gSignalExit = 1;
  }
  else if (signo == SIGTERM)
  {
    LOG_FATAL("[%d]signal: %d (SIGTERM) received, exiting", getpid(), signo);
    gSignalExit = 1;
  }
  else if (signo == SIGSEGV)
  {
    LOG_ERROR("[%d]signal: %d (SIGSEGV) received, core dumping", getpid(), signo);
    log_backtrace();
    log_flush();
    raise(SIGSEGV);
  }
  else if (signo == SIGABRT)
  {
    LOG_ERROR("[%d]signal: %d (SIGABRT) received, core dumping", getpid(), signo);
    log_backtrace();
    log_flush();
    raise(SIGABRT);
  }
  else
  {
    LOG_WARN("[%d]signal: %d received", getpid(), signo);
  }
}

int fork_process_and_keepalive()
{
  // forks the process. the child process will carry on, while the parent process observes the child
  // process
  uint restart_count = 0;
#ifdef DEVELOPER_BUILD
  while (restart_count < 5)
#else
  while (true)
#endif
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
      log_childprocess_init();
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
          LOG_ERROR("waitpid: %d error: %d: %s", pid, ret, strerror(errno));
          exit(EXIT_FAILURE);
        }
      }

      if (WIFEXITED(status))
        exit(EXIT_SUCCESS);
      else if (WIFSIGNALED(status))
      {
        LOG_FATAL("process: %d, name: %s terminated by signal: '%s'", pid,
                  program_invocation_short_name, strsignal(WTERMSIG(status)));
        usleep(1000 * 1000);
        ++restart_count;
        continue;
      }
      else
      {
        LOG_ERROR("process: %d terminated, waitpid status: %d\n", pid, status);
        exit(EXIT_FAILURE);
      }
    }
  }
  return 0;
}

extern char** environ;

static char* gTitleBase = nullptr;
static char* gTitleTail = nullptr;

void process_title_init(int argc, char* argv[])
{
  if (gTitleBase)
    return;

  char* base = argv[0];
  char* tail = argv[argc - 1] + strlen(argv[argc - 1]) + 1;

  char** envp = environ;
  for (int i = 0; envp[i]; ++i)
  {
    if (envp[i] < tail)
      continue;
    tail = envp[i] + strlen(envp[i]) + 1;
  }

  /* dup program name */
  program_invocation_name = strdup(program_invocation_name);
  program_invocation_short_name = strdup(program_invocation_short_name);

  /* dup argv */
  for (int i = 0; i < argc; ++i)
    argv[i] = strdup(argv[i]);

  /* dup environ */
  clearenv();
  for (int i = 0; envp[i]; ++i)
  {
    char* eq = strchr(envp[i], '=');
    if (eq == NULL)
      continue;

    *eq = '\0';
    setenv(envp[i], eq + 1, 1);
    *eq = '=';
  }

  gTitleBase = base;
  gTitleTail = tail;
  memset(gTitleBase, 0, gTitleTail - gTitleBase);
}

void process_title_set(const char* fmt, ...)
{
  if (!gTitleBase)
    return;

  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  int title_len = len < (gTitleTail - gTitleBase) ? len : (gTitleTail - gTitleBase - 1);
  memcpy(gTitleBase, buf, title_len);
  gTitleBase[title_len] = '\0';

  return;
}