#pragma once

#include <unistd.h>
#include <sys/resource.h>

#define WEXITSTATUS(s) (((s) >> 8) & 255)
#define WIFEXITED(s) (((s) & 127) == 0)
#define WTERMSIG(s) ((s) & 127)
#define WIFSIGNALED(s) (WTERMSIG(s) != 0 && WTERMSIG(s) != 127)

pid_t waitpid(pid_t pid, int *status, int options);
