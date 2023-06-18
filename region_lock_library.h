#pragma once

#ifndef __REGION_LOCK_LIBRARY_H__
#define __REGION_LOCK_LIBRARY_H__

#define boolean char
#define true 1
#define false !true

#define RL_LCKMAXPATH 1024

#define RL_LCKMAXCONDS 64
#define RL_LCKMAXFILES 16
#define RL_LCKMAXOWNERS 8
#define RL_LCKMAXLCKS 4

#define RL_OPEN_FAILED ((rl_descriptor*) -1)

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "hues.h"

#define pthread_mutex_lock(mtx)  \
    {                            \
        trace("#mxl\n", mtx);    \
        pthread_mutex_lock(mtx); \
    }

#define pthread_mutex_unlock(mtx)  \
    {                              \
        trace("#mxu\n", mtx);      \
        pthread_mutex_unlock(mtx); \
    }

#define pthread_cond_wait(cond, mtx)  \
    {                                 \
        trace("#cwl\n", cond, mtx);   \
        pthread_cond_wait(cond, mtx); \
    }

#define pthread_cond_broadcast(cond)  \
    {                                 \
        trace("#cbl\n", cond);        \
        pthread_cond_broadcast(cond); \
    }

typedef struct {
    pid_t thread_id; // thread id (use getpid() to get it)
    int descriptor; // descriptor of the physical file
} rl_owner;

typedef struct rl_lock {
    size_t readers_count; // how many readers are currently reading
    boolean has_writer; // true if there is a writer, false otherwise
    pthread_cond_t condition_variable; // condition variable for readers
    rl_owner owners[RL_LCKMAXOWNERS]; // owners of the lock
    off_t offset; // offset of the lock (= start of flock)
    size_t size; // size of the lock (= len of flock)
    size_t type; // type of the lock (= type of flock)
    boolean is_initialized; // true if there is a next lock, false otherwise
} rl_lock;

typedef struct {
    pthread_mutex_t mutex; // mutex to protect the file
    char* shared_memory_object_name; // name of the shared memory object
    rl_lock locks[RL_LCKMAXLCKS]; // locks of the file
    size_t users_on_file; // how many users are currently using the file
} rl_file;

typedef struct {
    rl_file* file; // mapped file
    int descriptor; // descriptor of the physical file
} rl_descriptor;

int rl_initialize();

extern void rl_util_show_global_state();

rl_descriptor rl_open(const char*, int);
int rl_close(rl_descriptor);

rl_descriptor rl_dup(rl_descriptor);
rl_descriptor rl_dup2(rl_descriptor, int);

pid_t rl_fork();

int rl_fcntl(rl_descriptor, int, struct flock*);

ssize_t rl_write(rl_descriptor, const void*, size_t);
ssize_t rl_read(rl_descriptor, void*, size_t);

#endif /* __REGION_LOCK_LIBRARY_H__ */
