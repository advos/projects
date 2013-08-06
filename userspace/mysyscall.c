#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>

/* for invoking syscalls */
#include <sys/syscall.h>

/* getpid */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* for logging */
#include <syslog.h>

/* open */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* strnlen */
#include <string.h>

/* constants */
#define FORK (2)
#define SYS_GET_PID (20)
#define SYS_SWITCH_SANDBOX (350)
#define SYS_GET_SANDBOX (351)
#define MAX_LENGTH (100)
#define WD_SIZE (1024)

int getpid_syscall(void) {
  return (int) syscall(SYS_GET_PID);
}

int before_fork(void) 
{
  int prio2 = LOG_USER | LOG_DEBUG;
  int fd_common;
  ssize_t bw = 0;
  char * common_str = "common";
  ssize_t cstr_size = strnlen(common_str, MAX_LENGTH);

  fd_common = open("common.txt", O_CREAT | O_RDWR, S_IRWXU);
  bw = write(fd_common, common_str, cstr_size);
  if (bw < 0) { 
    syslog(prio2, "sandbox-common: before fork, common file write FAIL\n"); 
  } else { 
    syslog(prio2, "sandbox-common: before fork, common file write OK\n"); 
  } 

  return fd_common;
}

void test_syscall_blocking(char * identity, int pid_out)
{
  int prio = LOG_USER | LOG_ALERT;
  int prio2 = LOG_USER | LOG_DEBUG;
  pid_t pid;

  syslog(prio, "%s: ----------------------------------------------------\n", identity);
  syslog(prio, "%s: syscall blocking test\n", identity);

    /* getppid */
  pid = getppid();
  if (pid < 0) {
    syslog(prio2, "%s: getppid() FAIL\n", identity);
  } else {
    syslog(prio2, "%s: getppid() OK\n", identity);
  }

  /* getpid */
  pid = getpid_syscall();
  if (pid < 0) {
    syslog(prio2, "%s: getpid() FAIL\n", identity);
    if (0 == pid_out) {
      syslog(prio, "%s: test OK!\n", identity);
    }
  } else {
    syslog(prio2, "%s: getpid() OK\n", identity);
    if (0 == pid_out) {
      syslog(prio, "%s: test FAILED!\n", identity);
    }
  }
}

void test_file_stripping(char * identity, int fd_common, int pid_out)
{
  int prio = LOG_USER | LOG_ALERT;
  int prio2 = LOG_USER | LOG_DEBUG;
  ssize_t bw;
  char * str2 = " 2ndstr";
  ssize_t str2_size = strnlen(str2, MAX_LENGTH);
  int cc;
  
  syslog(prio, "%s: ----------------------------------------------------\n", identity);
  syslog(prio, "%s: file stripping test\n", identity);

  /* trying to keep writing to fd_common */
  /* only father should succeed */
  bw = write(fd_common, str2, str2_size);
  if (bw < 0) {
    syslog(prio2, "%s: after fork, common file write FAIL\n", identity);
  } else {
    syslog(prio2, "%s: after fork, common file write OK\n", identity);

  }
  
  cc = close(fd_common);
  if (cc < 0) {
    syslog(prio2, "%s: close fd common FAIL\n", identity);
    if (0 == pid_out) {
      syslog(prio, "%s: test OK!\n", identity);
    }
  } else {
    syslog(prio2, "%s: close fd common OK\n", identity);
    if (0 == pid_out) {
      syslog(prio, "%s: test FAILED!\n", identity);
    }
  }
}

void test_allowed_file(char * identity, int pid_out, const char * filename, int allowed)
{
  int prio = LOG_USER | LOG_ALERT;
  int prio2 = LOG_USER | LOG_DEBUG;
  /* ssize_t br; */
  /* char read_buffer[MAX_LENGTH]; */
  int co;

  int fd_allowed;

  syslog(prio, "%s: ----------------------------------------------------\n", identity);
  syslog(prio, "%s: test_allowed_file\n", identity);
  
  /* trying to open file outside sandbox */
  /* only father should succeed */
  fd_allowed = open(filename, O_RDONLY, S_IRWXU);
  if ((fd_allowed < 0) &&(0 == pid_out)) { /* child failed */
    syslog(prio2, "%s: file [%s] FAIL\n", identity, filename);
    syslog(prio2, "%s: file execptional: %d\n", identity, allowed);
  } else {
    if (0 == pid_out) {
      syslog(prio2, "%s: file [%s] OK\n", identity, filename);
      syslog(prio2, "%s: file execptional: %d\n", identity, allowed);
    }
  }
  
  /* br = read(fd_allowed, read_buffer, MAX_LENGTH); */
  /* syslog(prio2, "%s: read %d bytes from fd %d: [%s]\n", identity, (int) br, fd_allowed, read_buffer); */
  
  co = close(fd_allowed);
  if (co < 0) {
    syslog(prio2, "%s: close fd FAIL\n", identity);
  } else {
    syslog(prio2, "%s: close fd OK\n", identity);
  }
}

void check_wd(char * identity)
{
  int prio = LOG_USER | LOG_ALERT;
  char working_dir[WD_SIZE];

  getcwd(working_dir, WD_SIZE);
  syslog(prio, "%s: my working dir is [%s]\n", identity, working_dir);
}

void after_fork(char * identity, int fd_common, int pid_out)
{
  check_wd(identity);
  test_syscall_blocking(identity, pid_out);
  test_file_stripping(identity, fd_common, pid_out);
  test_allowed_file(identity, pid_out, "existing.txt", 0);
  test_allowed_file(identity, pid_out, "b.txt", 1);
  test_allowed_file(identity, pid_out, "a.txt", 1);
}

int main(void) 
{
  int prio = LOG_USER | LOG_ALERT;
  int prio2 = LOG_USER | LOG_DEBUG;
  int ret, pid;
  long new_sandbox = 1;
  int fd_common;
  int status;

  fd_common = before_fork();

  printf("starting sandbox test program... check syslog.\n");
  
  pid = (int) syscall(FORK);
  if(pid == 0) {
    /* this is the child */
    syslog(prio2, "sandbox-child: fork() returned!\n");
    
    /* switches sandbox */
    ret = (int) syscall(SYS_SWITCH_SANDBOX, new_sandbox);
    printf("sandbox-child: switch_sandbox() ret: %d\n", ret);
    syslog(prio2, "sandbox-child: switch_sandbox() ret: %d\n", ret);

    new_sandbox = (long) syscall(SYS_GET_SANDBOX);
    syslog(prio, "sandbox-child: get_sandbox() ret: %ld\n", new_sandbox);

    /* same actions */
    after_fork("sandbox-child", fd_common, pid);
  
  } else {
    /* this is the father */
    syslog(prio, "sandbox-father: (pid of child is %d)\n", pid);
    
    waitpid(pid, &status, 0);
    printf("wait returned status (%d).\n", status);
    /* same actions */
    after_fork("sandbox-father", fd_common, pid);
  }
  return 0;
}
