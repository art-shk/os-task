#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>

typedef void (*set_key_func)(char);
typedef void (*caesar_func)(void *, void *, int);

int main(int argc, char *argv[])
{
    if (argc < 5)
    {
        fprintf(stderr, "Использование: %s <путь_к_библиотеке> <ключ> <входной_файл> <выходной_файл>\n", argv[0]);
        return 1;
    }

    const char *lib_path = argv[1];
    char key = argv[2][0];
    const char *input_file = argv[3];
    const char *output_file = argv[4];

    // Загрузка библиотеки
    void *handle = dlopen(lib_path, RTLD_LAZY);
    if (!handle)
    {
        fprintf(stderr, "Ошибка: %s\n", dlerror());
        return 1;
    }

    set_key_func set_key = (set_key_func)(uintptr_t)dlsym(handle, "set_key");
    caesar_func caesar = (caesar_func)(uintptr_t)dlsym(handle, "caesar");

    if (!set_key || !caesar)
    {
        fprintf(stderr, "Ошибка загрузки функций\n");
        dlclose(handle);
        return 1;
    }

    // Чтение файла
    FILE *infile = fopen(input_file, "rb");
    if (!infile)
    {
        fprintf(stderr, "Ошибка открытия: %s\n", input_file);
        dlclose(handle);
        return 1;
    }

    fseek(infile, 0, SEEK_END);
    long size = ftell(infile);
    fseek(infile, 0, SEEK_SET);

    char *buffer = (char *)malloc(size);
    if (!buffer)
    {
        fprintf(stderr, "Ошибка выделения памяти\n");
        fclose(infile);
        dlclose(handle);
        return 1;
    }

    fread(buffer, 1, size, infile);
    fclose(infile);

    // Шифрование
    set_key(key);
    caesar(buffer, buffer, (int)size);

    // Запись результата
    FILE *outfile = fopen(output_file, "wb");
    if (!outfile)
    {
        fprintf(stderr, "Ошибка создания: %s\n", output_file);
        free(buffer);
        dlclose(handle);
        return 1;
    }

    fwrite(buffer, 1, size, outfile);
    fclose(outfile);
    free(buffer);
    dlclose(handle);

    return 0;
}