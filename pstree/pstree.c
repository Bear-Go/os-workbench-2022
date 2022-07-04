#include <assert.h>
#include <dirent.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static struct option long_options[] = {
  {"numeric-sort",  no_argument,  0,  'n'},
  {"show-pids",     no_argument,  0,  'p'},
  {"version",       no_argument,  0,  'V'},
  {0,               0,            0,   0 }
};

static char Usage[] = 
"Usage: pstree [ -p ] [ -n ]\n\
   or: pstree -V\n\n\
Display a tree of processes.\n\n\
  -n, --numeric-sort  sort output by PID\n\
  -p, --show-pids     show PIDs; implies -c\n\
  -V, --version       display version information\n";

static char version[] =
"pstree (PSmisc) UNKNOWN\n\
Copyright (C) 1993-2019 Werner Almesberger and Craig Small\n\n\
PSmisc comes with ABSOLUTELY NO WARRANTY.\n\
This is free software, and you are welcome to redistribute it under\n\
the terms of the GNU General Public License.\n\
For more information about these matters, see the files named COPYING.\n";

int isnumber(const char *name) {
  for (int i = 0; name[i] != '\0'; ++i) {
    if (name[i] < '0' || name[i] > '9')
      return 0;
  }
  return 1;
}

struct proc {
  char name[256];
  pid_t pid;
  pid_t ppid;
  int son;
  int brother;
};

#define MAXN 1 << 16
static int root, tap;
static struct proc plist[MAXN];

int c, nflag, pflag;

void print(int n) {
  for (int i = 0; i < tap; ++i) {
    printf("\t");
  }
  if (pflag) 
    printf("%s(%d)\n", plist[n].name, plist[n].pid);
  else 
    printf("%s\n", plist[n].name);
  
  if (plist[n].son != -1) {
    tap++;
    print(plist[n].son);
    tap--;
  }

  if (plist[n].brother != -1) {
    print(plist[n].brother);
  }
}

int main(int argc, char *argv[]) {

  // getopt
  while ((c = getopt_long(argc, argv, "npV", long_options, 0)) != -1) {
    switch (c) {
      case 'n': nflag = 1; break;
      case 'p': pflag = 1; break;
      case 'V': fprintf(stderr, "%s", version); return 0;
      case '?': fprintf(stderr, "%s", Usage); return 0;
      default:  return 1;
    }
  }

  if (optind < argc) {
    printf("non-option ARGV-elements: ");
    while (optind < argc)
      printf("%s ", argv[optind++]);
    printf("\n");
  }

  int ret = chdir("/proc");
  assert(ret == 0);

  DIR *dp = opendir(".");
  assert(dp);

  struct dirent *dirname;
  int pnum = 0;
  while ( (dirname = readdir(dp)) ) {
    // check whether it is a proc dir
    if (!isnumber(dirname->d_name)) continue;

    char pathname[512];
    strcpy(pathname, dirname->d_name);
    strcat(pathname, "/status");

    FILE *fp = fopen(pathname, "r");
    assert(fp);

    char buf[512], name[256];
    pid_t pid, ppid;
    fscanf(fp, "Name:\t%s", name); // name may be not one word
    while (fscanf(fp, "Pid:\t%d", &pid) != 1) fgets(buf, sizeof(buf), fp);
    while (fscanf(fp, "PPid:\t%d", &ppid) != 1) fgets(buf, sizeof(buf), fp);

    // filter
    if (pid == 2 || ppid == 2) continue;

    strcpy(plist[pnum].name, name);
    plist[pnum].pid = pid;
    plist[pnum].ppid = ppid;
    plist[pnum].brother = -1;
    plist[pnum].son = -1;
    pnum++;

    fclose(fp);
  }
  closedir(dp);

  if (nflag) {
    // numeric sort
  }
  else {
    // name sort
  }


  // build the tree

  // .brother = the other proc whose ppid is the same
  // .son = the first proc whose ppid equals its pid
  for (int i = 0; i < pnum; ++i) {

    // root of pstree
    assert(plist[i].ppid != 2);
    if (plist[i].ppid == 0) {
      root = i;
      continue;
    }

    // transverse the plist to find every one's brother and son
    for (int j = 0; j < pnum; ++j) {

      // check whether plist[j] relates to plist[i]
      if (plist[i].ppid == plist[j].pid) {

        if (plist[j].son == -1) {
          // the first son of plist[j]
          plist[j].son = i;
        }
        else {
          // the other son of plist[j]
          int cur = plist[j].son;
          while (plist[cur].brother != -1) cur = plist[cur].brother;
          plist[cur].brother = i;
        } 
      }
    }
  }

  // print the tree
  print(root);

  return 0;
}
