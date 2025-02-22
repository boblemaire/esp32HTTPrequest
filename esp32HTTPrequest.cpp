#include "esp32HTTPrequest.h"

// ESP32 does not seem to reliably handle multiple cocurrent TLS requests.
// This semaphore controls the number of concurrent requests.
// ESP32_HTTP_REQUEST_MAX_TLS can be set to allow more than one.
// The semaphore is created in the constructor of the first instance
// of esp32HTTPrequest and the handle is saved here.

SemaphoreHandle_t TLSlock_S = nullptr;

//**************************************************************************************************************
esp32HTTPrequest::esp32HTTPrequest()
    : _readyState(readyStateUnsent)
    , _HTTPcode(0)
    , _chunked(false)
    , _debug(DEBUG_IOTA_HTTP_SET)
    , _async(false)
    , _timeout(DEFAULT_RX_TIMEOUT)
    , _lastActivity(0)
    , _requestStartTime(0)
    , _requestEndTime(0)
    , _connectedHost(nullptr)
    , _connectedPort(-1)
    , _client(nullptr)
    , _contentLength(0)
    , _contentRead(0)
    , _readyStateChangeCB(nullptr)
    , _readyStateChangeCBarg(nullptr)
    , _onDataCB(nullptr)
    , _onDataCBarg(nullptr)
    , _URL(nullptr)
    , _cert_pem(nullptr)
    , _cert_len(0)
    , _useGlobalCAStore(false)
    , _request(nullptr), _response(nullptr), _headers(nullptr)
{
    DEBUG_HTTP("New request.");
#ifdef ESP32
    threadLock = xSemaphoreCreateRecursiveMutex();
    if( ! TLSlock_S){
        TLSlock_S = xSemaphoreCreateCounting(ESP32_HTTP_REQUEST_MAX_TLS, ESP32_HTTP_REQUEST_MAX_TLS);
    } 
#endif
}

//**************************************************************************************************************
esp32HTTPrequest::~esp32HTTPrequest(){
    if(_client){
        esp_http_client_cleanup(_client);
        _client = nullptr;
    }
    delete _headers;
    delete _request;
    delete _response;
    delete[] _connectedHost;
#ifdef ESP32
    vSemaphoreDelete(threadLock);
#endif
}

//**************************************************************************************************************
void    esp32HTTPrequest::setDebug(bool debug){
    if(_debug || debug) {
        _debug = true;
        DEBUG_HTTP("setDebug(%s) version %s\r\n", debug ? "on" : "off", esp32HTTPrequest_h);
    }
	_debug = debug;
}

//**************************************************************************************************************
bool    esp32HTTPrequest::debug(){
    return(_debug);
}

//**************************************************************************************************************
void    esp32HTTPrequest::setCert(const uint8_t* pem, size_t len){
    _cert_pem = pem;
    _cert_len = len;
}
//**************************************************************************************************************
void    esp32HTTPrequest::useGlobalCAStore(bool globalCA){
    _useGlobalCAStore = globalCA;
} 

//**************************************************************************************************************
bool	esp32HTTPrequest::open(const char* method, const char* url){
    DEBUG_HTTP("open(%s, %.*s)\r\n", method, strlen(url), url);
    if(_readyState != readyStateUnsent && _readyState != readyStateDone) {return false;}
    _requestStartTime = millis();
    delete _headers;
    delete _request;
    delete _response;
    _headers = nullptr;
    _response = nullptr;
    _request = nullptr;
    _chunked = false;
    _contentRead = 0;
    _readyState = readyStateUnsent;
    if( ! _parseURL(url)){
        DEBUG_HTTP("_parseURL failed\n");
        return false;
    }
    _addHeader("host", _URL->host);
    if (strcmp(method, "GET") == 0) {
        _HTTPmethod = HTTP_METHOD_GET;
    } else if (strcmp(method, "POST") == 0) {
        _HTTPmethod = HTTP_METHOD_POST;
    } else 
        return false;

    if(!_client){
        esp_http_client_config_t config;
        memset(&config, 0, sizeof(config));
        config.url = url,
        config.method = _HTTPmethod;
        config.event_handler = http_event_handle;
        config.user_data = this;
        config.buffer_size = HTTP_REQUEST_MAX_RX_BUFFER;
        config.cert_pem = (char*) _cert_pem;
        config.cert_len = _cert_len;
        config.use_global_ca_store = _useGlobalCAStore;
        _client = esp_http_client_init(&config);
        if(!_client){
           DEBUG_HTTP("client_init failed\n");
           return false;
        }
    }
    else {
        esp_http_client_set_method(_client, _HTTPmethod);
        esp_http_client_set_url(_client, url);
    }
    _lastActivity = millis();
    return true;
}
//**************************************************************************************************************
void    esp32HTTPrequest::onReadyStateChange(readyStateChangeCB cb, void* arg){
    _readyStateChangeCB = cb;
    _readyStateChangeCBarg = arg;
}

//**************************************************************************************************************
void	esp32HTTPrequest::setTimeout(int seconds){
    DEBUG_HTTP("setTimeout(%d)\r\n", seconds);
    _timeout = seconds;
}

//**************************************************************************************************************
bool	esp32HTTPrequest::send(){
    DEBUG_HTTP("send()\r\n");
    _seize;
    _send(_request, 0);
    _release;
    return true;
}

//**************************************************************************************************************
bool    esp32HTTPrequest::send(String body){
    DEBUG_HTTP("send(String) %s... (%d)\r\n", body.substring(0,16).c_str(), body.length());
    _seize;
    _request = (char *)ps_malloc(body.length()); //new char[body.length()];
    memcpy(_request, body.c_str(), body.length());
    _send(_request, body.length());
    _release;
    return true;
}

//**************************************************************************************************************
bool	esp32HTTPrequest::send(const char* body){
    DEBUG_HTTP("send(char*) %s.16... (%d)\r\n",body, strlen(body));
    _seize;
    _send(body, strlen(body));
    _release;
    return true;
}

//**************************************************************************************************************
bool	esp32HTTPrequest::send(const uint8_t* body, size_t len){
    DEBUG_HTTP("send(char*) %s.16... (%d)\r\n",(char*)body, len);
    _seize;
    _send((char*)body, len);
    _release;
    return true;
}

//**************************************************************************************************************
bool	esp32HTTPrequest::send(xbuf* body, size_t len){
    DEBUG_HTTP("send(char*) %s.16... (%d)\r\n", body->peekString(16).c_str(), len);
    _seize;
    _request = (char *)ps_malloc(len);
    body->read((uint8_t*)_request, len);
    _send(_request, len);
    _release;
    return true;
}

//**************************************************************************************************************
void    esp32HTTPrequest::abort(){
    DEBUG_HTTP("abort()\r\n");
    _seize;
    if(! _client) return;
    esp_http_client_cleanup(_client);
    _client = nullptr;
    _release;
}
//**************************************************************************************************************
int		esp32HTTPrequest::readyState(){
    return _readyState;
}

//**************************************************************************************************************
int	esp32HTTPrequest::responseHTTPcode(){
    return _HTTPcode;
}

//**************************************************************************************************************
String	esp32HTTPrequest::responseText(){
    DEBUG_HTTP("responseText() ");
    _seize;
    if( ! _response || _readyState < readyStateLoading || ! available()){
        DEBUG_HTTP("responseText() no data\r\n");
        _release;
        return String(); 
    }       
    size_t avail = available();
    String localString = _response->readString(avail);
    if(localString.length() < avail) {
        DEBUG_HTTP("!responseText() no buffer\r\n")
        _HTTPcode = HTTPCODE_TOO_LESS_RAM;
        esp_http_client_close(_client);
        _client = nullptr;
        return String();
    }
    _contentRead += localString.length();
    DEBUG_HTTP("responseText() %s... (%d)\r\n", localString.substring(0,16).c_str() , avail);
    _release;
    return localString;
}

//**************************************************************************************************************
size_t  esp32HTTPrequest::responseRead(uint8_t* buf, size_t len){
    if( ! _response || _readyState < readyStateLoading || ! available()){
        DEBUG_HTTP("responseRead() no data\r\n");
        return 0;
    } 
    _seize;
    size_t avail = available() > len ? len : available();
    _response->read(buf, avail);
    DEBUG_HTTP("responseRead() %.16s... (%d)\r\n", (char*)buf , avail);
    _contentRead += avail;
    _release;
    return avail;
}

//**************************************************************************************************************
size_t	esp32HTTPrequest::available(){
    if(_readyState < readyStateLoading) return 0;
    if(_chunked && (_contentLength - _contentRead) < _response->available()){
        return _contentLength - _contentRead;
    }
    return _response->available();
}

//**************************************************************************************************************
size_t	esp32HTTPrequest::responseLength(){
    if(_readyState < readyStateLoading) return 0;
    return _contentLength;
}

//**************************************************************************************************************
void	esp32HTTPrequest::onData(onDataCB cb, void* arg){
    DEBUG_HTTP("onData() CB set\r\n");
    _onDataCB = cb;
    _onDataCBarg = arg;
}

//**************************************************************************************************************
uint32_t esp32HTTPrequest::elapsedTime(){
    if(_readyState <= readyStateOpened) return 0;
    if(_readyState != readyStateDone){
        return millis() - _requestStartTime;
    }
    return _requestEndTime - _requestStartTime;
}

//**************************************************************************************************************
String esp32HTTPrequest::version(){
    return String(esp32HTTPrequest_h);
}

/*______________________________________________________________________________________________________________

               PPPP    RRRR     OOO    TTTTT   EEEEE    CCC    TTTTT   EEEEE   DDDD
               P   P   R   R   O   O     T     E       C   C     T     E       D   D
               PPPP    RRRR    O   O     T     EEE     C         T     EEE     D   D
               P       R  R    O   O     T     E       C   C     T     E       D   D
               P       R   R    OOO      T     EEEEE    CCC      T     EEEEE   DDDD
_______________________________________________________________________________________________________________*/

size_t  esp32HTTPrequest::_send(const char* body, size_t len){
    DEBUG_HTTP("_send() %d\r\n", len);
    if(_HTTPmethod == HTTP_METHOD_POST){
        _addHeader("Content-Length", String(len).c_str());
    }
    header* hdr = _headers;
    while(hdr){
        esp_http_client_set_header(_client, hdr->name, hdr->value);
        hdr = hdr->next;
    }
    delete _headers;
    _headers = nullptr;
    _requestLen = len;
    if(len){
      esp_http_client_set_post_field(_client, body, len);
    }
    bool isTLS = strcmp(_URL->scheme, "HTTPS") == 0;
    if(isTLS){
        xSemaphoreTake(TLSlock_S, portMAX_DELAY);
    }
    esp_err_t err;
    do {
        err = esp_http_client_perform(_client);
    } while (err == ESP_ERR_HTTP_EAGAIN);
    if(isTLS){
        xSemaphoreGive(TLSlock_S);
    }
    if(err != ESP_OK){
        _HTTPcode = HTTPCODE_PERFORM_FAILED;
        DEBUG_HTTP("perform failed  %s\r\n", esp_err_to_name(err));
        abort();
        _setReadyState(readyStateDone);
    }
    _lastActivity = millis(); 
    return len;
}

//**************************************************************************************************************
void  esp32HTTPrequest::_setReadyState(readyStates newState){
    if(_readyState != newState){
        _readyState = newState;          
        DEBUG_HTTP("_setReadyState(%d)\r\n", _readyState);
        if(_readyStateChangeCB){
            _readyStateChangeCB(_readyStateChangeCBarg, this, _readyState);
        }
    } 
}

//**************************************************************************************************************
bool  esp32HTTPrequest::_parseURL(const char* url){
    delete _URL;
    _URL = new URL;
    _URL->buffer = new char[strlen(url) + 8];
    char *bufptr = _URL->buffer;
    const char *urlptr = url;

        // Find first delimiter

    int seglen = strcspn(urlptr, ":/?");

        // scheme

    _URL->scheme = bufptr;
    if(! memcmp(urlptr+seglen, "://", 3)){
        while(seglen--){
            *bufptr++ = toupper(*urlptr++);
        }
        urlptr += 3;
        seglen = strcspn(urlptr, ":/?");
    }
    else {
        memcpy(bufptr, "HTTP", 4);
        bufptr += 4;
    }
    *bufptr++ = 0;

        // host

    _URL->host = bufptr;
    memcpy(bufptr, urlptr, seglen);
    bufptr += seglen;
    *bufptr++ = 0;
    urlptr += seglen;

        // port

    _URL->port = bufptr;
    if(*urlptr == ':'){
        urlptr++;
        seglen = strcspn(urlptr, "/?");
        memcpy(bufptr, urlptr, seglen);
        bufptr += seglen;
        urlptr += seglen;
    }
    *bufptr++ = 0;

        // path 

    _URL->path = bufptr;
    *bufptr++ = '/';
    if(*urlptr == '/'){
        seglen = strcspn(++urlptr, "?");
        memcpy(bufptr, urlptr, seglen);
        bufptr += seglen;
        urlptr += seglen;
    }
    *bufptr++ = 0;

        // query

    _URL->query = bufptr;
    if(*urlptr == '?'){
        seglen = strlen(urlptr);
        memcpy(bufptr, urlptr, seglen);
        bufptr += seglen;
        urlptr += seglen;
    }
    *bufptr++ = 0;

    DEBUG_HTTP("_parseURL() %s://%s:%s%s%.32s\r\n", _URL->scheme, _URL->host, _URL->port, _URL->path, _URL->query);
    return true;
}

/*______________________________________________________________________________________________________________

EEEEE   V   V   EEEEE   N   N   TTTTT         H   H    AAA    N   N   DDDD    L       EEEEE   RRRR     SSS
E       V   V   E       NN  N     T           H   H   A   A   NN  N   D   D   L       E       R   R   S 
EEE     V   V   EEE     N N N     T           HHHHH   AAAAA   N N N   D   D   L       EEE     RRRR     SSS
E        V V    E       N  NN     T           H   H   A   A   N  NN   D   D   L       E       R  R        S
EEEEE     V     EEEEE   N   N     T           H   H   A   A   N   N   DDDD    LLLLL   EEEEE   R   R    SSS 
_______________________________________________________________________________________________________________*/

// Event callback from esp_http_client.
// Pickup class context and jump into class handler.

esp_err_t http_event_handle(esp_http_client_event_t *evt)
{
    esp32HTTPrequest *_this = (esp32HTTPrequest*)evt->user_data;
    return _this->_http_event_handle(evt);
}

esp_err_t esp32HTTPrequest::_http_event_handle(esp_http_client_event_t * evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            DEBUG_HTTP("HTTP_EVENT_ERROR\n");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            DEBUG_HTTP("client connected event\n");
            _setReadyState(readyStateOpened);
            break;
        case HTTP_EVENT_HEADER_SENT:
            DEBUG_HTTP("headers sent event\n");
            break;
        case HTTP_EVENT_ON_HEADER:
            DEBUG_HTTP("header received event %s:%s\n", evt->header_key, evt->header_value);
            _addHeader(evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            DEBUG_HTTP("on-data event, len=%d\n", evt->data_len);
            _setReadyState(readyStateHdrsRecvd);
            _onData(evt->data, evt->data_len);
            break;
        case HTTP_EVENT_DISCONNECTED:
            DEBUG_HTTP("disconnect event\n");
            break;
        case HTTP_EVENT_ON_FINISH:
            DEBUG_HTTP("client finish event\n");
            _HTTPcode = esp_http_client_get_status_code(_client);
            _setReadyState(readyStateDone);
            delete _request;
            _request = nullptr;
            // String connection = respHeaderValue("connection");
            // if(!connection.equalsIgnoreCase("keep-alive")){
            //     esp_http_client_close(_client); 
            // }
            while(_onDataCB && _response->available()){
                _lastActivity = millis(); 
                _onDataCB(_onDataCBarg, this, available());
            }
            break;
    }
    return ESP_OK;
}

//**************************************************************************************************************
void  esp32HTTPrequest::_onData(void* Vbuf, size_t len){
    DEBUG_HTTP("_onData handler %.16s... (%d)\r\n",(char*) Vbuf, len);
    _seize;
    _lastActivity = millis();

    if(!_chunked && esp_http_client_is_chunked_response(_client)){
        _chunked = true;
        DEBUG_HTTP("Response is chunked.\n");
    } 
              
    if(! _response){
        _response = new xbuf;
        _contentRead = 0;
        if (_chunked){
            _contentLength = 0;
        }
        else {
            _contentLength = esp_http_client_get_content_length(_client);
        }
    }
    
                // Transfer data to xbuf

    _response->write((uint8_t*)Vbuf, len);
    if(_chunked){
        _contentLength += len;
    }

                // If there's data in the buffer and not Done,
                // advance readyState to Loading.

    if(_response->available() && _readyState != readyStateDone){
        _setReadyState(readyStateLoading);
    }

    _release;            

                // If onData callback requested, do so.

    while(_onDataCB && available() > 0){
        _onDataCB(_onDataCBarg, this, available());
    }
}

/*_____________________________________________________________________________________________________________

                        H   H  EEEEE   AAA   DDDD   EEEEE  RRRR    SSS
                        H   H  E      A   A  D   D  E      R   R  S   
                        HHHHH  EEE    AAAAA  D   D  EEE    RRRR    SSS
                        H   H  E      A   A  D   D  E      R  R       S
                        H   H  EEEEE  A   A  DDDD   EEEEE  R   R   SSS
______________________________________________________________________________________________________________*/

//**************************************************************************************************************
void	esp32HTTPrequest::setReqHeader(const char* name, const char* value){
    if(_readyState <= readyStateOpened && _headers){
        _addHeader(name, value);
    }
}

//**************************************************************************************************************
void	esp32HTTPrequest::setReqHeader(const char* name, const __FlashStringHelper* value){
    if(_readyState <= readyStateOpened && _headers){
        char* _value = _charstar(value);
        _addHeader(name, _value);
        delete[] _value;
    }
}

//**************************************************************************************************************
void	esp32HTTPrequest::setReqHeader(const __FlashStringHelper *name, const char* value){
    if(_readyState <= readyStateOpened && _headers){
        char* _name = _charstar(name);
        _addHeader(_name, value);
        delete[] _name;
    }
}

//**************************************************************************************************************
void	esp32HTTPrequest::setReqHeader(const __FlashStringHelper *name, const __FlashStringHelper* value){
    if(_readyState <= readyStateOpened && _headers){
        char* _name = _charstar(name);
        char* _value = _charstar(value);
        _addHeader(_name, _value);
        delete[] _name;
        delete[] _value;
    }
}

//**************************************************************************************************************
void	esp32HTTPrequest::setReqHeader(const char* name, int32_t value){
    if(_readyState <= readyStateOpened && _headers){
        setReqHeader(name, String(value).c_str());
    }
}

//**************************************************************************************************************
void	esp32HTTPrequest::setReqHeader(const __FlashStringHelper *name, int32_t value){
    if(_readyState <= readyStateOpened && _headers){
        char* _name = _charstar(name);
        setReqHeader(_name, String(value).c_str());
        delete[] _name;
    }
}

//**************************************************************************************************************
int		esp32HTTPrequest::respHeaderCount(){
    if(_readyState < readyStateHdrsRecvd) return 0;                                            
    int count = 0;
    header* hdr = _headers;
    while(hdr){
        count++;
        hdr = hdr->next;
    }
    return count;
}

//**************************************************************************************************************
char*   esp32HTTPrequest::respHeaderName(int ndx){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(ndx);
    if ( ! hdr) return nullptr;
    return hdr->name;
}

//**************************************************************************************************************
char*   esp32HTTPrequest::respHeaderValue(const char* name){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(name);
    if( ! hdr) return nullptr;
    return hdr->value;
}

//**************************************************************************************************************
char*   esp32HTTPrequest::respHeaderValue(const __FlashStringHelper *name){
    if(_readyState < readyStateHdrsRecvd) return nullptr;
    char* _name = _charstar(name);      
    header* hdr = _getHeader(_name);
    delete[] _name;
    if( ! hdr) return nullptr;
    return hdr->value;
}

//**************************************************************************************************************
char*   esp32HTTPrequest::respHeaderValue(int ndx){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(ndx);
    if ( ! hdr) return nullptr;
    return hdr->value;
}

//**************************************************************************************************************
bool	esp32HTTPrequest::respHeaderExists(const char* name){
    if(_readyState < readyStateHdrsRecvd) return false;      
    header* hdr = _getHeader(name);
    if ( ! hdr) return false;
    return true;
}

//**************************************************************************************************************
bool	esp32HTTPrequest::respHeaderExists(const __FlashStringHelper *name){
    if(_readyState < readyStateHdrsRecvd) return false;
    char* _name = _charstar(name);      
    header* hdr = _getHeader(_name);
    delete[] _name;
    if ( ! hdr) return false;
    return true;
}

//**************************************************************************************************************
String  esp32HTTPrequest::headers(){
    _seize;
    String _response = "";
    header* hdr = _headers;
    while(hdr){
        _response += hdr->name;
        _response += ':';
        _response += hdr->value;
        _response += "\r\n";
        hdr = hdr->next;
    }
    _response += "\r\n";
    _release;
    return _response;
}

//**************************************************************************************************************
esp32HTTPrequest::header*  esp32HTTPrequest::_addHeader(const char* name, const char* value){
    _seize;
    header* hdr = (header*) &_headers;
    while(hdr->next) {
        if(strcasecmp(name, hdr->next->name) == 0){
            header* oldHdr = hdr->next;
            hdr->next = hdr->next->next;
            oldHdr->next = nullptr;
            delete oldHdr;
        }
        else {
            hdr = hdr->next;
        }
    }
    hdr->next = new header;
    hdr->next->name = new char[strlen(name)+1];
    strcpy(hdr->next->name, name);
    hdr->next->value = new char[strlen(value)+1];
    strcpy(hdr->next->value, value);
    _release;
    return hdr->next;
}

//**************************************************************************************************************
esp32HTTPrequest::header* esp32HTTPrequest::_getHeader(const char* name){
    _seize;
    header* hdr = _headers;
    while (hdr) {
        if(strcasecmp(name, hdr->name) == 0) break;
        hdr = hdr->next;
    }
    _release;
    return hdr;
}

//**************************************************************************************************************
esp32HTTPrequest::header* esp32HTTPrequest::_getHeader(int ndx){
    _seize;
    header* hdr = _headers;
    while (hdr) {
        if( ! ndx--) break;
        hdr = hdr->next; 
    }
    _release;
    return hdr;
}

//**************************************************************************************************************
char* esp32HTTPrequest::_charstar(const __FlashStringHelper * str){
  if( ! str) return nullptr;
  char* ptr = new char[strlen_P((PGM_P)str)+1];
  strcpy_P(ptr, (PGM_P)str);
  return ptr;
}

