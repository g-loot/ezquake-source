#include <curl/curl.h>
#include <jansson.h>
#include "quakedef.h"
#include "fs.h"
#include "menu.h"
#include "utils.h"


#define BASE_URL "https://edge-dev.gloot.com/quake-matchmaking/api/v1"
/*#define BASE_URL "http://localhost:8080/api/v1"*/
#define GAME_ID "5898192534634496"
#define MAX_TRIES 10

#define AUTH_HEADER "Authorization: Bearer "
#define JOIN_URL      BASE_URL "/match"
#define GET_MATCH_URL BASE_URL "/match/"

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

char *trim(char *s) {
	char *ptr;
	if (!s)
		return NULL;   // handle NULL string
	if (!*s)
		return s;      // handle empty string
	for (ptr = s + strlen(s) - 1; (ptr >= s) && isspace(*ptr); --ptr);
	ptr[1] = '\0';
	return s;
}

struct curl_slist *Add_AuthHeader(struct curl_slist *headers, CURL *curl, char *access_token) {
	char auth_header[strlen(AUTH_HEADER) + strlen(access_token)];
	strcpy(auth_header, AUTH_HEADER);
	strcat(auth_header, trim(access_token));
	headers = curl_slist_append(headers, auth_header);
	/*curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);*/
	return headers;
}

struct curl_buf* Get_Match_Data(char *access_token)
{
	CURL *curl;
	CURLcode res;
	struct curl_buf *curl_buf;

	curl = curl_easy_init();
	if (curl) {
		char *name = Info_ValueForKey(cls.userinfo, "name");
		char url[200];
		strcpy(url, JOIN_URL);
		/*strcat(url, name);*/
		Com_Printf("match url: %s\n", url);
		/*curl_easy_setopt(curl, CURLOPT_URL, JOIN_URL);*/
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
		struct curl_slist * headers = NULL;
		headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
		headers = Add_AuthHeader(headers, curl, access_token);
		// add the header list
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); 
		// set body with the name
		char body[200];
		sprintf(body, "{\"name\":\"%s\"}\n", name);
		Com_Printf("JSON: %s", body);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, sizeof(char) * strlen(body));
	}
	else {
		Com_Printf_State(PRINT_FAIL, "GLoog_Init() Can't init cURL\n");
		return NULL;
	}

	curl_buf = curl_buf_init();
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_func);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);
	// post request
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		Com_Printf("Gloot_Init(): Could not read URL %s\n", JOIN_URL);
		Com_Printf("Error %d\n", res);
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
		token = Q_malloc(sizeof(char) * filesize + 0);
		strcpy(token, (char *) data);
		Q_free(data);
		Con_Printf("Access token found.");
	}
	return token;
}

qbool Connect_To_Match(const char *server_loc)
{
	Com_Printf("Match started! Connecting to server %s.\n", server_loc);
	Cbuf_AddText("connect ");
	Cbuf_AddText(server_loc); // add the command but dont send it.
	return true;
}

qbool Process_Join_Response(char *access_token, struct curl_buf* curl_data)
{
	char *json;
	qbool success = false;
	json_t *json_root = NULL;
	json_t *server_loc = NULL;
	json_error_t error;

	// Don't modify curl_data as it might be used to create cache file
	json = Q_malloc(sizeof(char) * curl_data->len);
	memcpy(json, curl_data->ptr, sizeof(char) * curl_data->len);
	// extract server location from json
	json_root = json_loads(json, JSON_DISABLE_EOF_CHECK, &error);
	if (json_root == NULL || !json_is_object(json_root)) {
		Com_Printf("error: cannot parse json 1\n%s\n", json);
		success = false;
	} else {
		server_loc = json_object_get(json_root, "serverLocation");
		if (!json_is_string(server_loc)) {
			Com_Printf("error: cannot parse json 2\n%s\n", json);
			success = false;
		} else {
			const char *server_loc_str = json_string_value(server_loc);
			Com_Printf("Match found: %s\n", server_loc_str);
			success = Connect_To_Match(server_loc_str);
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

