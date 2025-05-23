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
  bool running = false; // True if the process has been commanded to start and hasn't finished/stopped
  float aim = 0.0;
  unsigned long dur = 0; // Total cooking duration in ms
  unsigned long scheduledProcessStartTime = 0; // When the process (heating) is scheduled to begin (millis() + start_diff from web)
  unsigned long durationPhaseStartTime = 0; // When the cooking duration timer actually starts (temp is close to aim)
  bool cookingDurationHasStarted = false; // Flag to indicate the duration timer has started
} settings;


void getInfo(WebServer &server, WebServer::ConnectionType type, char *, bool) {
  Log.info("Webserver: getInfo request");
  server.httpSuccess("application/json");
  char buffer[200]; // Increased buffer size for potentially more info
  unsigned long rem_dur = 0;
  const char* status_str = "idle";

  if(settings.running) {
    if (millis() < settings.scheduledProcessStartTime) {
      status_str = "scheduled";
      rem_dur = settings.dur; // Full duration is pending
    } else if (!settings.cookingDurationHasStarted) {
      status_str = "heating";
      rem_dur = settings.dur; // Full duration is pending
    } else {
      status_str = "cooking";
      unsigned long elapsed_cooking_time = millis() - settings.durationPhaseStartTime;
      if (settings.dur > elapsed_cooking_time) {
        rem_dur = settings.dur - elapsed_cooking_time;
      } else {
        rem_dur = 0; // Duration met or exceeded
      }
    }
    sprintf(buffer,"{\"temp\":%.1f,\"running\":%d,\"aim\":%.1f,\"dur\":%lu,\"rem_dur\":%lu,\"status\":\"%s\",\"cookingHasBegun\":%d}", 
            settings.temp, settings.running, settings.aim, settings.dur, rem_dur, status_str, settings.cookingDurationHasStarted);
  } else {
    // Could be 'idle' or 'finished', for simplicity using 'idle' if not running
    sprintf(buffer,"{\"temp\": %.1f,\"running\": %d,\"status\":\"%s\"}", settings.temp, settings.running, status_str);
  }
  server.printf(buffer);
}

void start(WebServer &server, WebServer::ConnectionType type, char *, bool) {
  Log.info("Webserver: start request");
  if (settings.running || type != WebServer::POST) { server.httpFail(); return;}
  bool readSuccess;
  char name[16], value[32];

  readSuccess = server.readPOSTparam(name, 16, value, 32);
  if(!readSuccess || strcmp(name,"start") != 0) {server.httpFail(); return;}
  unsigned long start_diff = std::stoul(value); // This is the delay before heating starts

  readSuccess = server.readPOSTparam(name, 16, value, 32);
  if(!readSuccess || strcmp(name,"dur") != 0) {server.httpFail(); return;}
  unsigned long dur_ms = std::stoul(value);

  readSuccess = server.readPOSTparam(name, 16, value, 32);
  if(!readSuccess || strcmp(name,"aim") != 0) {server.httpFail(); return;}
  float aim_temp = std::stof(value);

  if(server.readPOSTparam(name, 16, value, 32)) {server.httpFail(); return;}
  
  server.httpSuccess("application/json");
  server.print("{}");

  settings.running = true;
  settings.aim = aim_temp;
  settings.dur = dur_ms;
  settings.scheduledProcessStartTime = millis() + start_diff;
  settings.cookingDurationHasStarted = false;
  settings.durationPhaseStartTime = 0; // Reset this

  
  int dur_min_log = settings.dur / 60000;
  int start_min_log = start_diff / 60000;
  Log.info("Process initiated. Aim: %.1f C, Duration: %d min. Heating starts after offset: %d min. Cooking timer starts when temp is within 10C of aim.", settings.aim, dur_min_log, start_min_log);
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
  Log.info("Aim temperature updated to: %.1f °C", settings.aim);
  // If cookingDurationHasStarted is false, the condition for starting it will use the new aim.
  // If true, the heating will adjust to the new aim.
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
  int dur_min = settings.dur / 60000;
  Log.info("Duration updated to: %d min", dur_min);
  // The main loop() will use the new duration for the timer check.
}

void stop(WebServer &server, WebServer::ConnectionType type, char *, bool) {
  Log.info("Webserver: stop request");
  // Allow stopping even if it thinks it's not running, to be safe.
  // if (!settings.running && type == WebServer::POST) { // Original check
  //   server.httpFail(); 
  //   return;
  // }
  server.httpSuccess("application/json");
  server.print("{}");
  settings.running = false;
  settings.cookingDurationHasStarted = false; // Reset this flag
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
  int i = 0;
  bool tempIsValid = false;
  float currentTempC = settings.temp; // Use last known temp if read fails
  do {
      float tempRead = ds18b20.getTemperature(); // Returns temperature in Celsius
      if (ds18b20.crcCheck()) 
      {
          currentTempC = tempRead; // Update current temp
          settings.temp = currentTempC; // Store in global settings
          tempIsValid = true;
          break; 
      }
  } while (MAXRETRY > i++);
  
  if (!tempIsValid) 
  {
      Log.warn("Failed to read temperature after %d retries. Using last known temp: %.1fC. Heater control might be affected.", i, currentTempC);
  }

  // --- Main Process Logic ---
  bool shouldHeatOrCook = settings.running && (millis() >= settings.scheduledProcessStartTime);

  if (shouldHeatOrCook) {
      // Start cooking duration timer if conditions are met
      if (!settings.cookingDurationHasStarted && tempIsValid && fabs(currentTempC - settings.aim) <= 10.0f) {
          settings.durationPhaseStartTime = millis();
          settings.cookingDurationHasStarted = true;
          Log.info("Temp (%.1fC) near aim (%.1fC). Cooking duration timer of %lu ms started.", currentTempC, settings.aim, settings.dur);
      }

      // Check if the cooking duration has expired (only if duration timer has started)
      if (settings.cookingDurationHasStarted && settings.dur > 0 && (millis() - settings.durationPhaseStartTime >= settings.dur)) {
          Log.info("Cooking duration expired. Stopping process.");
          settings.running = false; // This will turn off LED/Relay in the else block below
          settings.cookingDurationHasStarted = false; // Reset for next run
      }
  }

  // --- LED and Relay Control ---
  if (settings.running && (millis() >= settings.scheduledProcessStartTime)) { // Process is active (heating or cooking)
      digitalWrite(led, HIGH); // Indicate active state

      if (tempIsValid) { // Only control relay if temp is valid
          const float effectiveSetpoint = settings.aim + 0.3f; // Using existing hysteresis
          if (currentTempC < effectiveSetpoint) {
              digitalWrite(relay, HIGH); // Turn heater ON
          } else {
              digitalWrite(relay, LOW);  // Turn heater OFF
          }
      } else {
          // Safety: If temperature reading is invalid during an active phase, turn off the heater.
          Log.warn("Invalid temperature reading during active phase; turning relay OFF for safety.");
          digitalWrite(relay, LOW);
      }
  } else { // Not running, or before scheduled heating start time, or just finished
      digitalWrite(led, LOW);
      digitalWrite(relay, LOW); // Ensure heater is OFF
      if (settings.running && millis() < settings.scheduledProcessStartTime) {
          Log.trace("Process scheduled, waiting for heating to start.");
      }
  }
}
