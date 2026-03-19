#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdint.h>
#include "libcaesar.h"

#define NUM_THREADS 3
#define TIMEOUT_SECONDS 5

// Глобальные мьютексы
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// Указатели на функции из libcaesar.so (глобальные, чтобы все потоки использовали одни)
void (*set_key_ptr)(char) = NULL;
void (*caesar_ptr)(void *, void *, int) = NULL;

typedef struct
{
    char **input_files;
    const char *output_dir;
    int num_files;
    int key;
    volatile int *current_index;
    volatile int *completed_count;
} thread_args_t;

void get_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

const char *get_filename(const char *path)
{
    const char *last_slash = strrchr(path, '/');
    return last_slash ? last_slash + 1 : path;
}

void log_operation(const char *filename, const char *status, long duration_ms)
{
    pthread_mutex_lock(&log_mutex);
    FILE *log_file = fopen("log.txt", "a");
    if (!log_file)
    {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    unsigned long tid = (unsigned long)pthread_self();

    fprintf(log_file, "[%s] [%lu] [%s] %s %ldms\n",
            timestamp, tid, filename, status, duration_ms);

    fclose(log_file);
    pthread_mutex_unlock(&log_mutex);
}

char *read_file(const char *filename, off_t *size)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
        return NULL;

    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(*size);
    if (!buffer)
    {
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, *size, file);
    fclose(file);
    return buffer;
}

int write_file(const char *filename, const char *data, off_t size)
{
    FILE *file = fopen(filename, "wb");
    if (!file)
        return -1;
    fwrite(data, 1, size, file);
    fclose(file);
    return 0;
}

void *worker_thread(void *arg)
{
    thread_args_t *args = (thread_args_t *)arg;
    char **input_files = args->input_files;
    const char *output_dir = args->output_dir;
    int key = args->key;
    volatile int *current_index = args->current_index;
    volatile int *completed_count = args->completed_count;

    while (1)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += TIMEOUT_SECONDS;

        if (pthread_mutex_timedlock(&counter_mutex, &ts) == ETIMEDOUT)
        {
            unsigned long tid = (unsigned long)pthread_self();
            printf("Возможная взаимоблокировка: поток %lu ждёт мьютекс более %d секунд\n",
                   tid, TIMEOUT_SECONDS);
            exit(EXIT_FAILURE);
        }

        int idx = (*current_index)++;
        if (idx >= args->num_files)
        {
            pthread_mutex_unlock(&counter_mutex);
            break;
        }

        (*completed_count)++;
        pthread_mutex_unlock(&counter_mutex);

        const char *input_file = input_files[idx];
        const char *filename = get_filename(input_file);

        char output_path[1024];
        snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, filename);

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        off_t file_size;
        char *buffer = read_file(input_file, &file_size);
        if (!buffer)
        {
            log_operation(filename, "ERROR_READ", 0);
            continue;
        }

        // Используем загруженные из .so функции
        set_key_ptr((char)key);
        caesar_ptr(buffer, buffer, (int)file_size);

        if (write_file(output_path, buffer, file_size) != 0)
        {
            free(buffer);
            log_operation(filename, "ERROR_WRITE", 0);
            continue;
        }

        free(buffer);

        clock_gettime(CLOCK_MONOTONIC, &end);
        long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                           (end.tv_nsec - start.tv_nsec) / 1000000;

        log_operation(filename, "SUCCESS", duration_ms);
    }

    return NULL;
}

int create_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
    {
        return S_ISDIR(st.st_mode) ? 0 : (errno = ENOTDIR, -1);
    }
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return 0;
    return -1;
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "Использование: %s <файл1> ... <выходная_директория> <ключ>\n", argv[0]);
        return 1;
    }

    // Загрузка libcaesar.so
    void *handle = dlopen("./libcaesar.so", RTLD_LAZY);
    if (!handle)
    {
        fprintf(stderr, "Ошибка загрузки libcaesar.so: %s\n", dlerror());
        return 1;
    }

    // Получение указателей на функции
    set_key_ptr = (void (*)(char))(uintptr_t)dlsym(handle, "set_key");
    caesar_ptr = (void (*)(void *, void *, int))(uintptr_t)dlsym(handle, "caesar");

    if (!set_key_ptr || !caesar_ptr)
    {
        fprintf(stderr, "Ошибка: не найдены функции в libcaesar.so\n");
        dlclose(handle);
        return 1;
    }

    int key = atoi(argv[argc - 1]);
    const char *output_dir = argv[argc - 2];
    int num_files = argc - 3;
    char **input_files = &argv[1];

    if (create_directory(output_dir) != 0)
    {
        perror("Ошибка создания выходной директории");
        dlclose(handle);
        return 1;
    }

    volatile int current_index = 0;
    volatile int completed_count = 0;

    pthread_t threads[NUM_THREADS];
    thread_args_t args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++)
    {
        args[i].input_files = input_files;
        args[i].output_dir = output_dir;
        args[i].num_files = num_files;
        args[i].key = key;
        args[i].current_index = &current_index;
        args[i].completed_count = &completed_count;

        if (pthread_create(&threads[i], NULL, worker_thread, &args[i]) != 0)
        {
            perror("Ошибка создания потока");
            dlclose(handle);
            return 1;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }

    dlclose(handle); // Закрываем библиотеку
    return 0;
}