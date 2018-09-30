#include <iostream>
#include <curl/curl.h>
#include <string>

/*
Use setopt before perform and getinfo after perform.

https://curl.haxx.se/libcurl/c/curl_easy_setopt.html
https://curl.haxx.se/libcurl/c/curl_easy_getinfo.html
*/

/*
https://curl.haxx.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
This callback function gets called by libcurl as soon as there is data received that needs to be saved.
ptr points to the delivered data, and the size of that data is nmemb; size is always 1.

libcurl will use fwrite as CURLOPT_WRITEFUNCTION and FILE * to stdout as CURLOPT_WRITEDATA by default.

Notice std::strings fit so well to this callback function's parameter
*/

static size_t writeFunction(void *ptr, size_t size, size_t nmemb, std::string* data) {
    data->append((char*) ptr, size * nmemb);
    return size * nmemb;
}

CURLcode CurlGet() {
    CURLcode r = (CURLcode)CURLE_OK;
    auto curl = curl_easy_init();
    if (curl) {
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, "Accept: */*");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        //curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com");
        curl_easy_setopt(curl, CURLOPT_URL, "https://10.74.68.201"); //self signed cert: CURLE_SSL_CACERT
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
        //curl_easy_setopt(curl, CURLOPT_USERPWD, "xinsfang:password");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.42.0");
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        
        std::string response_string;
        std::string header_string;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_string);
        
        CURLcode res;
        int retry = 1;
easy_perform:
        res=curl_easy_perform(curl);

        printf("curl res: %d message: %s\n", res, curl_easy_strerror(res));

        //Call curl_easy_getinfo after perform, otherwise they are default value. (response_code and elapsed are default. url is correct.)
        char* url;
        long response_code;
        double elapsed;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &elapsed);
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);

        if(CURLE_OK == res) {
          std::cout<<"url: "<<url<<std::endl;
          std::cout<<"total_time: "<<elapsed<<std::endl;
          std::cout<<"response_code: "<<response_code<<std::endl;
          std::cout<<"response_string: "+response_string.substr(0,300)<<std::endl;
          std::cout<<"header: "+header_string.substr(0,100)<<std::endl;
          std::cout<<"print end"<<std::endl;

          curl_easy_cleanup(curl);
          curl = NULL;
        }else if(CURLE_SSL_CACERT == res && retry) { //retry in case of ca verify failure
          --retry;
          //update_ca();
          goto easy_perform;
        }else{
          
        }
        r = (CURLcode)res;
    }
    return r;
}

int main() {
  CURLcode r = CurlGet();
  printf("CurlGet: %d\n", r);
}
