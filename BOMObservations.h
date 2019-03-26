#ifndef BOMObservations_h
#define BOMObservations_h

/* bom data paramaters */
#define DATA_HEADER "[data]"                                       // data header identifier string
#define DATA_OFFSET 2                                              // offset index from header of first observation data set
#define DATA_BUFSIZE 512                                           // size of buffer for observation data
#define DATA_DELIM 0x2C                                            // delimiter character code
#define DATA_QUOTE 0x22                                            // quote character code

/* bom data column indicies */
#define BOM_TIME_LOCAL 5                                           // index of local timestamp
#define BOM_TIME_UTC 6                                             // index of utc timestamp
#define BOM_TEMP_APP 9                                             // index of apparent temperature
#define BOM_CLOUD_OKTA 12                                          // index of cloud okta
#define BOM_WIND_GUST 16                                           // index of wind gust speed
#define BOM_TEMP_AIR 18                                            // index of air temperature
#define BOM_DEW_POINT 19                                           // index of dew point
#define BOM_PRES_MSL 22                                            // index of msl pressure
#define BOM_RAINFALL 24                                            // index of rainfall (since 9am)
#define BOM_HUM_REL 25                                             // index of relative humidity
#define BOM_WIND_DIR 32                                            // index of wind direction
#define BOM_WIND_SPEED 33                                          // index of wind speed

/* http request paramaters */
#define HTTP_HOSTNAME "www.bom.gov.au"
#define HTTP_METHOD_GET "GET"                                      // http get method
#define HTTP_CONNECTION "close"                                    // http connection control string
#define HTTP_USER_AGENT "ESP8266"                                  // http user agent string

class BOMObservations {
  public:
    BOMObservations(WiFiClient* client);

    bool requestObservations(const char* path, uint8_t index);
    char* getItemAt(uint8_t index);

    private:
      struct httpRequest {                                           // struct containing data for http request
        const char* method;
        const char* host;
        const char* path;
    };

    WiFiClient* _client;
    char* _observations;

    bool sendHttpRequest(httpRequest http);
};

BOMObservations::BOMObservations(WiFiClient* client) {
  _client = client;
}

bool BOMObservations::sendHttpRequest(httpRequest http) {
  if (_client->connect(http.host, 80)) {                           // connect to api host
    _client->printf("%s %s HTTP/1.1\r\n", http.method, http.path); // send http post command
    _client->printf("Host: %s\r\n", http.host);                    // specify http host
    _client->printf("Connection: %s\r\n", HTTP_CONNECTION);        // specify non-persistent connection
    _client->printf("User-Agent: %s\r\n", HTTP_USER_AGENT);	       // specify user agent string
    _client->println();                                            // send blank line (required)

    return 1;                                                      // return indicating success
  } else {

    return 0;                                                      // return indicating error
  }
}

bool BOMObservations::requestObservations(const char* path, uint8_t index = 0) {
  bool b_result;
  uint16_t i_line = 0;                                             // index of current line of http response
  uint16_t i_field = 0xFFFF;	                                     // index of desired axf field header
  httpRequest h_bom_req;	                                         // http request stuct

  h_bom_req.method = HTTP_METHOD_GET;	                             // build http request
  h_bom_req.host = HTTP_HOSTNAME;
  h_bom_req.path = path;

  b_result = sendHttpRequest(h_bom_req);                           // send http request

  _observations = (char*)malloc(DATA_BUFSIZE * sizeof(char*));     // allocate char array to contain current line of http response

  while (_client->connected()) {                                   // read response from api
    if (_client->available()) {
      memset(_observations, 0, DATA_BUFSIZE);                      // clear response buffer
      _client->readBytesUntil('\n', _observations, DATA_BUFSIZE);  // read data into buffer

      if (strcmp(_observations, DATA_HEADER) == 0) {               // test for desired field header
        i_field = i_line + DATA_OFFSET;
      }

      if (i_line == i_field + index) {                             // test for desired offset from fierld header
        _client->stop();
      }

      i_line++;                                                    // advance line index
    }
  }

  return b_result;                                                 // return indicating success or failure
}

char* BOMObservations::getItemAt(uint8_t index) {
  char* s_item = '\0';                                             // char array containing returned item
  uint8_t i_length = 0;                                            // length of field item
  uint16_t i_index = 0;                                            // index of field item
  uint16_t i_delim_curr = 0;                                       // index of current delimiter (end of field item)
  uint16_t i_delim_prev = 0;                                       // index of previous delimiter (start of field item)

  for (uint16_t i = 0; i < strlen(_observations); i++) {           // iterate through each char of array
    if (_observations[i] == DATA_DELIM) {                          // test if char is a delimiter
      i_delim_curr = i;

      if (index == i_index) {                                      // test if current field is the one requested
        if (_observations[i_delim_prev] == DATA_QUOTE) {           // test for quotes and truncate if present
          i_delim_prev += 1;
          i_delim_curr -= 1;
        }

        i_length = i_delim_curr - i_delim_prev;                    // calculate length of field substring
        s_item = (char*)malloc((i_length + 1) * sizeof(char*));    // allocate char array for field substring
        memcpy(s_item, &_observations[i_delim_prev], i_length);    // copy field substring to char array
        s_item[i_length] = '\0';                                   // terminate char array
        break;                                                     // go no further; we've achieved everything
      }

      i_delim_prev = i_delim_curr + 1;                             // advance delimiter index
      i_index++;                                                   // advance field index
    }
  }

  return s_item;
}

#endif
