#include "region_lock_library.h"

/**
 * @brief The library's internal data structure.
*/
static struct {
    size_t total_files;
    rl_file* files[RL_LCKMAXFILES];
} rl_library;

/**
 * Initializes a mutex.
*/
static int rl_core_initialize_mutex(rl_file* file) {
    pthread_mutexattr_t mutex_attributes;
    int mutex_attr_value = pthread_mutexattr_init(&mutex_attributes);
    pthread_mutexattr_setpshared(&mutex_attributes, PTHREAD_PROCESS_SHARED);
    pthread_mutex_t mutex;
    int mutex_init_value = pthread_mutex_init(&mutex, &mutex_attributes);
    if (mutex_attr_value == 0 && mutex_init_value == 0) {
        /* log("mutex init success\n"); */
        file->mutex = mutex;
        return 0;
    } // If the mutex attributes and the mutex cannot be initialized, return the error code.
    return -1;
}

static int rl_core_initialize_condition(rl_lock* lock) {
    pthread_condattr_t condition_attributes;
    int condition_attr_value = pthread_condattr_init(&condition_attributes);
    pthread_condattr_setpshared(&condition_attributes, PTHREAD_PROCESS_SHARED);
    pthread_cond_t condition_variable;
    int condition_init_value = pthread_cond_init(&condition_variable, &condition_attributes);
    if (condition_attr_value == 0 && condition_init_value == 0) {
        /* log("condition_variable init success\n"); */
        lock->condition_variable = condition_variable;
        return 0;
    } // If the condition_variable attributes and the condition_variable cannot be initialized, return the error code.
    return -1;
}

static void rl_util_initialize_lock(rl_lock* lock) {
    lock->readers_count = 0;
    lock->has_writer = false;
    lock->offset = -1;
    lock->size = -1;
    lock->type = -1;
    lock->is_initialized = false;
    for (size_t i = 0; i < RL_LCKMAXOWNERS; ++i) {
        lock->owners[i].thread_id = 0;
        lock->owners[i].descriptor = -1;
    }
    rl_core_initialize_condition(lock);
}

const char* rl_util_get_lock_type_as_string(short lock_type) {
    switch (lock_type) {
        case F_RDLCK:
            return "R";
        case F_WRLCK:
            return "W";
        case F_UNLCK:
            return "U";
        default:
            return "?";
    }
}

rl_lock* rl_util_find_free_lock(rl_file* file) {
    for (ssize_t i = 0; i < RL_LCKMAXLCKS; ++i) {
        if (!file->locks[i].is_initialized) {
            return &(file->locks[i]);
        }
    }
    return NULL;
}

void rl_util_clear_lock(rl_lock* lock) {
    lock->is_initialized = false;
    lock->readers_count = 0;
    lock->has_writer = false;
    lock->offset = 0;
    lock->size = 0;
    for (size_t i = 0; i < RL_LCKMAXOWNERS; ++i) {
        lock->owners[i].thread_id = 0;
        lock->owners[i].descriptor = -1;
    }
}

void rl_util_copy_lock(rl_lock* source, rl_lock* destination) {
    destination->is_initialized = source->is_initialized;
    destination->readers_count = source->readers_count;
    destination->has_writer = source->has_writer;
    destination->offset = source->offset;
    destination->size = source->size;
    destination->type = source->type;
    for (size_t i = 0; i < RL_LCKMAXOWNERS; ++i) {
        destination->owners[i].thread_id = source->owners[i].thread_id;
        destination->owners[i].descriptor = source->owners[i].descriptor;
    }
    destination->readers_count = source->readers_count;
    destination->has_writer = source->has_writer;
    rl_core_initialize_condition(destination);
}

void rl_util_defragment_locks(rl_file* file) {
    ssize_t i = 0, j = RL_LCKMAXLCKS - 1;
    while (i < j) {
        // Move i to an uninitialized lock
        while (i < RL_LCKMAXLCKS && file->locks[i].is_initialized) {
            ++i;
        }
        // Move j to an initialized lock
        while (j >= 0 && !file->locks[j].is_initialized) {
            --j;
        }
        // If i is still less than j, swap the locks
        if (i < j) {
            rl_lock temp = file->locks[i];
            file->locks[i] = file->locks[j];
            file->locks[j] = temp;
        }
    }
    // After defragmentation, sort the initialized locks by size first, then by start position using insertion sort
    for (i = 1; i < RL_LCKMAXLCKS; ++i) {
        // Skip uninitialized locks
        if (!file->locks[i].is_initialized) {
            continue;
        }
        rl_lock key = file->locks[i];
        j = i - 1;
        // Move elements that are greater than key to one position ahead
        while (j >= 0 && (file->locks[j].size < key.size || 
            (file->locks[j].size == key.size && file->locks[j].offset > key.offset))) {
            file->locks[j + 1] = file->locks[j];
            --j;
        }
        file->locks[j + 1] = key;
    }
}

void rl_util_show_global_state(const char* message)  {
    char buffer[BUFFER_SIZE * 2];
    size_t header_size;
    size_t written = hues_format_p(buffer, BUFFER_SIZE * 2, "\n-------------------- GLOBAL STATE --------------------\n\n", message);
    header_size = written;
    if (rl_library.total_files > 0) {
        for (size_t file_index = 0; file_index < rl_library.total_files; file_index++) {
            rl_file* file = rl_library.files[file_index];
            size_t file_header_size = hues_format_p(buffer + written, BUFFER_SIZE * 2, "|-%d- file mapped at '/dev/shm%s'\n", file_index, file->shared_memory_object_name, file->users_on_file);
            written += file_header_size;
            for (size_t lock_index = 0; lock_index < RL_LCKMAXLCKS; lock_index++) {
                rl_lock* current_lock = &(file->locks[lock_index]);
                // Skip uninitialized locks
                if (!current_lock->is_initialized) {
                    continue;
                }
                boolean has_owners = false;
                for (size_t owner_index = 0; owner_index < RL_LCKMAXOWNERS; owner_index++) {
                    if (current_lock->owners[owner_index].thread_id != 0) {
                        has_owners = true;
                        break;
                    }
                }
                if (has_owners) {
                    // Using warn, print the lock's information in the format: "pointer address [start; start+len] type as string [readers | writer as int]"
                    written += hues_format_p(buffer + written, BUFFER_SIZE * 2, "|--%ld-- lock [%ld; %ld] %s [%ld | %d]\n", lock_index, current_lock->offset, current_lock->offset + current_lock->size, rl_util_get_lock_type_as_string(current_lock->type), current_lock->readers_count, current_lock->has_writer);
                    for (size_t owner_index = 0; owner_index < RL_LCKMAXOWNERS; owner_index++) {
                        if (current_lock->owners[owner_index].thread_id != 0) {
                            written += hues_format_p(buffer + written, BUFFER_SIZE * 2, "|---%ld--- owner #%d\n", owner_index, current_lock->owners[owner_index].thread_id);
                        }
                    }
                }
            }
            written += hues_format_p(buffer + written, BUFFER_SIZE * 2, "|");
            for (size_t i = 0; i < file_header_size - 2; i++) {
                written += hues_format_p(buffer + written, BUFFER_SIZE * 2, "-");
            }
            written += hues_format_p(buffer + written, BUFFER_SIZE * 2, "\n");
        }
    }
    header_size -= 2;
    char* footer = malloc(header_size * sizeof(char));
    char* footer_message = "END";
    size_t message_length = strlen(footer_message);
    size_t padding = (header_size - message_length) / 2;  // how many -'s should go before and after
    // Add -'s before the message
    size_t i;
    for (i = 0; i < padding - 1; i++) {
        footer[i] = '-';
    }
    footer[i] = ' ';
    memcpy(footer + padding, footer_message, message_length);
    footer[padding + message_length] = ' ';
    for (int i = padding + message_length + 1; i < header_size - 1; i++) {
        footer[i] = '-';
    }
    footer[header_size - 1] = '\0';
    written += hues_format_p(buffer + written, BUFFER_SIZE * 2, "\n%s\n", footer);
    debug("%s", buffer);
}

int rl_initialize() {
    rl_library.total_files = 0;
    return 0;
}

rl_descriptor rl_open(const char* path, int flags) {
    if (rl_library.total_files == RL_LCKMAXFILES) {
        severe("no more files can be opened\n");
        return (*(RL_OPEN_FAILED));
    } // If the maximum number of files is already open, return RL_OPEN_FAILED. 
    int descriptor = open(path, flags);
    if (descriptor == -1) {
        severe("file cannot be opened\n");
        close(descriptor);
        return (*(RL_OPEN_FAILED));
    } // If the file cannot be opened, return RL_OPEN_FAILED.
    // Get the path of the shared memory object.
    char* shared_memory_object_name = malloc(RL_LCKMAXPATH * sizeof(char));
    char* rl_env_prefix = getenv("RL_LOCK_PREFIX");
    if (rl_env_prefix == NULL) {
        rl_env_prefix = "/f_";
    } // Get the prefix for the shared memory object name from the environment variable RL_LOCK_PREFIX. If it is not set, use "/f_" as the prefix.
    // Get std_dev and st_ino from the file descriptor.
    struct stat file_stats;
    if (fstat(descriptor, &file_stats) == -1) {
        close(descriptor);
        severe("cannot stat file\n");
        return (*(RL_OPEN_FAILED));
    } // If the file cannot be stat'ed, return NULL.
    int total_length = snprintf(shared_memory_object_name, RL_LCKMAXPATH, "%s%ld_%ld", rl_env_prefix, (long) file_stats.st_dev, (long) file_stats.st_ino);
    shared_memory_object_name[total_length] = '\0';
    if (shared_memory_object_name == NULL || shared_memory_object_name[0] == '\0') {
        severe("cannot get shared memory object name\n");
        close(descriptor);
        return (*(RL_OPEN_FAILED));
    } // If the shared memory object name cannot be retrieved, return RL_OPEN_FAILED.
    boolean shared_memory_object_exists = false;
    // Open the shared memory object.
    int shared_memory_object_descriptor = shm_open(shared_memory_object_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (shared_memory_object_descriptor == -1 && errno != EEXIST) { // If the shared memory object cannot be opened and the error is not that it already exists, return RL_OPEN_FAILED.
        critical("shared memory object cannot be opened\n");
        close(descriptor);
        shm_unlink(shared_memory_object_name);
        return (*(RL_OPEN_FAILED));
    } // If the shared memory object cannot be opened and the error is not that it already exists, return RL_OPEN_FAILED.
    if (shared_memory_object_descriptor == -1 && errno == EEXIST) {
        trace("shared memory object already exists\n");
        shared_memory_object_exists = true;
        shared_memory_object_descriptor = shm_open(shared_memory_object_name, O_RDWR, S_IRUSR | S_IWUSR);
        if (shared_memory_object_descriptor == -1) {
            critical("shared memory object cannot be opened\n");
            close(descriptor);
            shm_unlink(shared_memory_object_name);
            return (*(RL_OPEN_FAILED));
        } // If the shared memory object cannot be opened, return RL_OPEN_FAILED.
    }
    info("shared memory object opened.\n");
    // Truncate the shared memory object to the size of the file.
    if (ftruncate(shared_memory_object_descriptor, sizeof(rl_file)) == -1) {
        severe("shared memory object cannot be truncated!\n");
        close(descriptor);
        shm_unlink(shared_memory_object_name);
        return (*(RL_OPEN_FAILED));
    } // If the shared memory object cannot be truncated, return RL_OPEN_FAILED.
    debug("shared memory object truncated to %ld bytes.\n", sizeof(rl_file));
    // Map the shared memory object to memory.
    void* shared_memory_object = mmap(NULL, sizeof(rl_file), PROT_READ | PROT_WRITE, MAP_SHARED, shared_memory_object_descriptor, 0);
    if (shared_memory_object == MAP_FAILED) {
        severe("shared memory object cannot be mapped!\n");
        close(descriptor);
        shm_unlink(shared_memory_object_name);
        return (*(RL_OPEN_FAILED));
    } // If the shared memory object cannot be mapped, return RL_OPEN_FAILED.
    rl_file* file = (rl_file*) shared_memory_object;
    info("shared memory object mapped at %p.\n", shared_memory_object);
    rl_descriptor rl_descriptor;
    rl_descriptor.descriptor = descriptor;
    // Initialize the shared memory object.
    if (!shared_memory_object_exists) {
        debug("initializing shared memory object...\n");
        rl_core_initialize_mutex(file);
        pthread_mutex_lock(&(file->mutex));
        trace("mutex locked (cold).\n");
        file->shared_memory_object_name = malloc(strlen(shared_memory_object_name) + 1);
        strncpy(file->shared_memory_object_name, shared_memory_object_name, strlen(shared_memory_object_name) + 1);
        file->users_on_file = 1;
        for (size_t i = 0; i < RL_LCKMAXLCKS; i++) {
            rl_util_initialize_lock(&(file->locks[i]));
        }
        pthread_mutex_unlock(&(file->mutex));
        trace("mutex unlocked (cold).\n");
        rl_descriptor.file = file;
        debug("shared memory object initialized (cold).\n");
        trace("%ld user(s) on file (cold).\n", file->users_on_file);
    } else {
        warn("shared memory object already exists!\n");        
        rl_core_initialize_mutex(file);
        pthread_mutex_lock(&(file->mutex));
        trace("mutex locked (warm).\n");
        file->users_on_file++;
        trace("users on file: %ld (warm).\n", file->users_on_file);
        // Initialize cond variables.
        pthread_mutex_unlock(&(file->mutex));
        trace("mutex unlocked (warm).\n");
        rl_descriptor.file = file;
        debug("shared memory object initialized (warm).\n");
    }
    // Add the file descriptor to the library.
    rl_library.files[rl_library.total_files] = rl_descriptor.file;
    rl_library.total_files++;
    info("%ld mapped file(s).\n", rl_library.total_files);
    close(shared_memory_object_descriptor); // Close the shared memory object descriptor.
    info("shared memory object descriptor closed.\n");
    // Free memory.
    free(shared_memory_object_name);
    return rl_descriptor;
}

int rl_close(rl_descriptor descriptor) {
    if (descriptor.descriptor == -1) {
        severe("invalid descriptor!\n");
        return -1;
    }
    rl_file* file = descriptor.file;
    pthread_mutex_lock(&(file->mutex));
    info("file retrieved & mutex locked..\n");
    if (file->users_on_file > 0) {
        int close_result = close(descriptor.descriptor);
        if (close_result == -1) {
            pthread_mutex_unlock(&(file->mutex));
            severe("file could not be closed (mutex released)!\n");
            return -1;
        }
        for (size_t lock_index = 0; lock_index < RL_LCKMAXLCKS; lock_index++) {
            rl_lock* current_lock = &(file->locks[lock_index]);
            // Skip uninitialized locks
            if (!current_lock->is_initialized) {
                continue;
            }
            for (size_t owner_index = 0; owner_index < RL_LCKMAXOWNERS; owner_index++) {
                rl_owner* owner = &(current_lock->owners[owner_index]);
                // Skip unowned locks
                if (owner->thread_id == 0) {
                    continue;
                }
                if (owner->thread_id == getpid() && owner->descriptor == descriptor.descriptor) {
                    // Shift owners after the current one back by one
                    for (size_t shift_owner_index = owner_index; shift_owner_index < RL_LCKMAXOWNERS - 1; shift_owner_index++) {
                        current_lock->owners[shift_owner_index] = current_lock->owners[shift_owner_index + 1];
                    }
                    // Reset the last owner
                    current_lock->owners[RL_LCKMAXOWNERS - 1].thread_id = 0;
                    current_lock->owners[RL_LCKMAXOWNERS - 1].descriptor = -1;
                    // Check if the lock is now unowned, and if so, reset it
                    boolean is_unowned = true;
                    for (size_t check_owner_index = 0; check_owner_index < RL_LCKMAXOWNERS; check_owner_index++) {
                        if (current_lock->owners[check_owner_index].thread_id != 0) {
                            is_unowned = false;
                            break;
                        }
                    }
                    if (is_unowned) {
                        current_lock->is_initialized = false;
                        current_lock->has_writer = false;
                        current_lock->readers_count = 0;
                        pthread_cond_broadcast(&(current_lock->condition_variable));
                        pthread_cond_destroy(&(current_lock->condition_variable));
                    }
                }
            }
        }
        rl_util_defragment_locks(file);
        file->users_on_file--;
        debug("%d remaining user(s) on file.\n", file->users_on_file);
        for (int i = 0; i < rl_library.total_files; i++) {
            if (rl_library.files[i] == file && file->users_on_file == 0) {
                for (int j = i; j < rl_library.total_files - 1; j++) {
                    rl_library.files[j] = rl_library.files[j + 1];
                }
                rl_library.total_files--;
                info("%ld mapped file(s).\n", rl_library.total_files);
                info("file closed & descriptor removed from library.\n");
                break;
            }
        }
        // Only destroy mutex if there are no more users on the file
        if (file->users_on_file == 0) {
            pthread_mutex_unlock(&(file->mutex));
            pthread_mutex_destroy(&(file->mutex));
        } else {
            pthread_mutex_unlock(&(file->mutex));
        }
        if (file->shared_memory_object_name != NULL) {
            shm_unlink(file->shared_memory_object_name);
            free(file->shared_memory_object_name);
        }
        munmap(file, sizeof(rl_file));
        trace("file closed & mutex released.\n");
        return 0;
    } else {
        pthread_mutex_unlock(&(file->mutex));
        return -1;
    }
}

rl_descriptor rl_dup(rl_descriptor descriptor) {
    rl_descriptor new_descriptor;
    new_descriptor.descriptor = dup(descriptor.descriptor);
    if (new_descriptor.descriptor == -1) {
        return (*(RL_OPEN_FAILED));
    } // If the file cannot be duplicated, return RL_OPEN_FAILED.
    new_descriptor.file = descriptor.file;
    return new_descriptor;
}

rl_descriptor rl_dup2(rl_descriptor descriptor, int new_descriptor) {
    rl_descriptor new_rl_descriptor;
    new_rl_descriptor.descriptor = dup2(descriptor.descriptor, new_descriptor);
    if (new_rl_descriptor.descriptor == -1) {
        return (*(RL_OPEN_FAILED));
    } // If the file cannot be duplicated, return RL_OPEN_FAILED.
    new_rl_descriptor.file = descriptor.file;
    return new_rl_descriptor;
}

static int fork_sync_pipe[2];

static pid_t rl_fork_internal() {
    debug("forking...\n");
    pid_t pid = fork();
    if (pid == 0) {
        info("in the child process..\n");
        char buf = '1';
        // Child
        pid_t parent_pid = getppid();
        for (size_t file_index = 0; file_index < rl_library.total_files; file_index++) {
            rl_file* file = rl_library.files[file_index];
            pthread_mutex_lock(&(file->mutex));
            trace("file retrieved & mutex locked.\n");
            for (size_t lock_index = 0; lock_index < RL_LCKMAXLCKS; lock_index++) {
                rl_lock* current_lock = &(file->locks[lock_index]);
                // Skip uninitialized locks
                if (!current_lock->is_initialized) {
                    continue;
                }
                for (size_t owner_index = 0; owner_index < RL_LCKMAXOWNERS; owner_index++) {
                    rl_owner* owner = &(current_lock->owners[owner_index]);
                    // Skip unowned locks
                    if (owner->thread_id == 0) {
                        continue;
                    }
                    if (owner->thread_id == parent_pid) {
                        // Look for a free owner spot in the owners array
                        for (size_t free_owner_index = 0; free_owner_index < RL_LCKMAXOWNERS; free_owner_index++) {
                            if (current_lock->owners[free_owner_index].thread_id == 0) {  // free spot found
                                current_lock->owners[free_owner_index].thread_id = getpid();
                                current_lock->owners[free_owner_index].descriptor = owner->descriptor;
                                file->users_on_file++;
                                if (current_lock->type == F_RDLCK) {
                                    current_lock->readers_count++;
                                } // TODO: ELSE? 2 writers can't write at the same time???
                                info("%ld user(s) on file.\n", file->users_on_file);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&(file->mutex));
            trace("copy into child done & mutex unlocked.\n");
        }
        close(fork_sync_pipe[0]);  // Close unused read end
        if (write(fork_sync_pipe[1], &buf, 1) != 1) {
            critical("could not write to pipe inside child!\n");
            return -1;
        }
        close(fork_sync_pipe[1]); // Close write end
        return pid;
    } else if (pid > 0) {
        // Parent
        char buf;
        close(fork_sync_pipe[1]);  // Close unused write end
        if (read(fork_sync_pipe[0], &buf, 1) != 1) {
            critical("could not read from pipe inside parent<\n");
            return -1;
        }
        close(fork_sync_pipe[0]);  // Close read end, we're done with it
        return pid;
    } else {
        critical("fork failed!\n");
        return -1;
    }
}

pid_t rl_fork() {
    if (pipe(fork_sync_pipe) == -1) {
        critical("pipe system call failed!\n");
        return -1;
    }
    pid_t fork_result = rl_fork_internal();
    return fork_result;
}

int rl_core_set_lock(rl_descriptor descriptor, struct flock* lock, boolean blocking) {
    rl_file* file = descriptor.file;
    ssize_t i;
    pid_t current_thread_id = getpid();
    // Handle the unlock operation
    if (lock->l_type == F_UNLCK) {
        debug("unlocking...\n");
        for (i = 0; i < RL_LCKMAXLCKS; ++i) {
            rl_lock* current_lock = &(file->locks[i]);
            // Skip uninitialized locks
            if (!current_lock->is_initialized) {
                continue;
            }
            // Only interested in locks owned by current thread
            for (size_t j = 0; j < RL_LCKMAXOWNERS; ++j) {
                if (current_lock->owners[j].thread_id == current_thread_id && 
                    current_lock->owners[j].descriptor == descriptor.descriptor &&
                    lock->l_start >= current_lock->offset && 
                    lock->l_start + lock->l_len <= current_lock->offset + current_lock->size) {
                    ssize_t old_lock_start = current_lock->offset;
                    ssize_t old_lock_end = current_lock->offset + current_lock->size;
                    // Check if we have to split the lock
                    if (lock->l_start > old_lock_start && lock->l_start + lock->l_len < old_lock_end) {
                        // New lock from the remaining part at the start
                        rl_lock* start_lock = rl_util_find_free_lock(file);
                        if (start_lock != NULL) {
                            rl_util_copy_lock(current_lock, start_lock);
                            start_lock->size = lock->l_start - old_lock_start;
                        }
                        // New lock from the remaining part at the end
                        rl_lock* end_lock = rl_util_find_free_lock(file);
                        if (end_lock != NULL) {
                            rl_util_copy_lock(current_lock, end_lock);
                            end_lock->offset = lock->l_start + lock->l_len;
                            end_lock->size = old_lock_end - end_lock->offset;
                        }
                        rl_util_clear_lock(current_lock);
                        pthread_cond_broadcast(&(current_lock->condition_variable));
                        pthread_cond_destroy(&(current_lock->condition_variable));
                    } else if (lock->l_start == old_lock_start && lock->l_start + lock->l_len < old_lock_end) {
                        // The lock to be removed is at the start of the current lock
                        current_lock->offset = lock->l_start + lock->l_len;
                        current_lock->size -= lock->l_len;
                    } else if (lock->l_start > old_lock_start && lock->l_start + lock->l_len == old_lock_end) {
                        // The lock to be removed is at the end of the current lock
                        current_lock->size -= lock->l_len;
                    } else {
                        // Just clear the lock as before
                        rl_util_clear_lock(current_lock);
                        pthread_cond_broadcast(&(current_lock->condition_variable));
                        pthread_cond_destroy(&(current_lock->condition_variable));
                    }
                    rl_util_defragment_locks(file);  // Defragment the locks array
                    // Signal the semaphore in case other threads are waiting
                    info("lock %ld unlocked for descriptor %d.\n", i, descriptor.descriptor);
                    return 0; // Successfully unlocked
                }
            }
        }
        return -1; // Lock not found
    } else {
        debug("setting lock...\n");
        for (i = 0; i < RL_LCKMAXLCKS; ++i) {
            rl_lock* current_lock = &(file->locks[i]);
            // If the lock is not initialized, we can safely skip it
            if (!current_lock->is_initialized) {
                continue;
            }
            if (lock->l_start < current_lock->offset + current_lock->size && 
                lock->l_start + lock->l_len > current_lock->offset) {
                warn("lock is in conflict with lock %ld!\n", i);
                // We have a conflict - check the type of locks
                if (lock->l_type == F_WRLCK || current_lock->type == F_WRLCK) {
                    // A write lock is requested or existing lock is a write lock - conflict
                    if (blocking) {
                        while (current_lock->has_writer || current_lock->readers_count > 0) {
                            pthread_cond_wait(&(current_lock->condition_variable), &(file->mutex));
                        }
                        i = 0; // Restart the search
                    } else {
                        return -1; // Conflict and non-blocking, so we return an error
                    }
                } else {
                    // Two read locks - not a conflict, update the readers count
                    ++current_lock->readers_count;
                    // Search for an empty slot in the owners array
                    size_t j;
                    for (j = 0; j < RL_LCKMAXOWNERS; ++j) {
                        if (current_lock->owners[j].thread_id == 0) {
                            current_lock->owners[j].thread_id = current_thread_id;
                            current_lock->owners[j].descriptor = descriptor.descriptor;
                            return 0;
                        }
                    }
                    // If we reach here, there was no available slot in the owners array
                    return -1;
                }
            }
        }
        trace("no conflict found.\n");
        // After checking all locks, if we reach here, there was no conflict
        // Now, find an empty slot to set the new lock
        for (i = 0; i < RL_LCKMAXLCKS; ++i) {
            rl_lock* current_lock = &(file->locks[i]);
            // If the lock is not initialized, we can safely set a new lock here
            if (!current_lock->is_initialized) {
                current_lock->is_initialized = true;
                current_lock->readers_count = (lock->l_type == F_RDLCK) ? 1 : 0;
                current_lock->has_writer = (lock->l_type == F_WRLCK) ? true : false;
                current_lock->offset = lock->l_start;
                current_lock->size = lock->l_len;
                current_lock->type = lock->l_type;
                current_lock->owners[0].thread_id = current_thread_id;
                current_lock->owners[0].descriptor = descriptor.descriptor;
                return 0;
            }
        }
        // If we reach here, it means there was no available slot for the lock
        return -1;
    }
    return -1;
}

int rl_fcntl(rl_descriptor descriptor, int command, struct flock* lock) {
    if (descriptor.descriptor == -1) {
        severe("invalid descriptor!\n");
        return -1;
    }
    rl_file* file = descriptor.file;
    pthread_mutex_lock(&(file->mutex));
    info("file retrieved & mutex locked.\n");
    int result = 0;
    switch (command) {
        case F_GETLK:
            result = /* _rl_get_lock(file, lock); */ -1;
            break;
        case F_SETLK:
            result = rl_core_set_lock(descriptor, lock, false);
            break;
        case F_SETLKW:
            result = rl_core_set_lock(descriptor, lock, true);
            break;
        default:
            warn("unsupported command!\n");
            result = -1;
            break;
    }
    pthread_mutex_unlock(&(file->mutex));
    return result;
}