// TODO
// - Send user token
// - Parse json response
// - Wait for match to start matchId to get state
// - Connect to server
#include <curl/curl.h>
#include "quakedef.h"
#include "fs.h"

#define BASE_URL "https://coin-gloot-dev.appspot.com/api/v1"
#define GAME_ID "5898192534634496"
#define MAX_TRIES 10

//#define JOIN_URL BASE_URL + "/join?nickname=curl"
#define JOIN_URL "http://localhost:8000/join?nickname=curl"

// Used by curl to read server lists from the web
struct curl_buf
{
	char *ptr;
	size_t len;
};

static struct curl_buf *curl_buf_init(void)
{
	// Q_malloc handles errors and exits on failure
	struct curl_buf *curl_buf = Q_malloc(sizeof(struct curl_buf));
	curl_buf->len = 0;
	curl_buf->ptr = Q_malloc(curl_buf->len + 1);
	return curl_buf;
}

static void curl_buf_deinit(struct curl_buf *curl_buf)
{
	Q_free(curl_buf->ptr);
	Q_free(curl_buf);
}

static size_t curl_write_func( void *ptr, size_t size, size_t nmemb, void* buf_)
{
	struct curl_buf * buf = (struct curl_buf *)buf_;
	size_t new_len = buf->len + size * nmemb;

	// not checking for realloc errors since Q_realloc will exit on failure
	buf->ptr = Q_realloc(buf->ptr, new_len + 1);

	memcpy(buf->ptr + buf->len, ptr, size * nmemb);
	buf->ptr[new_len] = '\0';
	buf->len = new_len;

	return size * nmemb;
}

struct curl_buf* Get_Match_Data(void)
{
	CURL *curl;
	CURLcode res;
	struct curl_buf *curl_buf;

	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, JOIN_URL);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	}
	else {
		Com_Printf_State(PRINT_FAIL, "GLoog_Init() Can't init cURL\n");
		return NULL;
	}

	curl_buf = curl_buf_init();
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_func);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		Com_Printf("Gloot_Init(): Could not read URL %s\n", JOIN_URL);
		curl_easy_cleanup(curl);
		curl_buf_deinit(curl_buf);
		return NULL;
	}

	curl_easy_cleanup(curl);
	return curl_buf;
}

qbool Process_Response(struct curl_buf* curl_data)
{
	char *buf;
	qbool success = false;

	// Don't modify curl_buf as it might be used to create cache file
	buf = Q_malloc(sizeof(char) * curl_data->len);
	memcpy(buf, curl_data->ptr, sizeof(char) * curl_data->len);
	Com_Printf("Server response %s\n", buf);
	if (strstr(buf, "matchId") != NULL) {
		Com_Printf("Match found. Connecting...");
		success = true;
	} else {
		Com_Printf("Cannot connect to match");
		Com_Printf("Server response %s\n", buf);
	}

	Q_free(buf);
	return success;
}

int Poll_Matches(void * params)
{
	qbool success = false;
	for (int tries = 0; !success && tries < MAX_TRIES; tries++)
	{
		Com_Printf("Contacting server....\n");
		struct curl_buf *curl_data;
		curl_data = Get_Match_Data();
		if (curl_data == NULL) {
			return 0;
		}
		success = Process_Response(curl_data);
		curl_buf_deinit(curl_data);
		if (!success) Sys_MSleep(200);
	}
	return 0;
}

void Gloot_Init(void)
{
	if (Sys_CreateDetachedThread(Poll_Matches, NULL) < 0) {
		Com_Printf("Failed to create Poll_Matches thread\n");
	}
}






