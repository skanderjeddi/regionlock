#include "region_lock_library.h"

void rl_random_populate(const char* path, const size_t length) {
    int file_descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_descriptor == -1) {
        critical("Failed to open file\n");
        return;
    }
    int random_data_descriptor = open("/dev/urandom", O_RDONLY);
    if (random_data_descriptor == -1) {
        critical("Failed to open /dev/urandom\n");
        return;
    }
    char* buffer = (char*) malloc(length);
    if (buffer == NULL) {
        critical("Failed to allocate buffer\n");
        return;
    }
    ssize_t bytes_read = read(random_data_descriptor, buffer, length);
    if (bytes_read == -1) {
        critical("Failed to read from /dev/urandom\n");
        return;
    }
    for (size_t i = 0; i < length; i++) {
        char byte = buffer[i];
        char character = (byte % 26) + 65;
        write(file_descriptor, &character, 1);
    }
    int close_result = close(file_descriptor);
    if (close_result == -1) {
        critical("Failed to close file\n");
        return;
    }
    close_result = close(random_data_descriptor);
    if (close_result == -1) {
        critical("Failed to close /dev/urandom\n");
        return;
    }
    free(buffer);
}

size_t rl_hues_format_mutex_lock(char* buffer, size_t buffer_size, char specifier, va_list args) {
    pthread_mutex_t* mtx = va_arg(args, pthread_mutex_t*);
    return snprintf(buffer, buffer_size, "%s%d%s%p%s", "thread #",  getpid(), " locked mutex [", mtx, "]");
}

size_t rl_hues_format_mutex_unlock(char* buffer, size_t buffer_size, char specifier, va_list args) {
    pthread_mutex_t* mtx = va_arg(args, pthread_mutex_t*);
    return snprintf(buffer, buffer_size, "%s%d%s%p%s", "thread #", getpid(), " unlocked mutex [", mtx, "]");
}

size_t rl_hues_format_cond_wait(char* buffer, size_t buffer_size, char specifier, va_list args) {
    pthread_cond_t* cond = va_arg(args, pthread_cond_t*);
    pthread_mutex_t* mtx = va_arg(args, pthread_mutex_t*);
    return snprintf(buffer, buffer_size, "%s%p%s%p%s", "waiting on cond [", cond, "] on mutex [", mtx, "]");
}

size_t rl_hues_format_cond_broadcast(char* buffer, size_t buffer_size, char specifier, va_list args) {
    pthread_cond_t* cond = va_arg(args, pthread_cond_t*);
    return snprintf(buffer, buffer_size, "%s%p%s", "broadcasted on cond [", cond, "]");
}

void test_write_read_without_lock() {
    rl_descriptor descriptor = rl_open("loremipsum.txt", O_CREAT | O_RDWR);
    char write_buf[100] = "Hello, World!";
    char read_buf[100];
    rl_write(descriptor, write_buf, strlen(write_buf));
    lseek(descriptor.descriptor, 0, SEEK_SET); // reset file position
    ssize_t read = rl_read(descriptor, read_buf, strlen(write_buf));
    read_buf[read] = '\0';
    assert(strcmp(write_buf, read_buf) == 0);
    rl_close(descriptor);
    critical("test_write_read_without_lock passed\n");
}

void test_write_read_with_lock() {
    rl_descriptor descriptor = rl_open("loremipsum.txt", O_CREAT | O_RDWR);
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_len = 50;
    rl_fcntl(descriptor, F_SETLK, &lock);
    char write_buf[100] = "Hello, World!";
    rl_write(descriptor, write_buf, strlen(write_buf));
    // Set up a read lock
    lock.l_type = F_RDLCK;
    lock.l_start = 0;
    lock.l_len = 50;
    rl_fcntl(descriptor, F_SETLK, &lock);
    char read_buf[100];
    lseek(descriptor.descriptor, 0, SEEK_SET); // reset file position
    ssize_t read = rl_read(descriptor, read_buf, strlen(write_buf));
    read_buf[read] = '\0';
    assert(strcmp(write_buf, read_buf) == 0);
    rl_close(descriptor);
    critical("test_write_read_with_lock passed\n");
}

void test_write_lock_fork() {
    rl_descriptor descriptor = rl_open("loremipsum.txt", O_CREAT | O_RDWR);
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_len = 50;
    rl_fcntl(descriptor, F_SETLK, &lock);
    char write_buf[100] = "Hello, World!";
    rl_write(descriptor, write_buf, strlen(write_buf));
    rl_fork();
    // In the child process, try to write
    char child_buf[100] = "Hello, Child World!";
    ssize_t write_result = rl_write(descriptor, child_buf, strlen(child_buf));
    assert(write_result == -1); // We expect this to fail due to the write lock
    rl_close(descriptor);
    critical("test_write_lock_fork passed\n");
}

void *thread_func(void *arg) {
    rl_descriptor descriptor = *((rl_descriptor*)arg);
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_len = 50;
    rl_fcntl(descriptor, F_SETLK, &lock);
    char write_buf[100] = "Hello, Thread!";
    rl_write(descriptor, write_buf, strlen(write_buf));
    lock.l_type = F_UNLCK;
    rl_fcntl(descriptor, F_SETLK, &lock);
    return NULL;
}

void test_multiple_threads() {
    rl_descriptor descriptor = rl_open("loremipsum.txt", O_CREAT | O_RDWR);
    pthread_t thread1, thread2;
    pthread_create(&thread1, NULL, &thread_func, &descriptor);
    pthread_create(&thread2, NULL, &thread_func, &descriptor);
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    rl_close(descriptor);
    critical("test_multiple_threads passed\n");
}

void test_locks_released_after_close() {
    rl_descriptor descriptor = rl_open("loremipsum.txt", O_CREAT | O_RDWR);
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_len = 50;
    rl_fcntl(descriptor, F_SETLK, &lock);
    char write_buf[100] = "Hello, World!";
    rl_write(descriptor, write_buf, strlen(write_buf));
    rl_close(descriptor);
    descriptor = rl_open("loremipsum.txt", O_WRONLY);
    ssize_t write_result = rl_write(descriptor, write_buf, strlen(write_buf));
    assert(write_result != -1);
    rl_close(descriptor);
    critical("test_locks_released_after_close passed\n");
}

void test_conflicting_locks() {
    rl_descriptor descriptor = rl_open("loremipsum.txt", O_CREAT | O_RDWR);
    struct flock lock1;
    lock1.l_type = F_WRLCK;
    lock1.l_start = 0;
    lock1.l_len = 50;
    assert(rl_fcntl(descriptor, F_SETLK, &lock1) != -1); // Should succeed
    struct flock lock2;
    lock2.l_type = F_WRLCK;
    lock2.l_start = 25;
    lock2.l_len = 50;
    assert(rl_fcntl(descriptor, F_SETLK, &lock2) == -1); // Should fail
    rl_close(descriptor);
    critical("test_conflicting_locks passed\n");
}

void test_multiple_process_conflict() {
    int fd[2];
    pipe(fd); // create a pipe for process communication
    rl_descriptor descriptor = rl_open("loremipsum.txt", O_CREAT | O_RDWR);
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_len = 100;
    rl_fcntl(descriptor, F_SETLK, &lock);
    if (fork() == 0) { // child process
        close(fd[0]); // close unused read end
        rl_descriptor descriptor_child = rl_open("loremipsum.txt", O_RDWR);
        struct flock child_lock;
        child_lock.l_type = F_WRLCK;
        child_lock.l_start = 50;
        child_lock.l_len = 50;
        // attempt to acquire the lock and write the result to the parent process
        int result = rl_fcntl(descriptor_child, F_SETLK, &child_lock);
        write(fd[1], &result, sizeof(result));
        rl_util_show_global_state();
        rl_close(descriptor_child);
        close(fd[1]);
        exit(0);
    } else { // parent process
        close(fd[1]); // close unused write end
        int result;
        wait(NULL); // wait for the child process to terminate
        read(fd[0], &result, sizeof(result)); // read the result from the child process
        trace("%d\n", result);
        assert(result == -1); // The child's lock attempt should fail
        rl_close(descriptor);
        close(fd[0]);
    }
    critical("test_multiple_process_conflict passed\n");
}

void* test_thread_func2(void* arg) {
    rl_descriptor* descriptor = (rl_descriptor*) arg;
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_len = 50;
    if (rl_fcntl(*descriptor, F_SETLK, &lock) == -1) { // Attempt to set a write lock
        pthread_exit((void*)1); // Exit with status 1 on failure
    }
    pthread_exit(NULL); // Exit with status 0 on success
}

void test_multiple_thread_conflict() {
    rl_descriptor descriptor = rl_open("loremipsum.txt", O_CREAT | O_RDWR);
    pthread_t threads[2];
    // Create two threads and have them both attempt to acquire a write lock on the file
    for (int i = 0; i < 2; ++i) {
        pthread_create(&threads[i], NULL, &test_thread_func2, &descriptor);
    }
    // Check the exit statuses of the threads
    for (int i = 0; i < 2; ++i) {
        void* status;
        pthread_join(threads[i], &status);
        if (i == 0) {
            assert((long) status == 0); // First thread should succeed
        } else {
            assert((long) status == 1); // Second thread should fail
        }
    }
    rl_close(descriptor);
    critical("test_multiple_thread_conflict passed\n");
}

int old_main() {
    // Ignore this part of the code, it's just to make the output look nice
    hues_initialize();
    hues_configuration_set_level_format("(##p) #d @ #t [#L in #c]\t");
    hues_format format_mutex_lock;
    format_mutex_lock.specifier = "mxl";
    format_mutex_lock.format_function = rl_hues_format_mutex_lock;
    hues_configuration_add_format(&format_mutex_lock);
    hues_format format_mutex_unlock;
    format_mutex_unlock.specifier = "mxu";
    format_mutex_unlock.format_function = rl_hues_format_mutex_unlock;
    hues_configuration_add_format(&format_mutex_unlock);
    hues_format format_mutex_cond_wait;
    format_mutex_cond_wait.specifier = "cwl";
    format_mutex_cond_wait.format_function = rl_hues_format_cond_wait;
    hues_configuration_add_format(&format_mutex_cond_wait);
    hues_format format_mutex_cond_broadcast;
    format_mutex_cond_broadcast.specifier = "cbl";
    format_mutex_cond_broadcast.format_function = rl_hues_format_cond_broadcast;
    hues_configuration_add_format(&format_mutex_cond_broadcast);
    trace("logging sucessfully initialized.\n");
    // End of hues initialization.
    // This is the actual code that you should be looking at.
    rl_random_populate("loremipsum.txt", 4096); // Generate a 4KB file with random data
    rl_descriptor descriptor = rl_open("loremipsum.txt", O_RDWR);
    if (&descriptor == RL_OPEN_FAILED) {
        critical("failed to open file!\n");
        return EXIT_FAILURE;
    }
    trace("file opened successfully.\n");
    struct flock read_lock = {
        .l_type = F_RDLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 16
    };
    int lock_result = rl_fcntl(descriptor, F_SETLKW, &read_lock);
    if (lock_result == -1) {
        critical("failed to add read lock!\n");
        return EXIT_FAILURE;
    }
    trace("read lock added successfully.\n"); // Set a read lock on the first 16 bytes of the file
    rl_util_show_global_state("after setting lock #1"); 
    pid_t pid = rl_fork();
    if (pid == -1) {
        critical("failed to fork!\n");
        return EXIT_FAILURE;
    } else if (pid == 0) {
        sleep(1);
        debug("in the child process..\n");
        struct flock write_lock = {
            .l_type = F_WRLCK,
            .l_whence = SEEK_SET,
            .l_start = 16,
            .l_len = 32
        };
        debug("trying to add write lock...\n");
        int lock_result = rl_fcntl(descriptor, F_SETLKW, &write_lock);
        if (lock_result == -1) {
            critical("failed to add write lock!\n");
            return EXIT_FAILURE;
        }
        trace("write lock added successfully.\n"); // Try to set a write lock on the first 16 bytes of the file
        // This should block until the read lock is removed by the parent process (after 5 seconds) 
        // ... but it's not. Why?
        rl_util_show_global_state("after setting lock #3");
        char buffer[17];
        ssize_t read_result = rl_read(descriptor, buffer, 8);
        if (read_result == -1) {
            critical("failed to read from file!\n");
            return EXIT_FAILURE;
        }
        buffer[read_result] = '\0';
        trace("read from file successfully: %s\n", buffer);
        lseek(descriptor.descriptor, 43, SEEK_SET);
        ssize_t write_result = rl_write(descriptor, "hello", 5);
        if (write_result == -1) {
            critical("failed to write to file!\n");
            return EXIT_FAILURE;
        }
        trace("wrote to file successfully.\n");
        int close_result = rl_close(descriptor);
        if (close_result == -1) {
            critical("failed to close file!\n");
            return EXIT_FAILURE;
        }
        trace("file closed successfully by child.\n");
    } else {
        rl_util_show_global_state("after forking");
        struct flock read_lock = {
            .l_type = F_RDLCK,
            .l_whence = SEEK_SET,
            .l_start = 16,
            .l_len = 32
        };
        int lock_result = rl_fcntl(descriptor, F_SETLKW, &read_lock);
        if (lock_result == -1) {
            critical("failed to add read lock!\n");
            return EXIT_FAILURE;
        } // Remove the read lock from the file (which should allow the child process to continue) 
        // ... but it doesn't. Why?
        trace("lock unlocked successfully.\n");
        rl_util_show_global_state("after setting lock #2");
        sleep(3);
        struct flock unlock_lock = {
            .l_type = F_UNLCK,
            .l_whence = SEEK_SET,
            .l_start = 20,
            .l_len = 24
        };
        lock_result = rl_fcntl(descriptor, F_SETLKW, &unlock_lock);
        if (lock_result == -1) {
            critical("failed to unlock lock!\n");
            return EXIT_FAILURE;
        } // Remove the read lock from the file (which should allow the child process to continue) 
        // ... but it doesn't. Why?
        trace("lock unlocked successfully.\n");
        rl_util_show_global_state("after unlocking lock #1");
        /**
         * Close the file and exit.
        */
        int close_result = rl_close(descriptor);
        if (close_result == -1) {
            critical("failed to close file!\n");
            return EXIT_FAILURE;
        }
        info("file closed successfully by parent, exiting...\n");
    }
    return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    // Ignore this part of the code, it's just to make the output look nice
    hues_initialize();
    hues_configuration_set_level_format("(##p) #d @ #t [#L in #c]\t");
    hues_format format_mutex_lock;
    format_mutex_lock.specifier = "mxl";
    format_mutex_lock.format_function = rl_hues_format_mutex_lock;
    hues_configuration_add_format(&format_mutex_lock);
    hues_format format_mutex_unlock;
    format_mutex_unlock.specifier = "mxu";
    format_mutex_unlock.format_function = rl_hues_format_mutex_unlock;
    hues_configuration_add_format(&format_mutex_unlock);
    hues_format format_mutex_cond_wait;
    format_mutex_cond_wait.specifier = "cwl";
    format_mutex_cond_wait.format_function = rl_hues_format_cond_wait;
    hues_configuration_add_format(&format_mutex_cond_wait);
    hues_format format_mutex_cond_broadcast;
    format_mutex_cond_broadcast.specifier = "cbl";
    format_mutex_cond_broadcast.format_function = rl_hues_format_cond_broadcast;
    hues_configuration_add_format(&format_mutex_cond_broadcast);
    trace("logging sucessfully initialized.\n");
    // End of hues initialization.
    // This is the actual code that you should be looking at.
    rl_random_populate("loremipsum.txt", 4096); // Generate a 4KB file with random data
    // Tests
    test_write_read_without_lock();
    test_write_read_with_lock();
    test_write_lock_fork();
    test_multiple_threads();
    test_locks_released_after_close();
    test_conflicting_locks();
    test_multiple_process_conflict();
    test_multiple_thread_conflict();
}