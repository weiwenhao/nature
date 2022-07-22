#ifndef NATURE_SRC_LIB_HELPER_H_
#define NATURE_SRC_LIB_HELPER_H_

#include "src/value.h"

//void string_to_unique_list(string *list, string value);

char *itoa(int n);

bool str_equal(string a, string b);

char *str_connect(char *a, char *b);

void str_replace(char *str, char from, char to);

char *file_read(char *path);

#endif //NATURE_SRC_LIB_HELPER_H_