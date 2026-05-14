#ifndef LIBCAESAR_H
#define LIBCAESAR_H

void init_secure_key(void);
void set_key(const char *key_str);
void caesar(void *src, void *dst, int len);
void destroy_secure_key(void);
void test_protected_write(void);

#endif