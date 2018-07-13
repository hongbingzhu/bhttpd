#ifndef STRLIBS_H
#define STRLIBS_H

char * str_strip_tail(char *str);
int str_split(char *str, char split_char, char **ret, int max_ret);

#endif // STRLIBS_H
