#include <curl/curl.h>
#include <jansson.h>
#include "quakedef.h"
#include "fs.h"
#include "menu.h"
#include "utils.h"


#define BASE_URL "https://coin-gloot-dev.appspot.com/api/v1"
#define GAME_ID "5898192534634496"
#define MAX_TRIES 10

//#define JOIN_URL BASE_URL + "/join?nickname=curl"
#define JOIN_URL "http://localhost:8000/join?nickname="
#define GET_MATCH_URL "http://localhost:8000/match/"
#define AUTH_HEADER "Authorization: Bearer "

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

void Add_AuthHeader(CURL *curl, char *access_token) {
	struct curl_slist *chunk = NULL;
	char auth_header[strlen(AUTH_HEADER) + strlen(access_token)];
	strcpy(auth_header, AUTH_HEADER);
	strcat(auth_header, access_token);
	chunk = curl_slist_append(chunk, auth_header);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
}

struct curl_buf* Get_Match_Data(char *access_token)
{
	CURL *curl;
	CURLcode res;
	struct curl_buf *curl_buf;

	curl = curl_easy_init();
	if (curl) {
		char url[200];
		char *name = Info_ValueForKey(cls.userinfo, "name");
		strcpy(url, JOIN_URL);
		strcat(url, name);
		Com_Printf("match url: %s\n", url);
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		// add header
		Add_AuthHeader(curl, access_token);
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

char *Load_AccessToken(void)
{
	char *token = NULL;
	int filesize;
	byte *data = (byte *) FS_LoadHeapFile ("gloot.jwt", &filesize);
	if (data != NULL) {
		token = Q_malloc(sizeof(char) * filesize + 1);
		strcpy(token, (char *) data);
		Q_free(data);
		Con_Printf("Access token found.");
	}
	return token;
}

qbool Process_Get_Match_Response(struct curl_buf* curl_data)
{
	char *json = NULL;
	qbool success = false;

	// Don't modify curl_data as it might be used to create cache file
	json = Q_malloc(sizeof(char) * curl_data->len);
	memcpy(json, curl_data->ptr, sizeof(char) * curl_data->len);
	// extract match state from json
	json_t *json_root = NULL;
	json_t *match_state = NULL;
	json_error_t error;

	json_root = json_loads(json, 0, &error);
	if (json_root == NULL || !json_is_object(json_root)) {
		Com_Printf("error: cannot parse json 3\n%s\n", json);
		success = false;
	} else {
		match_state = json_object_get(json_root, "state");
		if (!json_is_string(match_state)) {
			Com_Printf("error: cannot parse json 4\n%s\n", json);
			success = false;
		} else {
			const char *match_state_str = json_string_value(match_state);
			success = strcmp(match_state_str, "STARTED") == 0;
			if (success) {
				Com_Printf("Match started! Connecting to server.\n");
				const char *host = json_string_value(json_object_get(json_root, "hostname"));
				const char *port = json_string_value(json_object_get(json_root, "port"));
				Cbuf_AddText("connect ");
				Cbuf_AddText(va("%s:%s", host, port)); // add the command but dont send it.
			}
		}
	}
	if (json_root != NULL) {
		json_decref(json_root);
	}
	Q_free(json);
	return success;
}

qbool Wait_For_Match(char *access_token, const char *match_id)
{
	qbool success = false;
	CURL *curl;
	CURLcode res;
	struct curl_buf *curl_buf;

	curl = curl_easy_init();
	if (curl) {
		char match_url[strlen(GET_MATCH_URL) + strlen(match_id)];
		strcpy(match_url, GET_MATCH_URL);
		strcat(match_url, match_id);
		curl_easy_setopt(curl, CURLOPT_URL, match_url);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		Add_AuthHeader(curl, access_token);
	}
	else {
		Com_Printf_State(PRINT_FAIL, "Wait_For_Match() Can't init cURL\n");
		return false;
	}

	curl_buf = curl_buf_init();
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_func);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);

	for (int tries = 0; !success && tries < 60; tries++ )
	{
		Com_Printf("Waiting for match to start...\n");
		res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
			Com_Printf("Wait_For_Match(): Could not read URL %s\n", JOIN_URL);
			curl_easy_cleanup(curl);
			curl_buf_deinit(curl_buf);
			return false;
		}
		curl_easy_cleanup(curl);
		success = Process_Get_Match_Response(curl_buf);
		success = true;
		if (!success) Sys_MSleep(1000);
	}
	return success;
}

qbool Process_Join_Response(char *access_token, struct curl_buf* curl_data)
{
	char *json;
	qbool success = false;
	json_t *json_root = NULL;
	json_t *match_id = NULL;
	json_error_t error;

	// Don't modify curl_data as it might be used to create cache file
	json = Q_malloc(sizeof(char) * curl_data->len);
	memcpy(json, curl_data->ptr, sizeof(char) * curl_data->len);
	// extract matchId from json
	json_root = json_loads(json, 0, &error);
	if (json_root == NULL || !json_is_object(json_root)) {
		Com_Printf("error: cannot parse json 1\n%s\n", json);
		success = false;
	} else {
		match_id = json_object_get(json_root, "matchId");
		if (!json_is_string(match_id)) {
			Com_Printf("error: cannot parse json 2\n%s\n", json);
			success = false;
		} else {
			const char *match_id_str = json_string_value(match_id);
			Com_Printf("Match found: %s\n", match_id_str);
			success = Wait_For_Match(access_token, match_id_str);
		}
	}
	if (json_root != NULL) {
		json_decref(json_root);
	}
	Q_free(json);
	return success;
}

int Join_Match(void * params)
{
	char *access_token;
	access_token = Load_AccessToken();
	if (access_token == NULL) {
		Com_Printf("Missing access_token. Please add it to gloot.jwt\n");
		return 0;
	}

	qbool success = false;

	for (int tries = 0; !success && tries < MAX_TRIES; tries++)
	{
		Com_Printf("Contacting server....\n");
		struct curl_buf *curl_data;
		curl_data = Get_Match_Data(access_token);
		if (curl_data == NULL) {
			return 0;
		}
		success = Process_Join_Response(access_token, curl_data);
		curl_buf_deinit(curl_data);
		if (!success) Sys_MSleep(200);
	}
	if (success) {
		Cbuf_AddText("\n");
	} else {
		Com_Printf("Cannot connect to match. Please try again.\n");
	}
	M_LeaveMenus();
	Q_free(access_token);
	return 0;
}

void Gloot_Init(void)
{
	if (Sys_CreateDetachedThread(Join_Match, NULL) < 0) {
		Com_Printf("Failed to create Poll_Matches thread\n");
	}
}






