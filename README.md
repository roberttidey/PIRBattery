# Battery PIR sensor
Battery PIR sensor module for esp8266 reporting to EasyIOT, IFTTT, and a web app

Features
Very low current consumption until PIR triggers
Sends IFTTT notification if wanted
Sends report to EasyIOT server if required
Can trigger other activity via a url
Configuration fetched from a web server file keyed on Mac Address of esp-8266 to allow for multiple units
Normally shuts down into low current mode after reporting but can be kept on for updating
Web update of software
Retries on network and server connection requests
Basic network conections controlled by wifiManager or can be manually set up in code.

Hardware
A 4.2V LiOn battery is used to power the PIR sensor and the ESP8266
The PIR has a 5V input but this is immediately regulated to 3.3V via a low drop out regulator abd will work with
inputs down to 3.5V
Unfortunately this regulator is very low current and not sufficien to power the ESP8266 as well.
Two solutions can be used 
a) Use a separate low drop out 3.3V regulator for the ESP8266
b) Unsolder the PIR regulator and replace with a pin compatible LDO regulator that can power the ESP8266

b) is my preferred solution as it makes the wiring much neater. I use XC6204E332 regulators which have very low drop out
and can supply up to 400mA

Configuration
The code needs some password and keys to be edited
#define WM_PASSWORD "password"  (to access Wifi Manager)
#define AP_SECURITY "?event=zoneSet&auth=999999" (trigger action URL)
#define MAKER_KEY "aaaaaaaaaaaaaaaa" (IFTTT access key)
const char* update_password = "password"; (firmware update password)
#define CONFIG_IP_ADDRESS  "192.168.0.7" (dynamic config server)

The module will access dynamic config from a web file espConfig
An example is included. The file can service multiple units as they are accessed by Mac address


