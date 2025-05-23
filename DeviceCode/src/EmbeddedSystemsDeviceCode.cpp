// Include Particle Device OS APIs
#include "Particle.h"

#define WEBDUINO_OUTPUT_BUFFER_SIZE 40
#define WEBDUINO_FAVICON_DATA ""    // no favicon

#define MAXRETRY 3

#include <MDNS.h>
#include <WebDuino.h>
#include <DS18B20.h>
#include <math.h>
#include <cstdio>

// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(AUTOMATIC);  ///TODO: Set System_mode to Manual

//Init of Pins
DS18B20  ds18b20(D2, true); //Sets Pin D2 for Water Temp Sensor
int led = D3; //LED Pin
int relay = D4; //Relay Control

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

  //Debug output 
  int dur_min = dur / 60000;
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
  //Debug output
  Log.info("Aim temperature updated to: %.1f °C", settings.aim);
}

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

  //Debug output
  int dur_min = dur / 60000;
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
  digitalWrite(relay, LOW);   // Explicitly turn off relay
  digitalWrite(led, LOW);     // Explicitly turn off LED
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

    //Pin Setup
    pinMode(relay, OUTPUT);
    pinMode(led, OUTPUT);
    digitalWrite(led, LOW);

    //this might improve lag of webserver
    ds18b20.setResolution(9); // Set resolution to 9 bits, before at 12Bit to long of a wait time resulting in lag

}

// loop() runs over and over again, as quickly as it can execute.
void loop() {
  mDNS.processQueries();
  char buff[64];
  int len = 64;

  webserver.processConnection(buff, &len);

  //Get temperature from DS18B20 
  //takes in total 103ms to run 
  int i = 0;
  bool tempIsValid = false;
  do {
      float tempRead = ds18b20.getTemperature(); // Returns temperature in Celsius
      if (ds18b20.crcCheck()) 
      {
          settings.temp = tempRead;
          tempIsValid = true;
          break; 
      }
  } while (MAXRETRY > i++);
  
  if (!tempIsValid) 
  {
      Log.info("Failed to read temperature after %d retries. Heater control will be disabled until valid read.", i);
  }

  // Check if the cooking duration has expired
  if (settings.running && settings.dur > 0 && millis() >= settings.start && (millis() - settings.start >= settings.dur)) 
  {
      Log.info("Timer expired. Stopping process.");
      settings.running = false;
  }

  // Main control logic
  bool isProcessActive = settings.running && (millis() >= settings.start);

  if (isProcessActive) 
  { 
      digitalWrite(led, HIGH); // Indicate active state

      if (tempIsValid) 
      {
          const float effectiveSetpoint = settings.aim + 0.3f; 

          if (settings.temp < effectiveSetpoint) 
          {
              digitalWrite(relay, HIGH); // Turn heater ON
          } 
          else 
          {
              digitalWrite(relay, LOW);  // Turn heater OFF
          }
      }
      else 
      {
          // Safety: If temperature reading is invalid, turn off the heater.
          Log.info("Invalid temperature reading; turning relay OFF for safety.");
          digitalWrite(relay, LOW);
      }
  } 
  else 
  { // Not running
      digitalWrite(led, LOW);
      digitalWrite(relay, LOW); // Ensure heater is OFF
      if (settings.running && millis() < settings.start) 
      {
          Log.info("Process scheduled, waiting for start time.");
      }
  }

}
