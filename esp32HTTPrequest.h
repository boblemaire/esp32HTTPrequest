#ifndef esp32HTTPrequest_h
#define esp32HTTPrequest_h "1.1.16"

   /***********************************************************************************
    Copyright (C) <2018>  <Bob Lemaire, IoTaWatt, Inc.>
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.  
   
***********************************************************************************/
#include <Arduino.h>

#ifndef DEBUG_IOTA_PORT
#define DEBUG_IOTA_PORT Serial
#endif

#ifdef DEBUG_IOTA_HTTP
#define DEBUG_IOTA_HTTP_SET true
#else
#define DEBUG_IOTA_HTTP_SET false
#endif

#define _seize xSemaphoreTakeRecursive(threadLock,portMAX_DELAY)
#define _release xSemaphoreGiveRecursive(threadLock)

#include <pgmspace.h>
#include <functional>
#include <xbuf.h>
#include "esp_HTTP_client.h"

#define DEBUG_HTTP(format,...)  if(_debug){\
                                    DEBUG_IOTA_PORT.printf("Debug(%3ld): ", millis()-_requestStartTime);\
                                    DEBUG_IOTA_PORT.printf_P(PSTR(format),##__VA_ARGS__);}

#define DEFAULT_RX_TIMEOUT 3                    // Seconds for timeout
#define HTTP_REQUEST_MAX_RX_BUFFER 1040
#define HTTP_REQUEST_MAX_TX_BUFFER 1040

#define HTTPCODE_CONNECTION_REFUSED  (-1)
#define HTTPCODE_SEND_HEADER_FAILED  (-2)
#define HTTPCODE_SEND_PAYLOAD_FAILED (-3)
#define HTTPCODE_NOT_CONNECTED       (-4)
#define HTTPCODE_CONNECTION_LOST     (-5)
#define HTTPCODE_NO_STREAM           (-6)
#define HTTPCODE_NO_HTTP_SERVER      (-7)
#define HTTPCODE_TOO_LESS_RAM        (-8)
#define HTTPCODE_ENCODING            (-9)
#define HTTPCODE_STREAM_WRITE        (-10)
#define HTTPCODE_TIMEOUT             (-11)
#define HTTPCODE_PERFORM_FAILED      (-12)
#define HTTPCODE_OPEN_FAILED         (-13)

#ifndef ESP32_HTTP_REQUEST_MAX_TLS
  #define ESP32_HTTP_REQUEST_MAX_TLS 1
#endif

esp_err_t http_event_handle(esp_http_client_event_t *evt);
void HTTPperform(void *);

void esp32HTTPS_task(void *);

class esp32HTTPrequest {

  struct header {
	  header*	 	next;
	  char*			name;
	  char*			value;
	  header():
        next(nullptr), 
        name(nullptr), 
        value(nullptr)
        {};
	  ~header()
    {
        delete[] name; 
        delete[] value; 
        delete next;
    }
  };

  struct  URL {
      char *buffer;
      char *scheme;
      char *host;
      char *port;
      char *path;
      char *query;
      URL() 
        :buffer(nullptr)
        ,scheme(nullptr)
        ,host(nullptr)
        ,port(nullptr)
        ,path(nullptr)
        ,query(nullptr)
        {};
      ~URL()
      {
        delete[] buffer;
      }
    };

    typedef std::function<void(void*, esp32HTTPrequest*, int readyState)> readyStateChangeCB;
    typedef std::function<void(void*, esp32HTTPrequest*, size_t len)> onDataCB;
	
  public:
    esp32HTTPrequest();
    ~esp32HTTPrequest();

     
    //External functions in typical order of use:
    //__________________________________________________________________________________________________________*/
    void    setDebug(bool);                                         // Turn debug message on/off
    bool    debug();                                                // is debug on or off?
    void    async(bool set) { _async = set; }

    bool    open(const char* /*GET/POST*/, const char* URL);        // Initiate a request
    void    onReadyStateChange(readyStateChangeCB, void* arg = 0);  // Optional event handler for ready state change
                                                                    // or you can simply poll readyState()    
    void	  setTimeout(int);                                        // overide default timeout (seconds)
    void    setReqHeader(const char* name, const char* value);      // add a request header 
    void    setReqHeader(const char* name, const __FlashStringHelper* value);
    void    setReqHeader(const __FlashStringHelper *name, const char* value);
    void    setReqHeader(const __FlashStringHelper *name, const __FlashStringHelper* value);

    void    setReqHeader(const char* name, int32_t value);          // overload to use integer value
    void    setReqHeader(const __FlashStringHelper *name, int32_t value);     

    bool    send();                                                 // Send the request (GET)
    bool    send(String body);                                      // Send the request (POST)
    bool    send(const char* body);                                 // Send the request (POST)
    bool    send(const uint8_t* buffer, size_t len);                // Send the request (POST) (binary data?)
    bool    send(xbuf* body, size_t len);                            // Send the request (POST) data in an xbuf
    void    abort();                                                // Abort the current operation
    
    int     readyState();                                           // Return the ready state

    int     respHeaderCount();                                      // Retrieve count of response headers
    char*   respHeaderName(int index);                              // Return header name by index
    char*   respHeaderValue(int index);                             // Return header value by index
    char*   respHeaderValue(const char* name);                      // Return header value by name
    char*   respHeaderValue(const __FlashStringHelper *name);
    bool    respHeaderExists(const char* name);                     // Does header exist by name?
    bool    respHeaderExists(const __FlashStringHelper *name);
    String  headers();                                              // Return all headers as String

    void    onData(onDataCB, void* arg = 0);                        // Notify when min data is available
    size_t  available();                                            // response available
    size_t  responseLength();                                       // indicated response length or sum of chunks to date     
    int     responseHTTPcode();                                     // HTTP response code or (negative) error code
    String  responseText();                                         // response (whole* or partial* as string)
    size_t  responseRead(uint8_t* buffer, size_t len);              // Read response into buffer
    uint32_t elapsedTime();                                         // Elapsed time of in progress transaction or last completed (ms)
    String  version();                                              // Version of esp32HTTPrequest

    // DO NOT USE THIS FUNCTION!!

    esp_http_client_handle_t client() { return _client; };
    esp_err_t _http_event_handle(esp_http_client_event_t *evt);

    //___________________________________________________________________________________________________________________________________

  private:
  
    esp_http_client_method_t _HTTPmethod;
			
    enum    readyStates {
                readyStateUnsent = 0,           // Client created, open not yet called
                readyStateOpened =  1,          // open() has been called, connected
                readyStateHdrsRecvd = 2,        // send() called, response headers available
                readyStateLoading = 3,          // receiving, partial data available
                readyStateDone = 4} _readyState; // Request complete, all data available.

    int16_t         _HTTPcode;                  // HTTP response code or (negative) exception code
    bool            _chunked;                   // Processing chunked response
    bool            _debug;                     // Debug state
    bool            _async;                     // Perform using forked task
    uint32_t        _timeout;                   // Default or user overide RxTimeout in seconds
    uint32_t        _lastActivity;              // Time of last activity 
    uint32_t        _requestStartTime;          // Time last open() issued
    uint32_t        _requestEndTime;            // Time of last disconnect
    char*           _connectedHost;             // Host when connected
    int             _connectedPort;             // Port when connected
    esp_http_client_handle_t _client;           // ESPAsyncTCP AsyncClient instance
    
    size_t          _contentLength;             // content-length header value or sum of chunk headers  
    size_t          _contentRead;               // number of bytes retrieved by user since last open()
    readyStateChangeCB  _readyStateChangeCB;    // optional callback for readyState change
    void*           _readyStateChangeCBarg;     // associated user argument
    onDataCB        _onDataCB;                  // optional callback when data received
    void*           _onDataCBarg;               // associated user argument
    URL*            _URL;

    SemaphoreHandle_t threadLock;

    // request and response String buffers and header list (same queue for request and response).   

    char*       _request;                       // Tx data buffer for POST
    int         _requestLen;
    xbuf*       _response;                      // Rx data buffer
    header*     _headers;                       // request or (readyState > readyStateHdrsRcvd) response headers    

    // Protected functions

    header*     _addHeader(const char*, const char*);
    header*     _getHeader(const char*);
    header*     _getHeader(int);
    bool        _buildRequest();
    bool        _parseURL(const char*);
    bool        _parseURL(String);
    void        _processChunks();
    bool        _connect();
    size_t      _send(const char* body, size_t len);
    void        _setReadyState(readyStates);
    char*       _charstar(const __FlashStringHelper *str);
    void        _onData(void *, size_t);
};
#endif 