#include "asterisk.h"
#include "asterisk/paths.h"	/* use ast_config_AST_MONITOR_DIR */
#include "asterisk/file.h"
#include "asterisk/audiohook.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/autochan.h"
#include "asterisk/manager.h"
#include "asterisk/test.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"

#include <ifaddrs.h>
#include <curl/curl.h>
#include "cJSON.h"
#include "voicebase.h"

static char vb_api_key[1024];
static char vb_password[1024];
static char vb_public[1024];
static char vb_callback_url[2048];
static char vb_api_url[1024];
static char vb_title[1024];
static int  vb_segment_duration;
static char vb_ip_string[1024];
//static char vb_time_string[1024];

struct buf_t{
	int 	pos;
	char* 	buf;
	int 	buf_size;
};

void set_defaults(){
    /* Set the default values */
    memset(vb_api_key, 0, sizeof(vb_api_key));
    memset(vb_password, 0, sizeof(vb_password));
    memset(vb_public, 0, sizeof(vb_public));
    memset(vb_callback_url, 0, sizeof(vb_callback_url));
    memset(vb_title, 0, sizeof(vb_title));
 //   memset(vb_time_string, 0, sizeof(vb_time_string));
    strcpy(vb_api_url, "http://www.beta.voicebase.com");
    get_ip_string(vb_ip_string, sizeof(vb_ip_string));

    vb_segment_duration = 120;
}

static void get_time_string(char* result, int max_size){
	time_t t;
	time(&t);
	snprintf(result, max_size, "%d", (int)t);
}

char* get_safe_object_strings(cJSON *m, char* name, char* default_val){
	char* result = default_val;
	if (m && name){
		cJSON* element = cJSON_GetObjectItem(m,name);
		if (element){
			if (element->valuestring)
				result = element->valuestring;
		}
	}
	return result;
}

int get_safe_object_integer(cJSON *m, char* name){

	int result = 0;
	cJSON* element = cJSON_GetObjectItem(m,name);
	if (element){
		result = element->valueint;
	}
	return result;
}

static const char* get_simple_name(const char* name){
	int len = strlen(name);
	while (len > 1 && name[len - 1] != '/'){
		--len;
	}
	return &name[len];
}


static size_t RecvCallBack ( char *ptr, size_t size, size_t nmemb, char *data ) {
	struct buf_t* buf = (struct buf_t*)data;
	if (buf->pos + size*nmemb < buf->buf_size){
		memcpy(&buf->buf[buf->pos],ptr, size*nmemb);
		buf->pos += size*nmemb;
	}

	return size * nmemb;
}

static CURLcode curl_post_segment(	const char* version,
								const char* apikey,
								const char* password,
								const char* action,
								const char* callID,
								const char* segmentNumber,
								const char* finalSegment,
								const char* rtCallbackURL,
								const char* content_name,
								const char* content_buff,
								long content_size,
								const char* pub,
								const char* title,
								const char* time_str,
								const char* desc,
								const char* lang,
								const char* sourceUrl,
								const char* recordedDate,
								const char* externalId,
								const char* ownerId,
								const char* autoCreate,
								const char* humanRush,
								const char* transcriptType,

								char* status_str,
								int status_max_size){
	CURL *curl;
	CURLcode res;
	struct curl_httppost *formpost=NULL;
	struct curl_httppost *lastptr=NULL;

	struct buf_t buf;
	CURLFORMcode form_res;

	buf.pos = 0;
	buf.buf = status_str;
	buf.buf_size = status_max_size;
//	ast_mutex_lock(&curl_lock);

#define ADD_FORM_DATA(X, VAL) if (VAL) { form_res = curl_formadd(&formpost,  &lastptr,  CURLFORM_COPYNAME, X, CURLFORM_COPYCONTENTS, VAL,  CURLFORM_END);  \
					 ast_log(LOG_NOTICE, "%s = %s\n", (X), (VAL));	\
					}

	ADD_FORM_DATA("version", 		version);
	ADD_FORM_DATA("apikey", 		apikey);
	ADD_FORM_DATA("password", 		password);
	ADD_FORM_DATA("action", 		action);
	ADD_FORM_DATA("callID", 		callID);
	ADD_FORM_DATA("startTime", 		time_str);
	ADD_FORM_DATA("segmentNumber", 	segmentNumber);
	ADD_FORM_DATA("finalSegment", 	finalSegment);
	ADD_FORM_DATA("rtCallbackUrl", 	rtCallbackURL);
	ADD_FORM_DATA("transcriptType", transcriptType);
	ADD_FORM_DATA("public", 		pub);
	ADD_FORM_DATA("title", 			title);
	ADD_FORM_DATA("desc", 			desc);
	ADD_FORM_DATA("lang", 			lang);
	ADD_FORM_DATA("sourceUrl", 			sourceUrl);
	ADD_FORM_DATA("recordedDate", 			recordedDate);
	ADD_FORM_DATA("externalId", 			externalId);
	ADD_FORM_DATA("ownerId", 			ownerId);
	ADD_FORM_DATA("autoCreate", 			autoCreate);
	ADD_FORM_DATA("humanRush", 			humanRush);

#undef ADD_FORM_DATA

	form_res = curl_formadd(&formpost,
	               &lastptr,
	               CURLFORM_COPYNAME, "file",
	               CURLFORM_BUFFER, 		content_name,
	               CURLFORM_BUFFERPTR, 		content_buff,
	               CURLFORM_BUFFERLENGTH, 	content_size,
	               CURLFORM_END);

//	ast_log(LOG_NOTICE, "content name = %s, content size = %d, content_ptr=0x%X\n", content_name, (int)content_size, (int)content_buff);


//	ast_log(LOG_NOTICE, "11 = %d\n", (int)form_res);


//	ast_log(LOG_NOTICE, "13 = %d\n", (int)form_res);

	/* get a curl handle */
	curl = curl_easy_init();
	if(curl) {
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		res = curl_easy_setopt(curl, CURLOPT_URL, vb_api_url);
	//	ast_log(LOG_NOTICE, "a = %d\n", (int)res);
		/* Now specify the POST data */

		res = curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
	//	ast_log(LOG_NOTICE, "b = %d\n", (int)res);
	//	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1 );
		res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, RecvCallBack);
	//	ast_log(LOG_NOTICE, "c = %d\n", (int)res);

		res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	//	ast_log(LOG_NOTICE, "d = %d\n", (int)res);

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);
		/* Check for errors */
		if(res != CURLE_OK)
			ast_log(LOG_NOTICE, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		if (buf.pos > 0 && buf.pos < buf.buf_size){
			buf.buf[buf.pos] = 0;
		} else{
			buf.buf[0] = 0;
		}

		curl_formfree(formpost);
		/* always cleanup */
		curl_easy_cleanup(curl);
	}else{
	    ast_log(LOG_NOTICE, "Failed to do curl_easy_init()\n");
	}
//	ast_mutex_unlock(&curl_lock);
	return res;
}

int write_tag(char* ptr, char* tag){
	ptr[0] = tag[0];
	ptr[1] = tag[1];
	ptr[2] = tag[2];
	ptr[3] = tag[3];
	return 4;
}

int write_int(char* ptr, int val){
	int* iptr = (int*)ptr;
	iptr[0] = val;
	return 4;
}

int write_short(char* ptr, short val){
	short* iptr = (short*)ptr;
	iptr[0] = val;
	return 2;
}

int write_wav_header(char* buf, int buf_size, int samplerate, int bit_per_sample, int num_of_channels){ //return header size in bytes
	char* ptr = buf;

	ptr += write_tag(ptr, "RIFF");
	ptr += write_int(ptr, 0);
	ptr += write_tag(ptr, "WAVE");

	ptr += write_tag(ptr, "fmt ");
	ptr += write_int(ptr, 16);
	ptr += write_short(ptr, 1); //audio format linear PCM
	ptr += write_short(ptr, num_of_channels);
	ptr += write_int(ptr, samplerate);
	ptr += write_int(ptr, samplerate * num_of_channels * bit_per_sample / 8);
	ptr += write_short(ptr, num_of_channels * bit_per_sample / 8);
	ptr += write_short(ptr, bit_per_sample);

	ptr += write_tag(ptr, "data");
	ptr += write_int(ptr, 0);

	return ptr - buf;
}

void wav_header_data_size_fix(char* buf, int data_size){
	write_int(buf + 4, data_size + 36);
	write_int(buf + 40, data_size);
}

int create_mem_storage(struct mem_storage_t* mem_storage, const char * command_line){
	mem_storage->buf 		= ast_calloc(1, vb_segment_duration * 8000 * 2 + 16000);
	if (mem_storage->buf)
		mem_storage->buf_size 	= vb_segment_duration * 8000 * 2 + 16000;
	else
		mem_storage->buf_size 	= 0;
	mem_storage->count 			= 0;
	mem_storage->pos 			= 0;
	mem_storage->is_opened		= 0;

	get_time_string(mem_storage->time_string, sizeof(mem_storage->time_string));

	memset(mem_storage->session_id, 0, sizeof(mem_storage->session_id));
	ast_log(LOG_WARNING, "Allocated memory for storage buffer %d\n", (int)mem_storage->buf_size);
	mem_storage->params 	= cJSON_Parse(command_line);
	if (!mem_storage->params){
		ast_log(LOG_ERROR, "Failed to parse cli params '%s'\n", command_line);
	}
	return (mem_storage->buf != NULL);
}

int destroy_mem_storage(struct mem_storage_t* mem_storage){
	if (mem_storage->buf){
		ast_free(mem_storage->buf);
	}
	mem_storage->buf 		= NULL;
	mem_storage->buf_size 	= 0;
	mem_storage->count 		= 0;
	mem_storage->pos 		= 0;
	mem_storage->is_opened	= 0;
	if (mem_storage->params)
		cJSON_Delete(mem_storage->params);
	return 0;
}

int is_opened(struct mem_storage_t* mem_storage){
	return mem_storage->is_opened;
}

int put_data(struct mem_storage_t* mem_storage, struct ast_frame* frm){
	if (is_opened(mem_storage)){
		int size = ast_codec_get_samples(frm) * 2;//we use 16 bit per sample
		if (mem_storage->pos + size > mem_storage->buf_size)
			size = mem_storage->buf_size - mem_storage->pos;
		if (size < 0)
			size = 0;

		//ast_log(LOG_NOTICE, "Added frame with ptr=%x, size=%d\n", (int)frm->data.ptr, (int)size);

		memcpy(mem_storage->buf + mem_storage->pos, frm->data.ptr, size);
		mem_storage->pos += size;
	}
	return 0;
}

int put_silence(struct mem_storage_t* mem_storage, int num_of_silence_samples){
	if (is_opened(mem_storage)){
		int size = num_of_silence_samples * 2;//we use 16 bit per sample
		if (mem_storage->pos + size > mem_storage->buf_size)
			size = mem_storage->buf_size - mem_storage->pos;
		if (size < 0)
			size = 0;

		memset(mem_storage->buf + mem_storage->pos, 0, size);
		mem_storage->pos += size;
	}
	return 1;
}

int open_mem_storage(struct mem_storage_t* mem_storage, const char* session_id, int count, int pts){

	strncpy(mem_storage->session_id, get_simple_name(session_id), sizeof(mem_storage->session_id) - 1);
	mem_storage->session_id[sizeof(mem_storage->session_id) - 1] = 0;

	mem_storage->wav_header_size = mem_storage->pos = write_wav_header(mem_storage->buf, mem_storage->buf_size, 8000, 16, 1);
	ast_log(LOG_WARNING, "Storage opened. Header size = %d\n, session_id = %s\n", mem_storage->wav_header_size, mem_storage->session_id);
	mem_storage->is_opened = 1;
	mem_storage->count = count;
	mem_storage->pts = pts;

	return 1;
}

void get_ip_string(char* result, int max_size){
	 struct ifaddrs *ifaddr, *ifa;
	 int family, s;
	 char host[1024];
	 result[0] = 0;

	 getifaddrs(&ifaddr);

	 for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		 if (ifa->ifa_addr == NULL)
			 continue;

		 family = ifa->ifa_addr->sa_family;

	   /* For an AF_INET* interface address, display the address */

	    if (family == AF_INET || family == AF_INET6) {
		    s = getnameinfo(ifa->ifa_addr,
				    (family == AF_INET) ?  sizeof(struct sockaddr_in) :
										  sizeof(struct sockaddr_in6),
			 	    host, sizeof(host), NULL, 0, NI_NUMERICHOST);
		    if (strlen(host) > 0 && strcmp(host, "127.0.0.1") != 0){
		 	    strcpy(result, host);
		 	    break;
		    }
	    }
	 }

	 freeifaddrs(ifaddr);
}

int close_mem_storage(struct mem_storage_t* mem_storage, int last){

	char full_session_id[4096];
	char str_segment_number[1024];
	char sending_status[1024];
	char content_name[1024];
	char start_pts[1024];
	CURLcode res;
	char*	title = NULL;
	char*	desc = NULL;
	char* 	lang = NULL;
	char* 	sourceUrl = NULL;
	char* 	recordedDate = NULL;
	char*	externalId = NULL;
	char*	ownerId = NULL;
	char*	autoCreate = NULL;
	char* 	humanRush = NULL;
	char*	transcriptType = NULL;
	char*	rtCallbackUrl = NULL;
	char* 	callId = NULL;
	char*	apikey = NULL;
	char*	pw = NULL;
	char* 	pub = NULL;


	wav_header_data_size_fix(mem_storage->buf, mem_storage->pos - mem_storage->wav_header_size);

	snprintf(full_session_id, sizeof(full_session_id), "%s_%s_%s", mem_storage->session_id, vb_ip_string, mem_storage->time_string);
	snprintf(start_pts, sizeof(start_pts), "%d.%d", (int)mem_storage->pts/1000, (int)mem_storage->pts%1000);

	snprintf(content_name, sizeof(content_name), "%s_%d.wav", full_session_id, mem_storage->count);
	snprintf(str_segment_number, sizeof(str_segment_number), "%d", mem_storage->count);

	ast_log(LOG_WARNING, "trying to send storage data to voicebase %s\n", full_session_id);

	ast_log(LOG_WARNING, " api_key = %s\n password = %s\n full_session_id = %s\n segment_number = %s\n content name = %s\n public = %s\n title = %s\n",
			    vb_api_key, vb_password, full_session_id, str_segment_number, content_name, vb_public, vb_title);

	apikey			= get_safe_object_strings(mem_storage->params, "apikey", 			vb_api_key);
	pw				= get_safe_object_strings(mem_storage->params, "pw", 				vb_password);
	title			= get_safe_object_strings(mem_storage->params, "title", 			vb_title);
	callId			= get_safe_object_strings(mem_storage->params, "callId", 			full_session_id);
	pub				= get_safe_object_strings(mem_storage->params, "public", 			vb_public);
	rtCallbackUrl	= get_safe_object_strings(mem_storage->params, "rtCallbackUrl",	 	vb_callback_url);

	desc			= get_safe_object_strings(mem_storage->params, "desc", 				NULL);
	lang			= get_safe_object_strings(mem_storage->params, "lang", 				NULL);
	sourceUrl		= get_safe_object_strings(mem_storage->params, "sourceUrl", 		NULL);
	recordedDate	= get_safe_object_strings(mem_storage->params, "recordedDate", 		NULL);
	externalId		= get_safe_object_strings(mem_storage->params, "externalId", 		NULL);
	ownerId			= get_safe_object_strings(mem_storage->params, "ownerId", 			NULL);
	autoCreate		= get_safe_object_strings(mem_storage->params, "autoCreate", 		NULL);
	humanRush		= get_safe_object_strings(mem_storage->params, "humanRush", 		NULL);
	transcriptType	= get_safe_object_strings(mem_storage->params, "transcriptType", 	"machine");

	ast_log(LOG_NOTICE, "Filling request properties finished\n");

	res = curl_post_segment(	"1.1",
								apikey,
								pw,
								"uploadMedia",
								callId,
								str_segment_number,
								last ? "true" : "false",
								rtCallbackUrl,
								content_name,
								mem_storage->buf,
								mem_storage->pos,
								pub,
								title,
								start_pts,
								desc,
								lang,
								sourceUrl,
								recordedDate,
								externalId,
								ownerId,
								autoCreate,
								humanRush,
								transcriptType,
								sending_status,
								sizeof(sending_status));
//	if (res != CURLE_OK){
		ast_log(LOG_WARNING, "Sent data with session id %s to %s, returned status = %s, curl result=%d\n", full_session_id, vb_api_url, sending_status, (int)res);
//	}
	/*
	f = fopen(full_filename, "w");
	if (f){
		fwrite(mem_storage->buf, 1, mem_storage->pos, f);
		fclose(f);
	}else{
	    ast_log(LOG_WARNING, "Failed to do data dump to %s\n", full_filename);
	}*/
	mem_storage->is_opened = 0;
	return 1;
}

void set_vb_api_key(const char* key){
	if (key)
		strncpy(vb_api_key, key, sizeof(vb_api_key));
	else
		vb_api_key[0] = 0;
}
char* get_vb_api_key(){
	return vb_api_key;
}

void set_vb_password(const char* pass){
	if (pass)
		strncpy(vb_password, pass, sizeof(vb_password));
	else
		vb_password[0] = 0;
}
char* get_vb_password(){
	return vb_password;
}

void set_vb_public(const char* pub){
	if (pub)
		strncpy(vb_public, pub, sizeof(vb_public));
	else
		vb_public[0] = 0;
}

char* get_vb_public(){
	return vb_public;
}

void set_vb_callback_url(const char* url){
	if (url)
		strncpy(vb_callback_url, url, sizeof(vb_callback_url));
	else
		vb_callback_url[0] = 0;
}

char* get_vb_callback_url(){
	return vb_callback_url;
}

void set_vb_api_url(const char* api_url){
	if (api_url)
		strncpy(vb_api_url, api_url, sizeof(vb_api_url));
	else
		vb_api_url[0] = 0;
}
char* get_vb_api_url(){
	return vb_api_url;
}

void set_vb_title(const char* title){
	if (title)
		strncpy(vb_title, title, sizeof(vb_title));
	else
		vb_title[0] = 0;
}

char* get_vb_title(){
	return vb_title;
}

void set_vb_segment_duration(int duration){
	vb_segment_duration = duration;
}

int get_vb_segment_duration(){
	return vb_segment_duration;
}

void set_vb_ip_string(const char* ip_string){
	if (ip_string)
		strncpy(vb_ip_string, ip_string, sizeof(vb_ip_string));
	else
		vb_ip_string[0] = 0;
}
char* get_vb_ip_string(){
	return vb_ip_string;
}
