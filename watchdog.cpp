#include "cpuuse.h"
#include "maile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <assert.h>
#include <getopt.h>
#include <initializer_list>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <iostream>
#include <string>
#include <memory>
#include <pwd.h>
#include <grp.h>
#include <algorithm>

#define HAVE_SYS_PRCTL_H 


#define SUPERVISOR_URL ""
#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif


class Logger {  
 public:
  Logger(const char* basename, int logLevel)
      : basename_(basename), logLevel_(logLevel) {}

  void setLogLevel(int level) 
  { 
	  logLevel_ = level; 
  }

	template <typename... Args>
	void error(const char* fmt, Args... args) 
	{
    	if (logLevel_ >= 1) 
		{
		  std::string fmt2("%s[%d]: ");
		  fmt2 += fmt;
		  fmt2 += "\n";
		  fprintf(stderr, fmt2.c_str(), basename_.c_str(), getpid(), args...);
		  fflush(stderr);
		}
	}

	template <typename... Args>
	void info(const char* fmt, Args... args) 
	{
		if (logLevel_ >= 2) 
		{
		  std::string fmt2("%s[%d]: ");
		  fmt2 += fmt;
		  fmt2 += "\n";
		  fprintf(stdout, fmt2.c_str(), basename_.c_str(), getpid(), args...);
		  fflush(stdout);
		}
	}

private:
	  const std::string basename_;
	  int logLevel_;
};

class PidTracker 
{  
	public:
	  PidTracker();
	  ~PidTracker();

	  void setMainExe(const std::string& exe) { mainExe_ = exe; }

	  void add(int pid);
	  std::vector<int> collectAll();
	  int findMainPID(const std::string& mainPidFileHint);

	  void dump(const char* msg);

	 private:
	  std::string mainExe_;
};

PidTracker::PidTracker() 
{
	  char path[80];
	  snprintf(path, sizeof(path), "/sys/fs/cgroup/cpu/%d.supervisor", getpid());
	  int rv = mkdir(path, 0777);

	  if (rv < 0) 
	  {
		perror("PidTracker: mkdir");
	  }
}

PidTracker::~PidTracker() 
{
	  char path[80];
	  snprintf(path, sizeof(path), "/sys/fs/cgroup/cpu/%d.supervisor", getpid());
	  rmdir(path);
}

void PidTracker::add(int pid) 
{
	  char path[80];
	  snprintf(path, sizeof(path), "/sys/fs/cgroup/cpu/%d.supervisor/tasks",
			   getpid());

	  char buf[64];
	  ssize_t n = snprintf(buf, sizeof(buf), "%d", pid);

	  int fd = open(path, O_WRONLY);
	  write(fd, buf, n);
	  close(fd);
}

std::vector<int> PidTracker::collectAll() 
{
	  std::vector<int> result;

	  char path[80];
	  snprintf(path, sizeof(path), "/sys/fs/cgroup/cpu/%d.supervisor/tasks",
			   getpid());

	  std::ifstream tasksFile(path);
	  std::string line;

	  while (std::getline(tasksFile, line)) 
	  {
		result.push_back(stoi(line));
	  }

	  return result;
}

/***
 * Retrieves the parent-PID of a given process or 0 on error.
*/
static pid_t getppid(pid_t pid) 
{
	  char statfile[64];
	  snprintf(statfile, sizeof(statfile), "/proc/%d/stat", pid);
	  FILE* fp = fopen(statfile, "r");
	  if (!fp) 
		  return 0;

	  int pid0;
	  char comm[16];  // definitely below 16
	  char state;
	  int ppid;

	  fscanf(fp, "%d %s %c %d", &pid0, comm, &state, &ppid);

	  return ppid;
}

	/**
	 * Retrieves absolute path to the executable of a given PID.
	 */
static std::string getExe(pid_t pid) 
{
	  char path[11 + 16];
	  snprintf(path, sizeof(path), "/proc/%d/exe", pid);

	  char buf[4096];
	  ssize_t n = readlink(path, buf, sizeof(buf) - 1);

	  if (n > 0) 
	  {
		return std::string(buf, n);
	  } 
	  else 
	  {
		return std::string();
	  }
}

int PidTracker::findMainPID(const std::string& mainPidFileHint) 
{
	  std::vector<int> candidates;

	  if (!mainPidFileHint.empty()) 
	  {
		std::ifstream ifs(mainPidFileHint);
		std::string pids;
		ifs >> pids;
		printf("PidTracker: main-PID hint: %s\n", pids.c_str());
		int pid = std::stoi(pids);
		if (pid != 0) 
		{
		  return pid;
		}
	  }

	  for (int pid : collectAll()) 
	  {
		if (getppid(pid) == getpid()) 
		{
		  if (getExe(pid) == mainExe_) 
		  {
			candidates.push_back(pid);
		  }
		}
	  }

	  return !candidates.empty() ? candidates.front() : 0;
}

	void PidTracker::dump(const char* msg) 
	{
#if 0  
		  assert(msg && *msg);
		  printf("PID tracking dump (%s): ", msg);

		  for (int pid : collectAll()) 
		  {
			printf(" %d", pid);
		  }
		  printf("\n");
#endif
	}

	class Program 
	{  
		 public:
		  Program(Logger* logger, const std::string& exe,
				  const std::vector<std::string>& argv, const std::string& mainPidfile);
		  ~Program();

		  bool start();
		  bool restart();
		  bool resume();
		  void signal(int signo);

		  int pid() const { return pid_; }

		 private:
		  bool spawn();

		 private:
		  Logger* logger_;
		  std::string exe_;
		  std::vector<std::string> argv_;
		  std::string mainPidfile_;
		  int pid_;
		  PidTracker pidTracker_;
	};

	Program::Program(Logger* logger, const std::string& exe,
						 const std::vector<std::string>& argv,
						 const std::string& mainPidfile)
			: logger_(logger),
			  exe_(exe),
			  argv_(argv),
			  mainPidfile_(mainPidfile),
			  pid_(0),
			  pidTracker_() 
	{
		  pidTracker_.setMainExe(exe);
	}

	Program::~Program() {}

	bool Program::start() 
	{
		  return spawn();
	}

	bool Program::resume() 
	{
		  pidTracker_.dump("resume");

		  if (pid_t pid = pidTracker_.findMainPID(mainPidfile_)) 
		  {
			logger_->info("new main pid: %d", pid);
			pid_ = pid;
			return true;
		  }

		  const auto pids = pidTracker_.collectAll();
		  if (!pids.empty()) 
		  {
			pid_ = pids[0];
			return true;
		  }

		  return false;
	}

	bool Program::restart() 
	{
		  return spawn();
	}

	void Program::signal(int signo)
	{
		  // just send signal to PID
		  kill(pid_, signo);
	}

	bool Program::spawn() 
	{
	  logger_->info("starting program (%s)...", exe_.c_str());

	  pid_t pid = fork();

	  if (pid < 0) 
	  {
		logger_->error("fork failed. %s", strerror(errno));
		return false;
	  } 
	  else if (pid > 0) 
	  {  // parent
		pid_ = pid;
		pidTracker_.add(pid);
		logger_->info("child pid is %d", pid);
		return true;
	  } 
	  else 
	  {  // child
		std::vector<char*> argv;

		for (const std::string& arg : argv_) 
		{
		  argv.push_back(const_cast<char*>(arg.c_str()));
		}
		argv.push_back(nullptr);

		execvp(exe_.c_str(), argv.data());
		logger_->error("execvp failed. %s", strerror(errno));
		abort();
	  }
}

class Supervisor { 
	 public:
	  Supervisor();
	  ~Supervisor();

	  int run(int argc, char* argv[]);

	  static Supervisor* self() { return self_; }

	 private:
	  bool isExitSuccess(int code) const;
	  bool isExitSuccess() const;
	  bool parseArgs(int argc, char* argv[]);
	  void printHelp();
	  bool restart();
	  static void sighandler(int signum);


	  Logger* logger() { return &logger_; }

	 private:
	  static Supervisor* self_;
	  Logger logger_;
	  std::unique_ptr<Program> program_;
	  std::string pidfile_;
	  std::string mainPidfile_;
	  int restartCount_;       //!< number of actual restarts so far
	  int restartDelay_;       //!< current restart delay
	  int restartDelayLimit_;  //!< restart delay limit

	  int restartOnError_;  //!< restart app on normal exit but code != 0
	  int restartOnCrash_;  //!< restart app on SIGSEGV
	  bool fork_;
	  int exitCode_;
	  std::vector<int> successExitCodes_;
};

Supervisor* Supervisor::self_ = nullptr;

Supervisor::Supervisor()
		: logger_("supervisor", 2),
		  program_(nullptr),
		  pidfile_(),
		  mainPidfile_(),
		  restartCount_(0),        // number of actual restarts
		  restartDelay_(0),        // do not wait during restarts
		  restartDelayLimit_(30),  // exponential backup delay cap
		  restartOnError_(false),
		  restartOnCrash_(false),
		  fork_(false),
		  exitCode_(0),
		  successExitCodes_() 
	{
	  assert(self_ == nullptr);
	  self_ = this;
	  successExitCodes_.push_back(EXIT_SUCCESS);
	}

	Supervisor::~Supervisor() 
	{
	  self_ = nullptr;

	  if (!pidfile_.empty()) 
	  {
		unlink(pidfile_.c_str());
	  }
	}

	bool Supervisor::parseArgs(int argc, char* argv[]) 
	{
	  if (argc <= 1) 
	  {
		printHelp();
		return false;
	  }

	  struct option opts[] = {
		  					  {"fork",no_argument,nullptr,'f'},
							  {"delay-limit", required_argument, nullptr, 'l'},
							  {"restart-on-error", no_argument, nullptr, 'e'},
							  {"restart-on-crash", no_argument, nullptr, 'c'},
							  {0, 0, 0, 0}
							  };

	  int logLevel = 2;

	  for (;;) 
	  {
		int long_index = 0;
		switch (getopt_long(argc, argv, "fp:l:fec", opts, &long_index)) 
		{
			case 'f':
				fork_= true;
				break;
		  	case 'l':
			// TODO: ensure optarg is a number
				restartDelayLimit_ = atoi(optarg);
        		break;
      		case 'e':
        		restartOnError_ = true;
        		break;
      		case 'c':
        		restartOnCrash_ = true;
        		break;
      		case 0:  // long option with (val!=nullptr && flag=0)
        		break;
      		case -1: {
        // EOF - everything parsed.
        		if (optind == argc) 
				{
          			logger()->error("no program path given");
          			return false;
        		}

        std::vector<std::string> args;
        while (optind < argc) 
		{
          args.push_back(argv[optind++]);
        }

        if (args[0].empty() || args[0][0] != '/') 
		{
          logger()->error("program path must be absolute.");
          return false;
        }

        if (getuid() && getuid()) 
		{
          logger()->error("Must run as (setuid) root. Please fix permissions.");
          return false;
        }

        if (fork_) 
		{
          int rv = daemon(1, 1);   //成功为0  失败为-1
          if (rv < 0) 
		  {
            logger()->error("Could not daemonize into background. %s",
                            strerror(errno));

            return false;
          }
        }

        logger_.setLogLevel(logLevel);

        program_.reset(new Program(logger(), args[0], args, mainPidfile_));

        auto signals = {SIGINT,  SIGQUIT, SIGTERM, SIGCONT,
                        SIGUSR1, SIGUSR2, SIGTTIN, SIGTTOU};

        for (int sig : signals) 
		{
          signal(sig, &Supervisor::sighandler);
        }

        return true;
      }
      case '?':  // ambiguous match / unknown arg
      default:
        return false;
    }
  }
  return true;
}


void Supervisor::printHelp() 
{
  printf(
      "watchdog: process supervising tool.\n"

	  "  -f,--fork              daemon process\n"
      "  -l,--delay-limit=N     max sleep time(no more than 30s)\n"
      "  -e,--restart-on-error  restart exitcode!=0\n"
      "  -c,--restart-on-crash  restart crash (SIGSEGV)\n"
      "\n"
      "Examples:\n"
      "    ./a.out -c /home/wyf1/linux_wdog/Test/test\n"
      "    ./a.out -e /home/wyf1/linux_wdog/Test/ll\n"
      "\n");
}

int Supervisor::run(int argc, char* argv[]) 
{
  if (!parseArgs(argc, argv)) 
  {
    return EXIT_FAILURE;
  }

  if (!pidfile_.empty()) 
  {
    logger()->info("writing watchdog-PID %d to %s", getpid(),
                   pidfile_.c_str());

    std::ofstream fs(pidfile_, std::ios_base::out | std::ios_base::trunc);
    fs << getpid() << std::endl;
  }

  program_->start();
  for (;;) 
  {
	cpuuse();
    int status = 0;
    pid_t pid = waitpid(-1, &status, 0);
    if (pid < 0) 
	{
      perror("waitpid");
      return EXIT_FAILURE;
    }

    if (pid != program_->pid()) 
	{
      logger()->info("Reaping child PID %d", pid);
      continue;
    }

    if (WIFEXITED(status)) 
	{
      exitCode_ = WEXITSTATUS(status);

      logger()->info("program PID %d terminated normally with exit code %d",
                     program_->pid(), exitCode_);

      if (program_->resume()) 
	  {
        logger()->info("reattaching to child PID %d.", program_->pid());
        continue;
      }

      if (!isExitSuccess() && restartOnError_) {
        logger()->info("restarting due to error code %d", exitCode_);

        if (restart())
		{
			continue;
		}
      }

      logger()->info("shutting down watchdog with application exit code %d",
                     exitCode_);

      return exitCode_;
    }

    if (WIFSIGNALED(status)) {
      int sig = WTERMSIG(status);
      logger()->info("Child %d terminated with signal '%s' (%d)",
                     program_->pid(), strsignal(sig), sig);

      // do only attempt to restart if it's none of those signals.
      static const int sigs[] = {SIGTERM, SIGINT, SIGQUIT};

      bool softTerminate =
          std::find(std::begin(sigs), std::end(sigs), sig) != std::end(sigs);

      if (!softTerminate && restartOnCrash_ && restart()) 
		  continue;

      return exitCode_;
    }


    if (restart())
	{
		continue;
	}

    return exitCode_;
  }
}

bool Supervisor::isExitSuccess(int value) const 
{
  for (int code: successExitCodes_)
    if (value == code)
      return true;

  return false;
}

bool Supervisor::isExitSuccess() const 
{
  return isExitSuccess(exitCode_);
}

bool Supervisor::restart() 
{
  if (restartDelay_) 
  {
    logger()->info("restart is sleeping for %d seconds\n", restartDelay_);
    sleep(restartDelay_);

    // exponential backoff for the next restart
    restartDelay_ = std::min(restartDelay_ << 1, restartDelayLimit_);
  } 
  else 
  {
    restartDelay_ = 1;
	cout<<endl;
  }

  restartCount_++;
  if(restartCount_%3==0)
  {
	  run_mail();
  }
  return program_->restart();
}

void Supervisor::sighandler(int signum) 
{
  if (self()->program_->pid()) 
  {
    self()->logger()->info(
        "Signal '%s' (%d) received. Forwarding to child PID %d.",
        strsignal(signum), signum, self()->program_->pid());

    // forward to child process
    self()->program_->signal(signum);
  }
}

int main(int argc, char* argv[]) 
{
  Supervisor supervisor;
  return supervisor.run(argc, argv);
}
