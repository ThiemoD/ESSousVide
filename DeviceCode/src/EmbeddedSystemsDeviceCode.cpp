// Include Particle Device OS APIs
#include "Particle.h"

#define WEBDUINO_OUTPUT_BUFFER_SIZE 40
#define WEBDUINO_FAVICON_DATA ""    // no favicon

#include <MDNS.h>
#include <WebDuino.h>
#include <DS18B20.h>
#include <math.h>
#include <cstdio>

// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(AUTOMATIC);  ///TODO: Set System_mode to Manual

const String hostname   = "sousvide";
const String remote_url = "http://google.at";

mdns::MDNS mDNS;
WebServer webserver("", 80);

void loadPage(WebServer &server, WebServer::ConnectionType type, char *, bool) {
  Log.info("Webserver: index.html request");
  server.httpSuccess();
  P(Server) = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><script type='text/javascript' src=\"https://cdn.jsdelivr.net/gh/ThiemoD/ESSousVide@latest/Website/mini.min.js\"></script></head><body></body></html>";
  server.printP(Server);
}

struct {
  float temp = 0.0;
  bool running = false;
  float aim = 0.0;
  unsigned long dur = 0;
  unsigned long start = 0;
} settings;


void getInfo(WebServer &server, WebServer::ConnectionType type, char *, bool) {
  Log.info("Webserver: getInfo request");
  server.httpSuccess("application/json");
  char buffer[128];
  if(settings.running)
  {
    unsigned long end = settings.start + settings.dur;
    unsigned long rem_dur = end > millis()? end-millis() : 0; 
    sprintf(buffer,"{\"temp\":%.1f,\"running\":%d,\"aim\":%.1f,\"dur\":%ld,\"rem_dur\":%ld}", settings.temp, settings.running, settings.aim, settings.dur, rem_dur);
  } 
  else 
    sprintf(buffer,"{\"temp\": %.1f,\"running\": %d}", settings.temp, settings.running);
  server.printf(buffer);
}

void start(WebServer &server, WebServer::ConnectionType type, char *, bool) {
  Log.info("Webserver: start request");
  if (settings.running || type != WebServer::POST) { server.httpFail(); return;}
  bool readSuccess;
  char name[16], value[32];

  readSuccess = server.readPOSTparam(name, 16, value, 32);
  if(!readSuccess || strcmp(name,"start") != 0) {server.httpFail(); return;}
  unsigned long start_diff = std::stoul(value);

  readSuccess = server.readPOSTparam(name, 16, value, 32);
  if(!readSuccess || strcmp(name,"dur") != 0) {server.httpFail(); return;}
  unsigned long dur = std::stoul(value);

  readSuccess = server.readPOSTparam(name, 16, value, 32);
  if(!readSuccess || strcmp(name,"aim") != 0) {server.httpFail(); return;}
  float aim = std::stof(value);

  if(server.readPOSTparam(name, 16, value, 32)) {server.httpFail(); return;}
  
  server.httpSuccess("application/json");
  server.print("{}");

  settings.running = true;
  settings.aim = aim;
  settings.dur = dur;
  settings.start = millis()+start_diff;
  //TODO: add starting code here
  int dur_min = settings.dur / 60000;
  int start_min = start_diff / 60000;
  Log.info("Process started with Aim: %.1f °C, Duration: %d min, Start offset: %d min", settings.aim, dur_min, start_min);
  // The main loop() will handle LED and relay based on these settings.

}

void setAim(WebServer &server, WebServer::ConnectionType type, char *, bool) {
  Log.info("Webserver: setAim request");
  if (!settings.running || type != WebServer::POST) { server.httpFail(); return;}

  char name[16], value[32];
  bool readSuccess = server.readPOSTparam(name, 16, value, 32);
  if(!readSuccess || strcmp(name,"aim") != 0) {server.httpFail(); return;}
  float aim = std::stof(value);

  if(server.readPOSTparam(name, 16, value, 32)) {server.httpFail(); return;}
  server.httpSuccess("application/json");
  server.print("{}");

  settings.aim = aim;
  //TODO: add code updating aim temperature
  Log.info("Aim temperature updated to: %.1f °C", settings.aim);


void setDur(WebServer &server, WebServer::ConnectionType type, char *, bool) {
  Log.info("Webserver: setDur request");
  if (!settings.running || type != WebServer::POST) { server.httpFail(); return;}

  char name[16], value[32];
  bool readSuccess = server.readPOSTparam(name, 16, value, 32);
  if(!readSuccess || strcmp(name,"dur") != 0) {server.httpFail(); return;}
  unsigned long dur = std::stoul(value);

  if(server.readPOSTparam(name, 16, value, 32)) {server.httpFail(); return;}
  server.httpSuccess("application/json");
  server.print("{}");

  settings.dur = dur;
  //TODO: add code updating duration
  int dur_min = settings.dur / 60000;
  Log.info("Duration updated to: %d min", dur_min);
  // The main loop() will use the new duration.
}

void stop(WebServer &server, WebServer::ConnectionType type, char *, bool) {
  Log.info("Webserver: stop request");
  if (!settings.running)
  {
    server.httpFail();
    return;
  }
  server.httpSuccess("application/json");
  server.print("{}");
  settings.running = false;
  //TODO: add stopping code here

  Log.info("Process stopped via web request.");
}


// Run the application and system concurrently in separate threads
SYSTEM_THREAD(ENABLED);

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
SerialLogHandler logHandler(LOG_LEVEL_INFO);

// setup() runs once, when the device is first turned on
void setup() {
    // WiFi.setCredentials("", "", WPA2);
    // WiFi.connect();
    // waitUntil(WiFi.ready);
 
  	bool mdns_success = mDNS.setHostname(hostname);

  	if(mdns_success) {
        mDNS.addService("tcp", "http", 80, hostname);
        mDNS.begin();
        mDNS.processQueries();
        Log.info("Set up mDNS");
  	}

  	// Webserver
  	webserver.setDefaultCommand(&loadPage);
  	webserver.addCommand("index.html", &loadPage);
  	webserver.addCommand("getInfo", &getInfo);
  	webserver.addCommand("setDur", &setDur);
  	webserver.addCommand("setAim", &setAim);
  	webserver.addCommand("start", &start);
  	webserver.addCommand("stop", &stop);
  	webserver.setFailureCommand(&loadPage);
  	webserver.begin();
    Log.info("Set up Webserver");
}

// loop() runs over and over again, as quickly as it can execute.
void loop() {
  mDNS.processQueries();
  char buff[64];
  int len = 64;

  webserver.processConnection(buff, &len);

  if(settings.running && millis() > settings.start + settings.dur) settings.running = false;
}
