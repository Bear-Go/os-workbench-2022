
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <regex.h>
#include <time.h>
#include <dirent.h>

// #define __DEBUG__

extern char **environ;

struct info {
  char name[32];
  double time;
} syscall_info[1000];

int compare(const void *a, const void *b) {
  return ((struct info *)a)->time < ((struct info *)b)->time;
}

int syscall_num = 0;
double total_time = 0;

void add_info(char *name, double time) {
  total_time += time;
  for (int i = 0; i < syscall_num; ++i) {
    if (strcmp(name, syscall_info[i].name) == 0) {
      syscall_info[i].time += time;
      return;
    }
  }
  strcpy(syscall_info[syscall_num].name, name);
  syscall_info[syscall_num].time = time;
  syscall_num++;
  return;
}

void print_info() {
  qsort(syscall_info, syscall_num, sizeof(struct info), compare);
  printf("total time: %f\n", total_time);
  for (int i = 0; i < syscall_num && i < 5; ++i) {
    int percent = syscall_info[i].time / total_time * 100;
    printf("%s (%d%%)\n", syscall_info[i].name, percent);
  }
  for (int i = 0; i < 80; ++i) {
    putc('\0', stdout);
  }
  fflush(stdout);
}

int main(int argc, char *argv[]) {

#ifdef __DEBUG__
  printf("argc: %d\n", argc);
  for (int i = 0; i < argc; ++i) {
    printf("argv[%d]: %s\n", i, argv[i]);
  }
#endif

  // Get the arguments of strace 
  char **exec_argv = malloc(sizeof(char *) * (argc + 2));
  exec_argv[0] = "strace";
  exec_argv[1] = "-T";
  for (int i = 1; i < argc; ++i) {
    exec_argv[i + 1] = argv[i];
  }

#ifdef __DEBUG__
  for (int i = 0; i < argc + 2; ++i) {
    printf("exec_argv[%d]: %s\n", i, exec_argv[i]);
  }
#endif

  // Get the environment variables of strace
  char **exec_envp = environ;

  char *path = strdup(getenv("PATH"));
  
  // regex
  regex_t regex_name, regex_time;
  if (regcomp(&regex_name, "^[a-zA-Z_0-9]*\\(", REG_EXTENDED) != 0 || regcomp(&regex_time, "<[0-9]*\\.[0-9]*>", REG_EXTENDED) != 0) {
    // error handling
    perror("regcomp");
    exit(EXIT_FAILURE);
  }

  // Create the pipe
  int fildes[2];
  if (pipe(fildes) == -1) {
    // error handling
    perror("pipe");
    exit(EXIT_FAILURE);
  }

#ifdef __DEBUG__
  printf("%d %d\n", fildes[0], fildes[1]);
#endif

  // Create the child process
  pid_t pid = fork();
  if (pid == 0) {
    // Child process
    close(fildes[0]);
    dup2(fildes[1], STDERR_FILENO);
    int fd = open("/dev/null", O_WRONLY);
    dup2(fd, STDOUT_FILENO);
    char *dir = strtok(path, ":");
    while (dir != NULL) {
      char *exec_path = malloc(sizeof(char) * (strlen(dir) + strlen("/strace") + 1));
      strcpy(exec_path, dir);
      strcat(exec_path, "/strace");
      execve(exec_path, exec_argv, exec_envp);
      dir = strtok(NULL, ":");
    }
    perror("execve");
    fflush(stdout);
    exit(EXIT_FAILURE);
  } else {
    // Parent process
    close(fildes[1]);
    dup2(fildes[0], STDIN_FILENO);
    char buf[1024];


    regmatch_t matchname, matchtime;
    time_t old, new;
    old = time(NULL);
    char syscall_name[32];
    double syscall_time;
    while (fgets(buf, 1024, stdin)) {
      if (regexec(&regex_name, buf, 1, &matchname, 0) == 0 && regexec(&regex_time, buf, 1, &matchtime, 0) == 0) {
        strncpy(syscall_name, buf + matchname.rm_so, matchname.rm_eo - matchname.rm_so);
        syscall_name[matchname.rm_eo - matchname.rm_so - 1] = '\0';
        syscall_time = atof(buf + matchtime.rm_so + 1);
        add_info(syscall_name, syscall_time);
      }
      new = time(NULL);
      if (new - old >= 1) {
        print_info();
        old = new;
      }
    }
    print_info();
    regfree(&regex_name);
    regfree(&regex_time);
  }
  return 0;
}
