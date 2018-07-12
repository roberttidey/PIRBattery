/*
 R. J. Tidey 2017/02/22
 PIR battery based notifier
 Supports IFTTT and Easy IoT server notification
 Web software update service included
 WifiManager can be used to config wifi network
 
 */
#define ESP8266

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include "Base64.h"
#include <IFTTTMaker.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WiFiManager.h>

/*
Wifi Manager Web set up
If WM_NAME defined then use WebManager
*/
#define WM_NAME "PIRBatteryWebSetup"
#define WM_PASSWORD "password"
#ifdef WM_NAME
	WiFiManager wifiManager;
#endif
//uncomment to use a static IP
//#define WM_STATIC_IP 192,168,0,100
//#define WM_STATIC_GATEWAY 192,168,0,1

int timeInterval = 50;
#define WIFI_CHECK_TIMEOUT 30000
unsigned long elapsedTime;
unsigned long wifiCheckTime;
int reported = 0;

#define POWER_HOLD_PIN 13

#define AP_AUTHID "999999"
#define AP_SECURITY "?event=zoneSet&auth=999999"

//IFTT and request key words
#define MAKER_KEY "aaaaaaaaaaaaaaaa"

//For update service
String host = "esp8266-pir";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "password";

//bit mask for server support
#define EASY_IOT_MASK 1
#define BOILER_MASK 4
#define BELL_MASK 8
#define SECURITY_MASK 16
#define LIGHTCONTROL_MASK 32
#define RESET_MASK 64
#define PIRBATTERY_MASK 128
int serverMode = 1;

#define SLEEP_MASK 5

//AP definitions
#define AP_SSID "ssid"
#define AP_PASSWORD "password"
#define AP_MAX_WAIT 10
String macAddr;

#define AP_PORT 80

ESP8266WebServer server(AP_PORT);
ESP8266HTTPUpdateServer httpUpdater;
WiFiClient cClient;
WiFiClientSecure sClient;
IFTTTMaker ifttt(MAKER_KEY, sClient);

//Config remote fetch from web page
#define CONFIG_IP_ADDRESS  "192.168.0.7"
#define CONFIG_PORT        80
//Comment out for no authorisation else uses same authorisation as EIOT server
#define CONFIG_AUTH 1
#define CONFIG_PAGE "espConfig"
#define CONFIG_RETRIES 10

// EasyIoT server definitions
#define EIOT_USERNAME    "admin"
#define EIOT_PASSWORD    "password"
#define EIOT_IP_ADDRESS  "192.168.0.7"
#define EIOT_PORT        80
#define USER_PWD_LEN 40
char unameenc[USER_PWD_LEN];
String eiotNode = "-1";
String pirEvent = "-1";
String pirNotify = "-1";
String securityURL = "-1";
int gapDelay = 1000;
int securityDevice = 0;

void ICACHE_RAM_ATTR  delaymSec(unsigned long mSec) {
	unsigned long ms = mSec;
	while(ms > 100) {
		delay(100);
		ms -= 100;
		ESP.wdtFeed();
	}
	delay(ms);
	ESP.wdtFeed();
	yield();
}

void ICACHE_RAM_ATTR  delayuSec(unsigned long uSec) {
	unsigned long us = uSec;
	while(us > 100000) {
		delay(100);
		us -= 100000;
		ESP.wdtFeed();
	}
	delayMicroseconds(us);
	ESP.wdtFeed();
	yield();
}

/*
  Set up basic wifi, collect config from flash/server, initiate update server
*/
void setup() {
	Serial.begin(115200);
	digitalWrite(POWER_HOLD_PIN, 1);
	pinMode(POWER_HOLD_PIN, OUTPUT);
	char uname[USER_PWD_LEN];
	String str = String(EIOT_USERNAME)+":"+String(EIOT_PASSWORD);  
	str.toCharArray(uname, USER_PWD_LEN); 
	memset(unameenc,0,sizeof(unameenc));
	base64_encode(unameenc, uname, strlen(uname));
	Serial.println("Set up Web update service");
	wifiConnect(0);
	macAddr = WiFi.macAddress();
	macAddr.replace(":","");
	Serial.println(macAddr);
	getConfig();

	//Update service
	MDNS.begin(host.c_str());
	httpUpdater.setup(&server, update_path, update_username, update_password);
	server.begin();

	MDNS.addService("http", "tcp", 80);
	pinMode(SLEEP_MASK, INPUT_PULLUP);
	Serial.println("Set up complete");
}

/*
  Connect to local wifi with retries
  If check is set then test the connection and re-establish if timed out
*/
int wifiConnect(int check) {
	if(check) {
		if(WiFi.status() != WL_CONNECTED) {
			if((elapsedTime - wifiCheckTime) * timeInterval > WIFI_CHECK_TIMEOUT) {
				Serial.println("Wifi connection timed out. Try to relink");
			} else {
				return 1;
			}
		} else {
			wifiCheckTime = elapsedTime;
			return 0;
		}
	}
	wifiCheckTime = elapsedTime;
#ifdef WM_NAME
	Serial.println("Set up managed Web");
#ifdef WM_STATIC_IP
	wifiManager.setSTAStaticIPConfig(IPAddress(WM_STATIC_IP), IPAddress(WM_STATIC_GATEWAY), IPAddress(255,255,255,0));
#endif
	wifiManager.autoConnect(WM_NAME, WM_PASSWORD);
#else
	Serial.println("Set up manual Web");
	int retries = 0;
	Serial.print("Connecting to AP");
	#ifdef AP_IP
		IPAddress addr1(AP_IP);
		IPAddress addr2(AP_DNS);
		IPAddress addr3(AP_GATEWAY);
		IPAddress addr4(AP_SUBNET);
		WiFi.config(addr1, addr2, addr3, addr4);
	#endif
	WiFi.begin(AP_SSID, AP_PASSWORD);
	while (WiFi.status() != WL_CONNECTED && retries < AP_MAX_WAIT) {
		delaymSec(1000);
		Serial.print(".");
		retries++;
	}
	Serial.println("");
	if(retries < AP_MAX_WAIT) {
		Serial.print("WiFi connected ip ");
		Serial.print(WiFi.localIP());
		Serial.printf(":%d mac %s\r\n", AP_PORT, WiFi.macAddress().c_str());
		return 1;
	} else {
		Serial.println("WiFi connection attempt failed"); 
		return 0;
	} 
#endif
}

/*
  Get config from server
*/
void getConfig() {
	int responseOK = 0;
	int retries = CONFIG_RETRIES;
	String line = "";

	while(retries > 0) {
		clientConnect(CONFIG_IP_ADDRESS, CONFIG_PORT);
		Serial.print("Try to GET config data from Server for: ");
		Serial.println(macAddr);

		cClient.print(String("GET /") + CONFIG_PAGE + " HTTP/1.1\r\n" +
			"Host: " + String(CONFIG_IP_ADDRESS) + "\r\n" + 
		#ifdef CONFIG_AUTH
				"Authorization: Basic " + unameenc + " \r\n" + 
		#endif
			"Content-Length: 0\r\n" + 
			"Connection: close\r\n" + 
			"\r\n");
		int config = 100;
		int timeout = 0;
		while (cClient.connected() && timeout < 10){
			if (cClient.available()) {
				timeout = 0;
				line = cClient.readStringUntil('\n');
				if(line.indexOf("HTTP") == 0 && line.indexOf("200 OK") > 0)
					responseOK = 1;
				//Don't bother processing when config complete
				if (config >= 0) {
					line.replace("\r","");
					Serial.println(line);
					//start reading config when mac address found
					if (line == macAddr) {
						config = 0;
					} else {
						if(line.charAt(0) != '#') {
							switch(config) {
								case 0: host = line;break;
								case 1: serverMode = line.toInt();break;
								case 2: eiotNode = line;break;
								case 3: pirEvent = line;break;
								case 4: pirNotify = line;break;
								case 5: gapDelay = line.toInt();break;
								case 6: securityDevice = line.toInt();break;
								case 7:
									securityURL = line;
									Serial.println("Config fetched from server OK");
									config = -100;
									break;
									Serial.println("Config fetched from server OK");
									config = -100;
									break;
							}
							config++;
						}
					}
				}
			} else {
				delaymSec(1000);
				timeout++;
				Serial.println("Wait for response");
			}
		}
		cClient.stop();
		if(responseOK == 1)
			break;
		retries--;
	}
	Serial.println();
	Serial.println("Connection closed");
	Serial.print("host:");Serial.println(host);
	Serial.print("serverMode:");Serial.println(serverMode);
	Serial.print("eiotNode:");Serial.println(eiotNode);
	Serial.print("pirEvent:");Serial.println(pirEvent);
	Serial.print("gapDelay:");Serial.println(gapDelay);
	Serial.print("securityDevice:");Serial.println(securityDevice);
	Serial.print("securityURL:");Serial.println(securityURL);
}


/*
  Establish client connection
*/
void clientConnect(char* host, uint16_t port) {
	int retries = 0;
   
	while(!cClient.connect(host, port)) {
		Serial.print("?");
		retries++;
		if(retries > CONFIG_RETRIES) {
			Serial.print("Client connection failed:" );
			Serial.println(host);
			wifiConnect(0); 
			retries = 0;
		} else {
			delaymSec(5000);
		}
	}
}

/*
 Access a URL
*/
void getFromURL(String url, int retryCount) {
	String url_host;
	uint16_t url_port;
	char host[32];
	int portStart, addressStart;
	int retries = retryCount;
	int responseOK = 0;
	
	Serial.println("get from " + url);
	portStart = url.indexOf(":");
	addressStart = url.indexOf("/");
	if(portStart >=0) {
		url_port = (url.substring(portStart+1,addressStart)).toInt();
		url_host = url.substring(0,portStart);
	} else {
		url_port = 80;
		url_host = url.substring(0,addressStart);
	}
	strcpy(host, url_host.c_str());
	
	while(retries > 0) {
		clientConnect(host, url_port);
		cClient.print(String("GET ") + url.substring(addressStart) + " HTTP/1.1\r\n" +
			   "Host: " + url_host + "\r\n" + 
		#ifdef ACTION_USERNAME
				"Authorization: Basic " + action_nameenc + " \r\n" + 
		#endif
			   "Connection: close\r\n" + 
			   "Content-Length: 0\r\n" + 
			   "\r\n");
		delaymSec(100);
		while(cClient.available()){
			String line = cClient.readStringUntil('\r');
			if(line)
			Serial.print(line);
			if(line.indexOf("HTTP") == 0 && line.indexOf("200 OK") > 0)
				responseOK = 1;
		}
		cClient.stop();
		if(responseOK)
			break;
		else
			Serial.println("Retrying EIOT report");
		retries--;
	}
}

/*
 Check Security device and alert security if changed
*/

void notifySecurity(int securityState) {
	Serial.print("Notify security ");
	Serial.println(securityState);
	if(securityURL != "-1")
		getFromURL(securityURL + AP_SECURITY + "&value1=" + String(securityDevice) + "&value2=" + String(securityState), CONFIG_RETRIES);
}

/*
 Send notify trigger to IFTTT
*/
int ifttt_notify(String eventName, String value1, String value2, String value3) {
  if(ifttt.triggerEvent(eventName, value1, value2, value3)){
    Serial.println("Notification successfully sent");
	return 1;
  } else {
    Serial.println("Failed!");
	return 0;
  }
}

/*
 Send report to easyIOTReport
 if digital = 1, send digital else analog
*/
void easyIOTReport(String node, float value, int digital) {
	int retries = CONFIG_RETRIES;
	int responseOK = 0;
	String url = "/Api/EasyIoT/Control/Module/Virtual/" + node;
	
	// generate EasIoT server node URL
	if(digital == 1) {
		if(value > 0)
			url += "/ControlOn";
		else
			url += "/ControlOff";
	} else
		url += "/ControlLevel/" + String(value);

	Serial.print("POST data to URL: ");
	Serial.println(url);
	while(retries > 0) {
		clientConnect(EIOT_IP_ADDRESS, EIOT_PORT);
		cClient.print(String("POST ") + url + " HTTP/1.1\r\n" +
				"Host: " + String(EIOT_IP_ADDRESS) + "\r\n" + 
				"Connection: close\r\n" + 
				"Authorization: Basic " + unameenc + " \r\n" + 
				"Content-Length: 0\r\n" + 
				"\r\n");

		delaymSec(100);
		while(cClient.available()){
			String line = cClient.readStringUntil('\r');
			if(line)
			Serial.print(line);
			if(line.indexOf("HTTP") == 0 && line.indexOf("200 OK") > 0)
				responseOK = 1;
		}
		cClient.stop();
		if(responseOK)
			break;
		else
			Serial.println("Retrying EIOT report");
		retries--;
	}
	Serial.println();
	Serial.println("Connection closed");
}


/*
  Main loop to read temperature and publish as required
*/
void loop() {
	if(reported == 0) {
		if(eiotNode != "-1")
			easyIOTReport(eiotNode, 1, 1);
		if(pirEvent != "-1")
			ifttt_notify(pirEvent, pirNotify, String(securityDevice), "");
		if(securityURL != "-1")
			notifySecurity(1);
		delaymSec(gapDelay);
		if(eiotNode != "-1")
			easyIOTReport(eiotNode, 0, 1);
		if(serverMode == PIRBATTERY_MASK && digitalRead(SLEEP_MASK) == 1) {
		digitalWrite(POWER_HOLD_PIN, 0);
			ESP.deepSleep(0);
		}
		if(securityURL != "-1")
			notifySecurity(0);
	}
	reported = 1;
	delaymSec(1000);
	server.handleClient();
}
