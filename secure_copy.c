#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "libcaesar.h"

#define BUFFER_SIZE 8192

// Структура для передачи данных между потоками
typedef struct
{
    int in_fd;
    int out_fd;
    off_t total_size;
    volatile sig_atomic_t *keep_running;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond; // Условная переменная
    int done;             // Флаг завершения
    char buffer[BUFFER_SIZE];
    size_t buffer_size;
    off_t bytes_processed;
} shared_data_t;

// Глобальная переменная для обработки сигнала
volatile sig_atomic_t keep_running = 1;

// Обработчик сигнала SIGINT
void sigint_handler(int sig)
{
    keep_running = 0;
}

void *producer_thread(void *arg)
{
    shared_data_t *data = (shared_data_t *)arg;
    ssize_t bytes_read;
    char temp_buffer[BUFFER_SIZE];

    while (keep_running)
    {
        // Читаем данные из файла
        bytes_read = read(data->in_fd, temp_buffer, BUFFER_SIZE);

        if (bytes_read < 0)
        {
            perror("Ошибка чтения");
            keep_running = 0;
            break;
        }

        if (bytes_read == 0)
        {
            // Достигнут конец файла
            pthread_mutex_lock(data->mutex);
            data->done = 1;
            pthread_cond_signal(data->cond);
            pthread_mutex_unlock(data->mutex);
            break;
        }

        // Шифруем данные
        caesar(temp_buffer, temp_buffer, (int)bytes_read);

        // Помещаем данные в общий буфер
        pthread_mutex_lock(data->mutex);

        while (data->buffer_size > 0 && keep_running)
        {
            // Ждём, пока потребитель заберёт данные
            pthread_cond_wait(data->cond, data->mutex);
        }

        if (!keep_running)
        {
            pthread_mutex_unlock(data->mutex);
            break;
        }

        // Копируем зашифрованные данные в буфер
        memcpy(data->buffer, temp_buffer, bytes_read);
        data->buffer_size = bytes_read;
        data->bytes_processed += bytes_read;

        // Сигнализируем потребителю
        pthread_cond_signal(data->cond);
        pthread_mutex_unlock(data->mutex);
    }

    return NULL;
}

void *consumer_thread(void *arg)
{
    shared_data_t *data = (shared_data_t *)arg;
    struct timespec last_update = {0};
    struct timespec now;
    int last_percent = -1;

    while (keep_running)
    {
        pthread_mutex_lock(data->mutex);

        // Ждём данных от производителя
        while (data->buffer_size == 0 && !data->done && keep_running)
        {
            pthread_cond_wait(data->cond, data->mutex);
        }

        if (!keep_running)
        {
            pthread_mutex_unlock(data->mutex);
            break;
        }

        if (data->buffer_size == 0 && data->done)
        {
            // Все данные обработаны
            pthread_mutex_unlock(data->mutex);
            break;
        }

        // Записываем данные в выходной файл
        size_t bytes_to_write = data->buffer_size;
        pthread_mutex_unlock(data->mutex);

        ssize_t bytes_written = write(data->out_fd, data->buffer, bytes_to_write);

        pthread_mutex_lock(data->mutex);

        if (bytes_written < 0)
        {
            perror("Ошибка записи");
            keep_running = 0;
            pthread_mutex_unlock(data->mutex);
            break;
        }

        // Очищаем буфер
        data->buffer_size = 0;

        // Сигнализируем производителю
        pthread_cond_signal(data->cond);
        pthread_mutex_unlock(data->mutex);

        // Обновляем прогресс-бар (не чаще 100мс)
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - last_update.tv_sec) * 1000 +
                          (now.tv_nsec - last_update.tv_nsec) / 1000000;

        if (elapsed_ms >= 100 || data->done)
        {
            last_update = now;

            // Вычисляем процент выполнения
            int percent = (data->total_size > 0) ? (int)((data->bytes_processed * 100) / data->total_size) : 0;

            // Обновляем только если изменился процент
            if (percent != last_percent)
            {
                last_percent = percent;

                // Выводим прогресс-бар
                int bar_width = 50;
                int filled = (percent * bar_width) / 100;
                int empty = bar_width - filled;

                printf("\r[");
                for (int i = 0; i < filled; i++)
                    printf("=");
                for (int i = 0; i < empty; i++)
                    printf(" ");
                printf("] %3d%%", percent);
                fflush(stdout);
            }
        }
    }

    printf("\n");
    return NULL;
}

off_t get_file_size(const char *filename)
{
    struct stat st;
    if (stat(filename, &st) == 0)
        return st.st_size;
    return -1;
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "Использование: %s <входной_файл> <выходной_файл> <ключ>\n", argv[0]);
        fprintf(stderr, "Пример: %s source.txt dest.txt 65\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    const char *output_file = argv[2];
    int key = atoi(argv[3]);

    if (access(input_file, F_OK) != 0)
    {
        fprintf(stderr, "Ошибка: файл '%s' не существует\n", input_file);
        return 1;
    }

    int in_fd = open(input_file, O_RDONLY);
    if (in_fd < 0)
    {
        perror("Ошибка открытия входного файла");
        return 1;
    }

    int out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0)
    {
        perror("Ошибка создания выходного файла");
        close(in_fd);
        return 1;
    }

    off_t file_size = get_file_size(input_file);
    if (file_size < 0)
    {
        fprintf(stderr, "Ошибка: не удалось определить размер файла\n");
        close(in_fd);
        close(out_fd);
        return 1;
    }

    // Установка обработчика сигнала SIGINT
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // Установка ключа шифрования
    set_key((char)key);

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

    shared_data_t shared_data;
    shared_data.in_fd = in_fd;
    shared_data.out_fd = out_fd;
    shared_data.total_size = file_size;
    shared_data.keep_running = &keep_running;
    shared_data.mutex = &mutex;
    shared_data.cond = &cond;
    shared_data.done = 0;
    shared_data.buffer_size = 0;
    shared_data.bytes_processed = 0;

    // Создание потоков
    pthread_t producer, consumer;

    if (pthread_create(&producer, NULL, producer_thread, &shared_data) != 0)
    {
        perror("Ошибка создания потока-производителя");
        close(in_fd);
        close(out_fd);
        return 1;
    }

    if (pthread_create(&consumer, NULL, consumer_thread, &shared_data) != 0)
    {
        perror("Ошибка создания потока-потребителя");
        keep_running = 0;
        pthread_join(producer, NULL);
        close(in_fd);
        close(out_fd);
        return 1;
    }

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    close(in_fd);
    close(out_fd);

    if (!keep_running)
    {
        printf("\nОперация прервана пользователем\n");
        // Удаляем частично записанный файл
        unlink(output_file);
        return 1;
    }

    printf("Копирование и шифрование завершены успешно\n");
    return 0;
}