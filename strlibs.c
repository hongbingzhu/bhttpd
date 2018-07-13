#include <ctype.h>
#include <string.h>

char * str_strip_tail(char *str)
{
	char *ptr = str;
	while (*ptr != 0) ptr++;
	--ptr;
	while ((isspace(*ptr))) {
		*ptr = 0;
		ptr--;
	}
	return str;
}

int str_split(char *str, char split_char, char **ret, int max_ret)
{
	int ret_num = 0, i;
	memset(ret, 0, max_ret*sizeof(*ret));
	while (ret_num < max_ret) {
		*ret++ = str;
		ret_num++;
		while ((*str != split_char) && (*str != '\0')) str++;
		if (*str == '\0') break;
		*str = '\0'; str++;
		while (*str == split_char) str++;
		if (*str == '\0') break;
	}

	for (i = ret_num; i < max_ret; i++)	// all other pointers set to NULL string
		*ret++ = str;
	return ret_num;
}
