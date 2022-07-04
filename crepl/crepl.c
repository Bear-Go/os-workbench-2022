#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dlfcn.h>

// #define __DEBUG__

int main(int argc, char *argv[]) {
  static char line[4096];
  while (1) {
    printf("crepl> ");
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin)) {
      break;
    }
    line[strlen(line) - 1] = '\0';
#ifdef __DEBUG__
    printf("Got %zu chars.\n", strlen(line)); // ??
#endif

    // function or expression
    int is_func = strncmp(line, "int", 3) == 0;
#ifdef __DEBUG__
    printf("is_func: %d\n", is_func);
#endif

    // create temporary file
    char src_name[32] = "/tmp/src_XXXXXX";
    char dst_name[32] = "/tmp/dst_XXXXXX";
    if (mkstemp(src_name) == -1 || mkstemp(dst_name) == -1) {
      perror("mkstemp");
      return 1;
    }
#ifdef __DEBUG__
    printf("src_name: %s\n", src_name);
    printf("dst_name: %s\n", dst_name);
#endif

    // set arguments
    char *exec_argv[] = { "gcc", "-m32", "-x", "c", "-w", "-fPIC", "-shared", "-o", dst_name, src_name, NULL };

    // write source code to temporary file
    FILE *src = fopen(src_name, "w");
    if (!src) {
      perror("fopen");
      return 1;
    }
    if (is_func) {
      fprintf(src, "%s", line);
    }
    else {
      fprintf(src, "int __expr_wrapper() { return (%s); }", line);
    }
    fclose(src);

    int pid = fork();
    if (pid < 0) {
      perror("fork");
      return 1;
    } else if (pid == 0) {

      // child process to compile
#ifdef __DEBUG__
      printf("compiling...\n");
      fflush(stdout);
#endif
      int fd = open("/dev/null", O_WRONLY);
      dup2(fd, STDERR_FILENO);
      dup2(fd, STDOUT_FILENO);
      execvp("gcc", exec_argv);
    } else {

      // parent process to wait for child process
      int status;
      wait(&status);
#ifdef __DEBUG__
      printf("status %d\n", status);
#endif
      if (status != 0) {
        printf("compile failed.\n");
      }
      else {
        // load compiled library
#ifdef __DEBUG__
        printf("destination: %s\n", dst_name);
#endif
        void *handle = dlopen(dst_name, RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
          printf("load failed.\n");
        }
        else {
          // call compiled function
          if (is_func) {
            printf("Success!\n");
          }
          else {
            int (*f)() = dlsym(handle, "__expr_wrapper");
            printf("= %d\n", f());
            dlclose(handle);
          }
        }
      }
      fflush(stdout);
    }
    unlink(src_name);
    unlink(dst_name);
  }
  return 0;
}
