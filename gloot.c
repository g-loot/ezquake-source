#include <curl/curl.h>
#include "quakedef.h"
#include "fs.h"

#define BASE_URL "https://coin-gloot-dev.appspot.com/api/v1"
#define GAME_ID "5898192534634496"
#define MAX_TRIES 10

int Poll_Matches(void * params)
{
	for (int tries = 0; tries < MAX_TRIES; tries++)
	{
		Com_Printf("Testing....\n");
		Sys_MSleep(200);
	}
	return 0;
}

void Gloot_Init(void)
{
	if (Sys_CreateDetachedThread(Poll_Matches, NULL) < 0) {
		Com_Printf("Failed to create Poll_Matches thread\n");
	}
}






