#include "libcaesar.h"

#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#define KEY_SIZE 16

static char *protected_key = NULL;
static size_t page_size = 0;

static void sigsegv_handler(int sig, siginfo_t *info, void *context)
{
    (void)sig;
    (void)context;

    if (protected_key &&
        info->si_addr >= (void *)protected_key &&
        info->si_addr < (void *)(protected_key + KEY_SIZE))
    {

        const char msg[] =
            "Ошибка безопасности: попытка записи в защищённую память\n";

        write(STDERR_FILENO, msg, sizeof(msg) - 1);
    }
    else
    {
        const char msg[] =
            "SIGSEGV: недопустимое обращение к памяти\n";

        write(STDERR_FILENO, msg, sizeof(msg) - 1);
    }

    _exit(EXIT_FAILURE);
}

void init_secure_key(void)
{
    if (protected_key)
        return;

    page_size = sysconf(_SC_PAGESIZE);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_sigaction = sigsegv_handler;
    sa.sa_flags = SA_SIGINFO;

    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSEGV, &sa, NULL) != 0)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    protected_key = mmap(
        NULL,
        page_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);

    if (protected_key == MAP_FAILED)
    {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    memset(protected_key, 0, KEY_SIZE);

    if (mprotect(protected_key, page_size, PROT_READ) != 0)
    {
        perror("mprotect");
        exit(EXIT_FAILURE);
    }
}

void set_key(const char *key_str)
{
    if (!protected_key)
        init_secure_key();

    if (mprotect(protected_key, page_size,
                 PROT_READ | PROT_WRITE) != 0)
    {

        perror("mprotect set_key");
        exit(EXIT_FAILURE);
    }

    memset(protected_key, 0, KEY_SIZE);

    size_t len = strlen(key_str);

    if (len > KEY_SIZE)
        len = KEY_SIZE;

    memcpy(protected_key, key_str, len);

    if (mprotect(protected_key, page_size,
                 PROT_READ) != 0)
    {

        perror("mprotect restore");
        exit(EXIT_FAILURE);
    }
}

void caesar(void *src, void *dst, int len)
{
    if (!src || !dst || len <= 0 || !protected_key)
        return;

    char local_key[KEY_SIZE];

    memcpy(local_key, protected_key, KEY_SIZE);

    unsigned char *s = (unsigned char *)src;
    unsigned char *d = (unsigned char *)dst;

    unsigned char key = (unsigned char)local_key[0];

    for (int i = 0; i < len; ++i)
    {
        d[i] = s[i] ^ key;
    }

    memset(local_key, 0, sizeof(local_key));
}

void test_protected_write(void)
{
    printf("Тест: попытка записи в защищённую память...\n");

    protected_key[0] = 'X';
}

void destroy_secure_key(void)
{
    if (!protected_key || protected_key == MAP_FAILED)
        return;

    if (mprotect(protected_key,
                 page_size,
                 PROT_READ | PROT_WRITE) != 0)
    {

        perror("mprotect destroy");
        exit(EXIT_FAILURE);
    }

    memset(protected_key, 0, KEY_SIZE);

    if (munmap(protected_key, page_size) != 0)
    {
        perror("munmap");
        exit(EXIT_FAILURE);
    }

    protected_key = NULL;
}