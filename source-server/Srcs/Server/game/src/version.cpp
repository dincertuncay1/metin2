#include <stdio.h>

void WriteVersion()
{
#ifndef __WIN32__
	FILE* fp = fopen("VERSION.txt", "w");

	if (fp)
	{
		fprintf(fp, "__GAME_VERSION__: %s\n", __GAME_VERSION__);
		fprintf(fp, "%s@%s:%s\n", "cHVjaGF0eQ==", __HOSTNAME__, __PWD__);
		fclose(fp);
	}
#endif
}
//martysama0134's 623a0779c74cb7565145d45548376308
