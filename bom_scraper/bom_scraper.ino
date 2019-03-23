#include <Arduino.h>
#include <ESP8266WiFi.h>                                               // esp8266 wifi library
#include <TimeLib.h>                                                   // time library used for time conversion functions
#include <NTPClient.h>                                                 // ntpclient library
#include <WiFiUdp.h>                                                   // wifiudp library as requried by ntplient

/* connection paramaters */
static const char* WIFI_SSID = "your_wifi_ssid";                       // your wireless network name (SSID)
static const char* WIFI_PASS = "super_secret_key";                     // your Wi-Fi network password
static const char* NTP_ADDRESS = "au.pool.ntp.org";                    // address of local ntp server

/* connection paramaters */
#define MAX_CONN_ATTEMPTS 3                                            // maximum number of api/ntp connection attempts
#define MAX_CONN_TIME 15                                               // maxiumum time in seconds to attempt wifi connection

/* bom data paramaters */
#define DATA_HEADER "[data]"                                           // data header identifier string
#define DATA_OFFSET 2                                                  // offset index from header of desired observation
#define DATA_DELIM 0x2C                                                // delimiter character code
#define DATA_QUOTE 0x22                                                // quote character code
#define DATA_BUFSIZE 512                                               // size of buffer for observation data

/* bom data column indicies */
#define BOM_TIME_LOCAL 5                                               // index of local timestamp
#define BOM_TIME_UTC 6                                                 // index of utc timestamp
#define BOM_TEMP_APP 9                                                 // index of apparent temperature
#define BOM_CLOUD_OKTA 12                                              // index of cloud okta
#define BOM_WIND_GUST 16                                               // index of wind gust speed
#define BOM_TEMP_AIR 18                                                // index of air temperature
#define BOM_DEW_POINT 19                                               // index of dew point
#define BOM_PRES_MSL 22                                                // index of msl pressure
#define BOM_RAINFALL 24                                                // index of rainfall (since 9am)
#define BOM_HUM_REL 25                                                 // index of relative humidity
#define BOM_WIND_DIR 32                                                // index of wind direction
#define BOM_WIND_SPEED 33                                              // index of wind speed

/* http request paramaters */
static const char* HTTP_METHOD_GET = "GET";                            // http get method
static const char* HTTP_CONNECTION = "close";                          // http connection control string
static const char* HTTP_USER_AGENT = "ESP8266";                        // http user agent string
static const char* BOM_OBS_HOST = "www.bom.gov.au";                    // bom observations http host
static const char* BOM_OBS_PATH = "/fwo/IDV60901/IDV60901.94870.axf";  // bom observations http path

/* struct for performing http requests */
struct httpRequest {                                                   // struct containing all data for http request
  const char* method;                                                  // http method for http/api call
  const char* host;                                                    // http/api hostname
  const char* path;                                                    // http/api path
};

/* connect to specified wifi ssid */
bool connectWiFi(const char* ssid, const char* psk) {
  Serial.print("\r\nConneting to Wi-Fi");

  WiFi.mode(WIFI_STA);                                                 // set wifi to station mode
  WiFi.begin(ssid, psk);                                               // begin wifi connection

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
    if (micros() / 1000000 > MAX_CONN_TIME) {                          // abort connection attempt if maximum connection time exceeded
      return 0;                                                        // return indicating failure
    }
  }

  Serial.println(" ok!");
  return 1;                                                            // return indicating success
}

/* return current epoch-format time from ntp server */
uint32_t get_ntptime() {
  uint16_t i_attempts = 0;                                             // number of ntp connection attempts
  uint32_t i_time;                                                     // current ntp time
  WiFiUDP w_udp;                                                       // wifiudp object used by ntp client
  NTPClient n_client(w_udp, NTP_ADDRESS);                              // create ntpclient object


  /* initialise and update ntp */
  n_client.begin();                                                    // start ntp client

  while (!n_client.update()) {
    Serial.println("NTP client update failed!");
    i_attempts++;                                                      // advance attempt counter
    delay(500);                                                        // wait half a second between attempts

    if(i_attempts == MAX_CONN_ATTEMPTS) {                              // abandon if max connecion attempts reached
      return 0;
    }
  }

  i_time = n_client.getEpochTime();
  Serial.printf("Current time from NTP: %u\n", i_time);
  return i_time;
}

/* send http request using specified instance of WiFiClient */
bool sendHttpRequest(httpRequest http, WiFiClient* client) {
  Serial.print("Connecting to API...");
  if (client->connect(http.host, 80)) {                                // connect to api host
    Serial.println(" ok!");
    client->printf("%s %s HTTP/1.1\r\n", http.method, http.path);      // send http post command
    Serial.printf("%s %s HTTP/1.1\r\n", http.method, http.path);
    client->printf("Host: %s\r\n", http.host);                         // specify http host
    Serial.printf("Host: %s\r\n", http.host);
    client->printf("Connection: %s\r\n", HTTP_CONNECTION);             // specify non-persistent connection
    Serial.printf("Connection: %s\r\n", HTTP_CONNECTION);
    client->printf("User-Agent: %s\r\n", HTTP_USER_AGENT);
    Serial.printf("User-Agent: %s\r\n", HTTP_USER_AGENT);

    client->println();                                                 // send blank line (required)

    return 1;                                                          // return indicating success
  } else {

    Serial.println(" connection error!");
    return 0;                                                          // return indicating error
  }
}

/* send http request for bom obserations data via http using specified instance of WiFiClient */
void requestBomObservations(WiFiClient* client) {
  httpRequest h_bom_req;

  h_bom_req.method = HTTP_METHOD_GET;
  h_bom_req.host = BOM_OBS_HOST;
  h_bom_req.path = BOM_OBS_PATH;

  sendHttpRequest(h_bom_req, client);
}

/* get specified field item of axf data response via http using specified instance of WiFiClient */
char* getAxfDataResponse(const char* field, uint8_t index, WiFiClient* client) {
  char* s_response = (char*)malloc(DATA_BUFSIZE * sizeof(char*));      // char array containing current line of http response
  uint16_t i_line = 0;                                                 // index of current line of http response
  uint16_t i_field = 65535;                                            // index of desired axf field header

  while (client->connected()) {                                        // read response from api
    if (client->available()) {
      memset(s_response, 0, DATA_BUFSIZE);                             // clear response buffer
      client->readBytesUntil('\n', s_response, DATA_BUFSIZE);          // read data into buffer

      if (strcmp(s_response, field) == 0) {                            // test for desired field header
        i_field = i_line;
      }

      if (i_line == i_field + index) {                                 // test for desired offset from fierld header
        client->stop();
      }

      i_line++;                                                        // advance line index
    }
  }

  return s_response;
}

/* get item at specified field index from delimited char array */
char* getItemAt(const char* data, uint8_t index, char delim = DATA_DELIM) {
  char* s_item = '\0';                                                 // char array containing returned item
  uint8_t i_length = 0;                                                // length of field item
  uint16_t i_index = 0;                                                // index of field item
  uint16_t i_delim_curr = 0;                                           // index of current delimiter (end of field item)
  uint16_t i_delim_prev = 0;                                           // index of previous delimiter (start of field item)

  for (uint16_t i = 0; i < strlen(data); i++) {                        // iterate through each char of array
    if (data[i] == delim) {                                            // test if char is a delimiter
      i_delim_curr = i;

      if (index == i_index) {                                          // test if current field is the one requested
        if (data[i_delim_prev] == DATA_QUOTE) {                        // test for quotes and truncate if present
          i_delim_prev += 1;
          i_delim_curr -= 1;
        }

        i_length = i_delim_curr - i_delim_prev;                        // calculate length of field substring
        s_item = (char*)malloc((i_length + 1) * sizeof(char*));        // allocate char array for field substring
        memcpy(s_item, &data[i_delim_prev], i_length);                 // copy field substring to char array
        s_item[i_length] = '\0';                                       // terminate char array
        break;                                                         // go no further; we've achieved everything
      }

      i_delim_prev = i_delim_curr + 1;                                 // advance delimiter index
      i_index++;                                                       // advance field index
    }
  }

  return s_item;
}

uint32_t getObservationTime(const char* obs, uint8_t index) {
  char* s_obs_time;                                                    // string containnig time of bom observations
  uint32_t i_obs_y, i_obs_mo, i_obs_d, i_obs_h, i_obs_m;               // ints containing various elements of time/date
  tmElements_t t_obs_time;                                             // tmelements struct used for epoch conversion

  s_obs_time = getItemAt(obs, index);
  sscanf(s_obs_time, "%4u%2u%2u%2u%2u", &i_obs_y, &i_obs_mo, &i_obs_d, &i_obs_h, &i_obs_m);

  t_obs_time.Year = i_obs_y - 1970;
  t_obs_time.Month = i_obs_mo;
  t_obs_time.Day = i_obs_d;
  t_obs_time.Hour = i_obs_h;
  t_obs_time.Minute = i_obs_m;

  return makeTime(t_obs_time);
}

void setup() {
  char* s_bom_obs;                                                     // string containing all bom observations (csv)
  char* s_obs_wind;                                                    // bom observation string items
  float f_obs_appr, f_obs_rain, f_obs_temp;                            // bom observation float items
  uint8_t i_obs_gust, i_obs_wind;                                      // bom observation int items
  uint16_t i_obs_age;                                                  // bom observation age in seconds
  uint32_t i_obs_time;                                                 // bom observation timestamp, converted to epoch
  WiFiClient w_client;                                                 // wifi client for bom connection

  Serial.begin(74880);                                                 // start serial connection
  connectWiFi(WIFI_SSID, WIFI_PASS);                                   // connect to wifi

  requestBomObservations(&w_client);                                   // request bom observation data
  s_bom_obs = getAxfDataResponse(DATA_HEADER, DATA_OFFSET, &w_client); // recieve bom observation data

  f_obs_appr = atof(getItemAt(s_bom_obs, BOM_TEMP_APP));               // extract individual observations from received data
  f_obs_temp = atof(getItemAt(s_bom_obs, BOM_TEMP_AIR));
  f_obs_rain = atof(getItemAt(s_bom_obs, BOM_RAINFALL));
  i_obs_wind = atoi(getItemAt(s_bom_obs, BOM_WIND_SPEED));
  i_obs_gust = atoi(getItemAt(s_bom_obs, BOM_WIND_GUST));
  s_obs_wind = getItemAt(s_bom_obs, BOM_WIND_DIR);
  i_obs_time = getObservationTime(s_bom_obs, BOM_TIME_UTC);
  i_obs_age = get_ntptime() - i_obs_time;

  Serial.println(s_bom_obs);                                           // display data over serial
  Serial.printf("\r\nCurrent temperature: %.1f°C, apparent: %.1f°C\r\n", f_obs_temp, f_obs_appr);
  Serial.printf("Rainfall since 9AM: %.1f mm\r\n", f_obs_rain);
  Serial.printf("Wind speed: %u km/h %s, gusting to: %u km/h\r\n", i_obs_wind, s_obs_wind, i_obs_gust);
  Serial.printf("Observation time: %u (%u seconds old)", i_obs_time, i_obs_age);

  free(s_bom_obs);                                                     // free allocated memory returned by getAxfDataResponse
}

void loop() {}
