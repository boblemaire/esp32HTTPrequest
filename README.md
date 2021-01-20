# esp32HTTPrequest

HTTP and HTTPS for ESP32. 
Subset of HTTP.
Wrapper class for esp_http_client
Methods similar in format and use to XmlHTTPrequest in Javascript.

Supports:
* GET and POST
* HTTP and HTTPS methods
* Request and response headers
* Chunked response
* Single String response for short (<~5K) responses (heap permitting).
* optional onData callback.
* optional onReadyStatechange callback.
* can be transparently substituted for asyncHTTPrequest (see caveats below)

This library is a follow on to asyncHTTPrequest created for the ESP8266. Where the need on the ESP8266 was to avoid blocking, this code supports HTTPS. Since sharing the asyncHTTPrequest code, the most common inquiry has been HTTPS support.  This is a work-in-progress. It works for both HTTP and HTTPS, you only need to specify HTTPS in the URL and be sure there is 40K to 50K of heap available for the TLS handshake.

The underlying interface to TCP is changed from asyncTCP to the esp_http_client.  It's really just a class wrappper at this point providing a simplified way to use the native C based ESP-IDF http client. It is no longer asynchronous in the way it's predecessor was, however that isn't as important with ESP32 in that FREErtos tasks run asynchronously anyway. Instances of this class can be made to run asynchronously by simply running them in a FREErtos task, which at the end of the day is how the other async methods do it. The only caveat to running asynchronously is that any supplied contiguous data buffers (char*) must remain static.  Data supplied as String or xbuf will be copied to a static buffer.

This class should be plug compatible with the older asyncHTTPrequest. It will block during send() and unblock at completion (readystate = 4).  Send can be launched from a separate task to avoid blocking the main task.  Callbacks will still work either way.

I haven't had the time or motivation to test this beyond my immediate needs, but given the interest expressed in HTTPS for asyncHTTPrequest, I'm pubishing this work-in-progress in the chance that others may provide feedback and hopefully PRs to firm it up.



