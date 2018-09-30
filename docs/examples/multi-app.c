/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 1998 - 2018, Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.haxx.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/
/* <DESC>
 * A basic application source code using the multi interface doing two
 * transfers in parallel.
 * </DESC>
 */

#include <stdio.h>
#include <string.h>

/* somewhat unix-specific */
#include <sys/time.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdbool.h> //bool support in C99

/* curl stuff */
#include <curl/curl.h>
/*
 * Download a HTTP file and upload an FTP file simultaneously.
 */

#define HTTPS_HANDLE 0   /* Index for the HTTP transfer */
#define FTP_HANDLE 1    /* Index for the FTP transfer */
#define SELF_SIGNED_HTTPS_HANDLE 2
#define UNSET_HANDLE 3
#define HANDLECOUNT 4   /* Number of simultaneous transfers */

struct MemoryStruct {
  char *memory;
  size_t size;
};

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  void *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }

  mem->memory = (char *)ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  fprintf(stderr, "realsize: %zd\n", realsize);

  return realsize;
}

int main(void) {
  CURL *handles[HANDLECOUNT];
  CURLM *multi_handle;

  int still_running = 0; /* keep number of running handles */
  int i;

  CURLMsg *msg; /* for picking up messages with the transfer status */
  int msgs_left; /* how many messages are left */

  FILE *https_output=NULL;

  struct MemoryStruct response_header;
  response_header.memory = (char *)malloc(1);  /* will be grown as needed by the realloc above */
  response_header.size = 0;    /* no data at this point */

  /* Allocate one CURL handle per transfer */
  for(i = 0; i<HANDLECOUNT; i++)
    handles[i] = curl_easy_init();

//HTTPS github
  curl_easy_setopt(handles[HTTPS_HANDLE], CURLOPT_URL, "https://api.github.com/");
  curl_easy_setopt(handles[HTTPS_HANDLE], CURLOPT_USERAGENT, "curl/7.42.0"); //github said: Please make sure your request has a User-Agent header

  //By default, body goes to stdout; header goes to NULL
  https_output = fopen("/dev/stderr", "wb"); //write response body to stderr
  if(!https_output) {
    fclose(https_output);
  }
  curl_easy_setopt(handles[HTTPS_HANDLE], CURLOPT_WRITEDATA, https_output);


//FTP
  curl_easy_setopt(handles[FTP_HANDLE], CURLOPT_URL, "ftp://speedtest.tele2.net/");
  //curl_easy_setopt(handles[FTP_HANDLE], CURLOPT_UPLOAD, 1L);
  curl_easy_setopt(handles[FTP_HANDLE], CURLOPT_WRITEDATA, stderr); //write response body to stderr


//HTTPS self signed
  curl_easy_setopt(handles[SELF_SIGNED_HTTPS_HANDLE], CURLOPT_URL, "https://10.74.68.201/controller/probe/ca-bundle/ios-core");
  struct curl_slist *chunk = NULL;
  chunk = curl_slist_append(chunk, "X-ACCESS-TOKEN: 6e459abf-6ff6-4b61-b1e1-5dbfa8de8b80");
  curl_easy_setopt(handles[SELF_SIGNED_HTTPS_HANDLE], CURLOPT_HTTPHEADER, chunk);

  curl_easy_setopt(handles[SELF_SIGNED_HTTPS_HANDLE], CURLOPT_HEADERFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(handles[SELF_SIGNED_HTTPS_HANDLE], CURLOPT_HEADERDATA , (void *)&response_header);

  curl_easy_setopt(handles[SELF_SIGNED_HTTPS_HANDLE], CURLOPT_WRITEDATA, stderr); //write response body to stderr
  //curl_easy_setopt(handles[SELF_SIGNED_HTTPS_HANDLE], CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(handles[SELF_SIGNED_HTTPS_HANDLE], CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(handles[SELF_SIGNED_HTTPS_HANDLE], CURLOPT_SSL_VERIFYHOST, 1L);


  /* init a multi stack */
  multi_handle = curl_multi_init();

  /* add the individual transfers */
  for(i = 0; i<HANDLECOUNT; i++)
    curl_multi_add_handle(multi_handle, handles[i]);

  int retry=3;
multi_perform:

  /* we start some action by calling perform right away */
  curl_multi_perform(multi_handle, &still_running);

  while(still_running) {
    //printf("# still_running hanlders: %d\n", still_running);

    struct timeval timeout;
    int rc; /* select() return code */
    CURLMcode mc; /* curl_multi_fdset() return code */

    fd_set fdread;
    fd_set fdwrite;
    fd_set fdexcep;
    int maxfd = -1;

    long curl_timeo = -1;

    FD_ZERO(&fdread);
    FD_ZERO(&fdwrite);
    FD_ZERO(&fdexcep);

    /* set a suitable timeout to play around with */
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    curl_multi_timeout(multi_handle, &curl_timeo);
    if(curl_timeo >= 0) {
      timeout.tv_sec = curl_timeo / 1000;
      if(timeout.tv_sec > 1)
        timeout.tv_sec = 1;
      else
        timeout.tv_usec = (curl_timeo % 1000) * 1000;
    }

    /* get file descriptors from the transfers */
    mc = curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

    if(mc != CURLM_OK) {
      fprintf(stderr, "curl_multi_fdset() failed, code %d.\n", mc);
      break;
    }

    /* On success the value of maxfd is guaranteed to be >= -1. We call
       select(maxfd + 1, ...); specially in case of (maxfd == -1) there are
       no fds ready yet so we call select(0, ...) --or Sleep() on Windows--
       to sleep 100ms, which is the minimum suggested value in the
       curl_multi_fdset() doc. */

    if(maxfd == -1) {
#ifdef _WIN32
      Sleep(100);
      rc = 0;
#else
      /* Portable sleep for platforms other than Windows. */
      struct timeval wait = { 0, 100 * 1000 }; /* 100ms */
      rc = select(0, NULL, NULL, NULL, &wait);
#endif
    }
    else {
      /* Note that on some platforms 'timeout' may be modified by select().
         If you need access to the original value save a copy beforehand. */
      rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
    }

    //printf("rc: %d\n",rc);

    switch(rc) {
    case -1:
      /* select error */
      break;
    case 0: /* timeout */
    default: /* action */
      curl_multi_perform(multi_handle, &still_running);
      break;
    }
  }

  bool cert_error=false;
  /* See how the transfers went */
  while((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
    if(msg->msg == CURLMSG_DONE) {
      int idx, found = 0;

      /* Find out which handle this message is about */
      for(idx = 0; idx<HANDLECOUNT; idx++) {
        found = (msg->easy_handle == handles[idx]);
        if(found)
          break;
      }

      double elapsed;
      curl_easy_getinfo(msg->easy_handle, CURLINFO_TOTAL_TIME, &elapsed);
      printf("\n\nhandler %d completed with status: %d total_time: %f\n", idx, msg->data.result, elapsed);

      if(SELF_SIGNED_HTTPS_HANDLE!=idx)
        continue;

//      if(msg->easy_handle->multi) {
//        fprintf(stdout, "%s Curl_easy.multi is not NULL\n", __func__);
//      }

      CURLMcode c;
      if(CURLE_SSL_CACERT == msg->data.result && retry) { //Get curl return code. Not HTTP status code
        fprintf(stdout, "%s retry in case of cert error: %d\n", __func__, msg->data.result);
        fprintf(stdout, "%s remaing retry: %d\n", __func__, --retry);

        c= curl_multi_remove_handle(multi_handle, msg->easy_handle);
        fprintf(stdout, "%s curl_multi_remove_handle return %d\n", __func__, c);

        c= curl_multi_add_handle(multi_handle, msg->easy_handle);
        fprintf(stdout, "%s curl_multi_add_handle return %d\n", __func__, c);

        cert_error=true;
        continue;
      }else if(msg->data.result!=CURLE_OK) {
        fprintf(stderr, "CURL error code: %d\n", msg->data.result);

        c= curl_multi_remove_handle(multi_handle, msg->easy_handle);
        fprintf(stdout, "%s curl_multi_remove_handle return %d\n", __func__, c);

        continue;
      }else{
        c= curl_multi_remove_handle(multi_handle, msg->easy_handle);
        fprintf(stdout, "%s curl_multi_remove_handle return %d\n", __func__, c);
      }

      int http_status_code=0;
      char* szUrl = NULL;

      curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_status_code);
      //curl_easy_getinfo(msg->easy_handle, CURLOPT_HEADERDATA, (void *)&response_header); //No need to call getinfo, you named it when calling curl_easy_setopt
      curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &szUrl);

      if(http_status_code==200) {
        //printf("200 OK for %s\n", szUrl);
        fprintf(stderr, "200 OK response_header\n%s\n", response_header.memory);
      } else {
        fprintf(stderr, "GET of %s returned http status code %d\n", szUrl, http_status_code);
      }
    }
  }

  if(cert_error){
    goto multi_perform;
  }

  curl_multi_cleanup(multi_handle);

  /* Free the CURL handles */
  for(i = 0; i<HANDLECOUNT; i++)
    curl_easy_cleanup(handles[i]);

  if(https_output) {
    fclose(https_output);
  }

  return 0;
}
