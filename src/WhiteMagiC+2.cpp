#include <iostream>
#include <sstream>
#include <string>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <vector>
#include <map>
#include <pthread.h>
#include <MQTTClient.h>
#include <SerialStream.h>
#include <list>

// MQTT settings
#define ADDRESS     "tcp://localhost:1883"
#define DEVICE_ID   "WhiteMagic1"
#define QOS         0

// function bitmask constants
#define SET_COUNTER 0x80 // bit 8
#define SET_POWER 0x40 // bit 7
#define SET_PWM 0x20 // bit 6
#define SET_AUTO 0x10 // bit 5

using namespace std;

class FunktorBase;
class SerialMessage;

MQTTClient client;
LibSerial::SerialStream my_serial_stream;
map<string, FunktorBase*> functions;
time_t lastSetPWM; // avoid republishing if new pwm mqtt message was faster than the serial answer of the atmega
bool loop = true;
pthread_mutex_t serialMutex;

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

class SerialMessage{
	friend ostream& operator<<(ostream&, const SerialMessage&);
public:
	SerialMessage(uint8_t opCode, uint8_t value): message{opCode, value, 0}{
	}
private:
	uint8_t message[3];
};

ostream& operator<<(ostream& os, const SerialMessage& message){
#ifdef DEBUG
	cerr << "send: " << hex << (int)message.message[0] << hex << (int)message.message[1] << dec << endl;
#endif
	return os.write(reinterpret_cast<const char *>(message.message), 3);
}

class Lamp: public FunktorBase{
public:
	explicit Lamp(uint8_t lampMask): lamp(lampMask){
		status = &WhiteMagic;
	}
	void operator()(const string& payload){
		time(&lastSetPWM);
		stringstream valueStream(payload);
		short value;
		valueStream >> value;
		if(value == status->lamps[lamp] || value < 0 || value > 255)
			return;
		if(status->master)
			handleMaster(value);
		if(status->relative)
			handleRelative(value);
		pthread_mutex_lock(&serialMutex);
		my_serial_stream << SerialMessage(SET_PWM | lamp, value);
		pthread_mutex_unlock(&serialMutex);
		string answer = AnythingToStr(static_cast<short>(value));
		MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe ").append(AnythingToStr(static_cast<short>(lamp + 1))).c_str()), answer.length(), static_cast<void*>(const_cast<char*>(answer.c_str())), QOS, 1, NULL);
		status->lamps[lamp] = value;
	}
protected:
	void handleMaster(uint8_t value){
		string answer = AnythingToStr(static_cast<short>(value));
		for(short i = 0; i < 4; ++i){
			if(i == lamp)
				continue;
			pthread_mutex_lock(&serialMutex);
			my_serial_stream << SerialMessage(SET_PWM | i, value);
			pthread_mutex_unlock(&serialMutex);
			status->lamps[i] = value;
			MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe ").append(AnythingToStr(i + 1)).c_str()), answer.length(), static_cast<void*>(const_cast<char*>(answer.c_str())), QOS, 1, NULL);
		}
	}
	void handleRelative(uint8_t value){
		short relativeValue;
		string answer;
		short offset = value - status->lamps[lamp];
		for(short i = 0; i < 4; ++i){
			if(i == lamp)
				continue;
			relativeValue = status->lamps[i] + offset;
			relativeValue = relativeValue > 255 ? 255 : relativeValue;
			relativeValue = relativeValue < 0 ? 0 : relativeValue;
			pthread_mutex_lock(&serialMutex);
			my_serial_stream << SerialMessage(SET_PWM | i, relativeValue);
			pthread_mutex_unlock(&serialMutex);
			status->lamps[i] = relativeValue;
			answer = AnythingToStr(static_cast<short>(relativeValue));
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
		my_serial_stream << SerialMessage(SET_POWER, value);
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
		my_serial_stream << SerialMessage(SET_AUTO, value);
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
    if(functions.find(sections[4]) != functions.end())
    	(*functions[sections[4]])(payload);
    else
    	cerr << "there is no such key" << endl;
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void handleSerialMessage(uint8_t message[2]){
	uint8_t functionID = message[0] & 0xF0; // first 4 bits of first byte determine the function
	string payload;
	switch(functionID){
	case SET_COUNTER:
		if(WhiteMagic.counter != message[1]){
			WhiteMagic.counter = message[1];
			payload = AnythingToStr(static_cast<short>(message[1]));
			MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/persons").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
		}
		break;
	case SET_POWER:
		if(WhiteMagic.power != message[1]){
			WhiteMagic.power = message[1];
			payload = AnythingToStr(static_cast<short>(message[1]));
			MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/power").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
		}
		break;
	case SET_PWM:
		short lamp = message[0] & 0x0F;
		time_t now;
		time(&now);
		if(WhiteMagic.lamps[lamp] != message[1] &&  difftime(lastSetPWM, now) > .5){
			payload = AnythingToStr(static_cast<short>(message[1]));
			MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe ").append(AnythingToStr(lamp + 1)).c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
		}
		break;
	}
}

void on_connection_lost(void *context, char *cause){
	cerr << endl << "Connection lost" << "\tcause: " << cause << endl;
}

void cleanup(int sig = 0){
	loop = false;
	cout << "cleaning up" << endl;
	if(my_serial_stream.IsOpen())
		my_serial_stream.Close();
	if(MQTTClient_isConnected(client))
		MQTTClient_disconnect(client, 1000);
	if(client)
		MQTTClient_destroy(&client);
	pthread_mutex_destroy(&serialMutex);
}

int main(int argc, char* argv[]){
	time(&lastSetPWM);
	pthread_mutex_init(&serialMutex, NULL);

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
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/power/meta/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/automatic/meta/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/master/meta/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/relative/meta/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    payload = "range";
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 1/meta/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 2/meta/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 3/meta/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/Lampe 4/meta/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);
    payload = "text";
    MQTTClient_publish(client, const_cast<char*>(string("/devices/").append(DEVICE_ID).append("/controls/persons/meta/type").c_str()), payload.length(), static_cast<void*>(const_cast<char*>(payload.c_str())), QOS, 1, NULL);

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
		my_serial_stream.SetBaudRate(LibSerial::SerialStreamBuf::BAUD_4800);
		my_serial_stream.SetCharSize(LibSerial::SerialStreamBuf::CHAR_SIZE_8);
		my_serial_stream.SetNumOfStopBits(1);
		uint8_t message[2];
		short position = 0;
		char c;
		while(loop){
			my_serial_stream.get(c);
			if(position || c){
				message[position++] = c;
				if(position == 2){
					handleSerialMessage(message);
					position = 0;
#ifdef DEBUG
					cerr << "recv: " << hex << (int)message[0] << hex << (int)message[1] << dec << endl;
#endif
				}
				if(my_serial_stream.bad() || my_serial_stream.eof() || my_serial_stream.fail())
					my_serial_stream.clear();
			}
		}
	}
	else{
		cerr << "cannot open serial port" << endl;
		cleanup();
	}
	cout << "exiting" << endl;
    return 0;
}
