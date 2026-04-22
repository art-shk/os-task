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

#define WORKERS_COUNT 4
#define TIMEOUT_SECONDS 5

static void (*set_key_ptr)(char) = NULL;
static void (*caesar_ptr)(void *, void *, int) = NULL;

typedef struct
{
    long total_ms;
    long avg_ms;
    int file_count;
} timing_result_t;

// Простая очередь файлов с мьютексом
typedef struct
{
    char **files;
    const char *output_dir;
    char key;
    int num_files;
    volatile int next_file;
    pthread_mutex_t mutex;
} file_queue_t;

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
    static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
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

timing_result_t run_sequential(char **files, int num_files, const char *output_dir, char key)
{
    timing_result_t result = {0};
    struct timespec start_total, end_total;
    clock_gettime(CLOCK_MONOTONIC, &start_total);

    for (int i = 0; i < num_files; i++)
    {
        const char *input_file = files[i];
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

        set_key_ptr(key);
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

    clock_gettime(CLOCK_MONOTONIC, &end_total);
    result.total_ms = (end_total.tv_sec - start_total.tv_sec) * 1000 +
                      (end_total.tv_nsec - start_total.tv_nsec) / 1000000;
    result.file_count = num_files;
    result.avg_ms = num_files ? result.total_ms / num_files : 0;

    return result;
}

void *worker_thread(void *arg)
{
    file_queue_t *queue = (file_queue_t *)arg;

    while (1)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += TIMEOUT_SECONDS;

        if (pthread_mutex_timedlock(&queue->mutex, &ts) == ETIMEDOUT)
        {
            unsigned long tid = (unsigned long)pthread_self();
            printf("Возможная взаимоблокировка: поток %lu ждёт мьютекс более %d секунд\n",
                   tid, TIMEOUT_SECONDS);
            exit(EXIT_FAILURE);
        }

        if (queue->next_file >= queue->num_files)
        {
            pthread_mutex_unlock(&queue->mutex);
            break;
        }

        int idx = queue->next_file++;
        pthread_mutex_unlock(&queue->mutex);

        const char *input_file = queue->files[idx];
        const char *filename = get_filename(input_file);

        char output_path[1024];
        snprintf(output_path, sizeof(output_path), "%s/%s", queue->output_dir, filename);

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        off_t file_size;
        char *buffer = read_file(input_file, &file_size);
        if (!buffer)
        {
            log_operation(filename, "ERROR_READ", 0);
            continue;
        }

        set_key_ptr(queue->key);
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

timing_result_t run_parallel(char **files, int num_files, const char *output_dir, char key)
{
    timing_result_t result = {0};
    struct timespec start_total, end_total;
    clock_gettime(CLOCK_MONOTONIC, &start_total);

    file_queue_t queue = {
        .files = files,
        .output_dir = output_dir,
        .key = key,
        .num_files = num_files,
        .next_file = 0,
        .mutex = PTHREAD_MUTEX_INITIALIZER};

    pthread_t workers[WORKERS_COUNT];
    for (int i = 0; i < WORKERS_COUNT; i++)
    {
        if (pthread_create(&workers[i], NULL, worker_thread, &queue) != 0)
        {
            perror("Ошибка создания потока");
            break;
        }
    }

    for (int i = 0; i < WORKERS_COUNT; i++)
    {
        pthread_join(workers[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_total);
    result.total_ms = (end_total.tv_sec - start_total.tv_sec) * 1000 +
                      (end_total.tv_nsec - start_total.tv_nsec) / 1000000;
    result.file_count = num_files;
    result.avg_ms = num_files ? result.total_ms / num_files : 0;

    return result;
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "Использование:\n");
        fprintf(stderr, "  %s [--mode=sequential|parallel] <файл1> ... <выходная_директория> <ключ>\n", argv[0]);
        return 1;
    }

    int arg_start = 1;
    int explicit_mode = 0;
    int mode_sequential = 0;

    if (strncmp(argv[1], "--mode=", 7) == 0)
    {
        explicit_mode = 1;
        if (strcmp(argv[1] + 7, "sequential") == 0)
        {
            mode_sequential = 1;
        }
        else if (strcmp(argv[1] + 7, "parallel") == 0)
        {
            mode_sequential = 0;
        }
        else
        {
            fprintf(stderr, "Неизвестный режим: %s\n", argv[1]);
            return 1;
        }
        arg_start = 2;
    }

    char key = argv[argc - 1][0];
    const char *output_dir = argv[argc - 2];
    int num_files = argc - 2 - arg_start;
    char **input_files = &argv[arg_start];

    if (num_files <= 0)
    {
        fprintf(stderr, "Ошибка: не указаны входные файлы\n");
        return 1;
    }

    void *handle = dlopen("./libcaesar.so", RTLD_LAZY);
    if (!handle)
    {
        fprintf(stderr, "Ошибка загрузки libcaesar.so: %s\n", dlerror());
        return 1;
    }

    set_key_ptr = (void (*)(char))(uintptr_t)dlsym(handle, "set_key");
    caesar_ptr = (void (*)(void *, void *, int))(uintptr_t)dlsym(handle, "caesar");

    if (!set_key_ptr || !caesar_ptr)
    {
        fprintf(stderr, "Ошибка: не найдены функции в libcaesar.so\n");
        dlclose(handle);
        return 1;
    }

    if (create_directory(output_dir) != 0)
    {
        perror("Ошибка создания выходной директории");
        dlclose(handle);
        return 1;
    }

    if (explicit_mode)
    {
        // Явный режим — выполняем только его
        timing_result_t res;
        if (mode_sequential)
        {
            res = run_sequential(input_files, num_files, output_dir, key);
            printf("=== Результаты (Sequential) ===\n");
        }
        else
        {
            res = run_parallel(input_files, num_files, output_dir, key);
            printf("=== Результаты (Parallel) ===\n");
        }
        printf("Обработано файлов: %d\n", res.file_count);
        printf("Общее время: %ld мс\n", res.total_ms);
        printf("Среднее время на файл: %ld мс\n", res.avg_ms);
    }
    else
    {
        // Автоматический режим — выполняем ОБА режима
        timing_result_t seq = run_sequential(input_files, num_files, output_dir, key);

        // Очистка выходной директории
        DIR *dir = opendir(output_dir);
        if (dir)
        {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL)
            {
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                {
                    char path[1024];
                    snprintf(path, sizeof(path), "%s/%s", output_dir, entry->d_name);
                    unlink(path);
                }
            }
            closedir(dir);
        }

        timing_result_t par = run_parallel(input_files, num_files, output_dir, key);

        printf("\n=== Сравнение режимов ===\n");
        printf("%-12s | %8s | %10s\n", "Режим", "Всего (мс)", "Среднее (мс)");
        printf("----------------------------------------\n");
        printf("%-12s | %8ld | %10ld\n", "Sequential", seq.total_ms, seq.avg_ms);
        printf("%-12s | %8ld | %10ld\n", "Parallel", par.total_ms, par.avg_ms);
        printf("Режим выбран автоматически: %s\n",
               (num_files < 5) ? "SEQUENTIAL (<5 файлов)" : "PARALLEL (≥5 файлов)");
    }

    dlclose(handle);
    return 0;
}