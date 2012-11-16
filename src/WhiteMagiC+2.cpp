#include <iostream>
#include <sstream>
#include <string>
#include <stdint.h>
#include <signal.h>
#include <vector>
#include <map>
#include <pthread.h>
#include <MQTTClient.h>
#include <SerialStream.h>

// redefinition because original was in C style and don't work here in C++
#define MQTTClient_message_initializer   { {'M','Q','T','M'}, 0, 0, NULL, 0, 0, 0, 0 }
#define MQTTClient_connectOptions_initializer   { {'M','Q','T','C'}, 0, 60, 1, 1, NULL, NULL, NULL, 30, 20 }

// MQTT settings
#define ADDRESS     "tcp://localhost:1883"
#define DEVICE_ID   "WhiteMagic1"
#define QOS         0

// function bitmask constants
#define SET_COUNTER 0x80 // bit 8
#define SET_STATUS 0x40 // bit 7
#define SET_PWM 0x20 // bit 6
#define SET_AUTO 0x10 // bit 5

using namespace std;

class FunktorBase;

volatile MQTTClient_deliveryToken deliveredtoken;
MQTTClient client;
LibSerial::SerialStream my_serial_stream;
map<std::string, FunktorBase*> functions;
bool loop = true;

template <typename t>
string AnythingToStr(t value){
    ostringstream tmp;
    tmp << value;
    return tmp.str();
}

struct WhiteMagicStatus{
	bool power;
	uint8_t counter;
	uint8_t lamps[4];
	bool automatic;
	bool master;
	bool relative;
}WhiteMagic = {true, 13, {255, 255, 255, 255}, true, false, false};

class FunktorBase{
public:
	virtual void operator()(const string&) = 0;
	virtual ~FunktorBase(){}
protected:
	WhiteMagicStatus* status;
};

class Lamp: public FunktorBase{
public:
	explicit Lamp(uint8_t lampMask): lamp(lampMask){
		status = &WhiteMagic;
	}
	void operator()(const string& payload){
		stringstream valueStream(payload);
		short value;
		valueStream >> value;
		if(value == status->lamps[lamp] || value < 0 || value > 255)
			return;
		if(status->master)
			handleMaster(value);
		if(status->relative)
			handleRelative(value);
		uint8_t message[2];
		message[0] = SET_PWM | lamp;
		message[1] = value;
		my_serial_stream.write(reinterpret_cast<const char *>(message), 2);
		string answer = AnythingToStr(value);
		MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe ").append(AnythingToStr(static_cast<short>(lamp + 1))).c_str()), answer.length(), static_cast<void*>(const_cast<char*>(answer.c_str())), QOS, 1, NULL);
		status->lamps[lamp] = value;
	}
protected:
	void handleMaster(uint8_t value){
		uint8_t message[2];
		message[1] = value;
		string answer = AnythingToStr(static_cast<short>(value));
		for(short i = 0; i < 4; ++i){
			if(i == lamp)
				continue;
			message[0] = SET_PWM | i;
			my_serial_stream.write(reinterpret_cast<const char *>(message), 2);
			status->lamps[i] = value;
			MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe ").append(AnythingToStr(i + 1)).c_str()), answer.length(), static_cast<void*>(const_cast<char*>(answer.c_str())), QOS, 1, NULL);
		}
	}
	void handleRelative(uint8_t value){
		uint8_t message[2];
		short relativeValue;
		string answer;
		short offset = value - status->lamps[lamp];
		for(short i = 0; i < 4; ++i){
			if(i == lamp)
				continue;
			relativeValue = status->lamps[i] + offset;
			relativeValue = relativeValue > 255 ? 255 : relativeValue;
			relativeValue = relativeValue < 0 ? 0 : relativeValue;
			message[0] = SET_PWM | i;
			message[1] = relativeValue;
			my_serial_stream.write(reinterpret_cast<const char *>(message), 2);
			status->lamps[i] = relativeValue;
			answer = AnythingToStr(relativeValue);
			MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe ").append(AnythingToStr(i + 1)).c_str()), answer.length(), static_cast<void*>(const_cast<char*>(answer.c_str())), QOS, 1, NULL);
		}
	}
	uint8_t lamp;
}lamp1(0), lamp2(1), lamp3(2), lamp4(3);

class MasterCheckbox: public FunktorBase{
public:
	explicit MasterCheckbox(){
		status = &WhiteMagic;
	}
	void operator()(const string& payload){
		stringstream valueStream(payload);
		bool value;
		valueStream >> value;
		string answer;
		if(status->relative){
			status->relative = false;
			answer = AnythingToStr(false);
			MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/relative").c_str()), answer.length(), static_cast<void*>(const_cast<char*>(answer.c_str())), QOS, 1, NULL);
		}
		status->master = value;
		answer = AnythingToStr(value);
		MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/master").c_str()), answer.length(), static_cast<void*>(const_cast<char*>(answer.c_str())), QOS, 1, NULL);
	}
}master;

class RelativeCheckbox: public FunktorBase{
public:
	explicit RelativeCheckbox(){
		status = &WhiteMagic;
	}
	void operator()(const string& payload){
		stringstream valueStream(payload);
		bool value;
		valueStream >> value;
		string answer;
		if(status->master){
			status->master = false;
			string answer = AnythingToStr(false);
			MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/master").c_str()), answer.length(), static_cast<void*>(const_cast<char*>(answer.c_str())), QOS, 1, NULL);
		}
		status->relative = value;
		answer = AnythingToStr(value);
		MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/relative").c_str()), answer.length(), static_cast<void*>(const_cast<char*>(answer.c_str())), QOS, 1, NULL);
	}
}relative;

class Power: public FunktorBase{
public:
	explicit Power(){
		status = &WhiteMagic;
	}
	void operator()(const string& payload){
		stringstream valueStream(payload);
		bool value;
		valueStream >> value;
		uint8_t message[2];
		message[0] = SET_STATUS;
		message[1] = value;
		my_serial_stream.write(reinterpret_cast<const char *>(message), 2);
		string answer = AnythingToStr(value);
		status->power = value;
		MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/power").c_str()), answer.length(), static_cast<void*>(const_cast<char*>(answer.c_str())), QOS, 1, NULL);
	}
}power;

class Automatic: public FunktorBase{
public:
	explicit Automatic(){
		status = &WhiteMagic;
	}
	void operator()(const string& payload){
		stringstream valueStream(payload);
		bool value;
		valueStream >> value;
		uint8_t message[2];
		message[0] = SET_AUTO;
		message[1] = value;
		my_serial_stream.write(reinterpret_cast<const char *>(message), 2);
		string answer = AnythingToStr(value);
		status->automatic = value;
		MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/automatic").c_str()), answer.length(), static_cast<void*>(const_cast<char*>(answer.c_str())), QOS, 1, NULL);
	}
}automatic;

vector<string>& split(const string &s, char delimiter, vector<string> &elems) {
    stringstream stringstr(s);
    string item;
    while(getline(stringstr, item, delimiter)) {
        elems.push_back(item);
    }
    return elems;
}

vector<string> split(const string &s, char delim) {
    vector<string> elems;
    return split(s, delim, elems);
}

void on_publish(void *context, MQTTClient_deliveryToken dt){
	// this is not implemented because QoS 0 is used
}

int on_message(void *context, char *topicName, int topicLen, MQTTClient_message *message){
	string payload(static_cast<char*>(message->payload), message->payloadlen);
    string topic(topicName);
    vector<string> sections = split(topic, '/');
    cout << "interesting part: " << sections[4] << endl;
    if(functions.find(sections[4]) != functions.end())
    	(*functions[sections[4]])(payload);
    else
    	cout << "there is no such key" << endl;
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
	cout << "topic: " << topic << " payload: " << payload << endl;
    return 1;
}

void on_connection_lost(void *context, char *cause){
	cout << endl << "Connection lost" << "\tcause: " << cause << endl;
}

void cleanup(int sig = 0){
	loop = false;
	cout << "cleaning up" << endl;
	my_serial_stream.Close();
    MQTTClient_disconnect(client, 1000);
    MQTTClient_destroy(&client);
}

int main(int argc, char* argv[]){
	functions["Lampe 1"] = &lamp1;
	functions["Lampe 2"] = &lamp2;
	functions["Lampe 3"] = &lamp3;
	functions["Lampe 4"] = &lamp4;
	functions["master"] = &master;
	functions["relative"] = &relative;
	functions["power"] = &power;
	functions["automatic"] = &automatic;

    signal(SIGABRT, &cleanup);
	signal(SIGTERM, &cleanup);
	signal(SIGINT, &cleanup);

    MQTTClient_connectOptions connectionOptions = MQTTClient_connectOptions_initializer;
    MQTTClient_create(&client, const_cast<char*>(string(ADDRESS).c_str()), const_cast<char*>(string(DEVICE_ID).c_str()), MQTTCLIENT_PERSISTENCE_NONE, NULL);
    connectionOptions.keepAliveInterval = 60;
    connectionOptions.cleansession = 1;

    MQTTClient_setCallbacks(client, NULL, on_connection_lost, on_message, on_publish);

    int returnCode;
    if ((returnCode = MQTTClient_connect(client, &connectionOptions)) != MQTTCLIENT_SUCCESS)
    {
        cerr << "Failed to connect, return code " << returnCode << endl;
        return -1;
    }
    string payload("switch");
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/power/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/automatic/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/master/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/relative/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    payload = "range";
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 1/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 2/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 3/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 4/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    payload = "count";
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/sensors/counter/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);

    MQTTClient_subscribe(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/power/on").c_str()), 0);
    MQTTClient_subscribe(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 1/on").c_str()), 0);
   	MQTTClient_subscribe(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 2/on").c_str()), 0);
   	MQTTClient_subscribe(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 3/on").c_str()), 0);
   	MQTTClient_subscribe(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 4/on").c_str()), 0);
   	MQTTClient_subscribe(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/automatic/on").c_str()), 0);
   	MQTTClient_subscribe(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/master/on").c_str()), 0);
   	MQTTClient_subscribe(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/relative/on").c_str()), 0);

	my_serial_stream.Open("/dev/ttyUSB0");
	if(my_serial_stream.IsOpen()){
		my_serial_stream.SetBaudRate( LibSerial::SerialStreamBuf::BAUD_19200);

		uint8_t message[2], position = 0;
		char c;
		while(loop){
			my_serial_stream.get(c);
			message[position++] = c;
			if(position == 2){
				cout << "read: " << hex << static_cast<short>(message[0]) << " " << dec << static_cast<short>(message[1]) << endl;
				position = 0;
			}
			if(my_serial_stream.bad() || my_serial_stream.eof() || my_serial_stream.fail())
				my_serial_stream.clear();
		}
	}
	else{
		cout << "cannot open serial port" << endl;
		cleanup();
	}
	cout << "exiting" << endl;
    return 0;
}
