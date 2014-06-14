// SerialGSM version 1.1
// by Meir Michanie
// meirm@riunx.com





// error codes
// http://www.developershome.com/sms/resultCodes2.asp
#include <SerialGSM.h>
#include <string.h>
#include <stdlib.h>

SerialGSM::SerialGSM(int rxpin,int txpin):
GSMSoftwareSerial(rxpin,txpin)
{
	verbose=false;
	lastStatusCode=0;
	lastErrorCode=0;
}

void SerialGSM::registerSMSCallback(int (*callback)(void)){
	onReceiveSMS = callback;
}

bool SerialGSM::FwdSMS2Serial(){
	bool success = false;
	
	if (verbose) Serial.println("AT+CMGF=1"); // set SMS mode to text
	this->println("AT+CMGF=1"); // set SMS mode to text
	success *= WaitResp("OK", 200);
	this->ReadLine();
	if (verbose) Serial.println("AT+CNMI=3,3,0,0"); // set module to send SMS data to serial out upon receipt 
	this->println("AT+CNMI=3,3,0,0"); // set module to send SMS data to serial out upon receipt 
	success *= WaitResp("OK", 200);
	this->ReadLine();
	
	return success;
}

bool SerialGSM::SendSMS(char * cellnumber,char * outmsg){
	bool success = true;
	
	if (strlen(outmsg) > 140){
		Serial.println("Error: SMS Message was longer than 140 characters.");
		return success;
	}

	// Set SMS mode to text
	if (verbose) Serial.println("AT+CMGF=1");
	this->println("AT+CMGF=1");
	success *= WaitResp("OK", 1000);
	
	// Print recipient surrounded with double quotes
	if (verbose){
		Serial.print("AT+CMGS=");	
		Serial.print(cellnumber);
	}
	
	this->print("AT+CMGS=");
	this->print(char(34));  // ASCII equivalent of "
	this->print(cellnumber);
	this->println(char(34));

	success *= WaitResp(">", 500);
	delay(500);				//Let the module think
	
	
	// Print the SMS body
	if (verbose) Serial.print(outmsg);
	this->print(outmsg);
	
	
	// End SMS with Ctrl-Z
	if (verbose) Serial.println();
	this->print(char(26));

	success *= WaitResp("+CMGS", 20000);	
	success *= WaitResp("OK", 5000);
	
	this->ReadLine();
	delay(500);
	return success;
}

bool SerialGSM::DeleteAllSMS(){
	if (verbose) Serial.println("AT+CMGD=1,4"); // delete all SMS
	this->println("AT+CMGD=1,4"); // delete all SMS
	return WaitResp("OK", 5000);
}

void SerialGSM::Reset(){
	if (verbose) Serial.println("AT+CFUN=0,1"); // Reset Modem
	this->println("AT+CFUN=0,1"); // Reset Modem
	delay(200);

	lastErrorCode = 0;
	lastStatusCode = 0;
	memset(senderNumber, 0, PHONESIZE + 1);
	memset(recipient, 0, PHONESIZE + 1);
	memset(inMessage, 0, 160);
	this->ReadLine();
}


bool SerialGSM::Call(char * cellnumber){
	bool success = true;
	
	// Disconnect any existing call/clear released call
	this->Hangup();

	this->Rcpt(cellnumber);
	if (verbose) Serial.println(recipient);

	if (verbose) Serial.print("ATD");
	this->print("ATD");
	if (verbose) Serial.print(recipient);
	this->print(recipient);

	//End Command - ASCII carriage return
	this->print(char(13));
	if (verbose) Serial.println();

	//Wait for connection
	success *= WaitResp("+SIND: 5,1", 3000);	
	//Wait for a ring
	success *= WaitResp("+SIND: 2",  10000);
	
	return success;
}

bool SerialGSM::Hangup(){
	bool success = false;

	if (verbose) Serial.print("ATH");
	this->print("ATH");

	//End Command - ASCII carriage return
	this->print(char(13));
	if (verbose) Serial.println();

	success = WaitResp("OK", 2000);
	
	//Let the module return to normal. (Prevents errors)
	delay(2000);
	
	return success;
}

int SerialGSM::ReadLine(){
	static int pos=0;
	char nc;
	while (this->available()){
		nc=this->read();
		if (nc == '\n' or (pos > MAXMSGLEN) or (((unsigned long)(millis() - lastRec) > SERIALTIMEOUT) and (pos > 0)) ){
			nc='\0';
			lastRec=millis();
			inMessage[pos]=nc;
			pos=0;
			if (verbose) Serial.println(inMessage);
			
			// Execute the callback
			if(ReceiveSMS()){
				onReceiveSMS();
				//Let the module return to normal. (Prevents CMS error 313)
				delay(500);
			}
			
			return 1;
		}
		else if (nc=='\r') {
		}
		else{
			inMessage[pos++]=nc;
			lastRec=millis();
		}
	}
	return 0;
}

// GetGSMStatus returns one of the following status codes as reported from the GSM module
// 0 SIM card removed 
// 1 SIM card inserted 
// 2 Ring melody 
// 3 AT module is partially ready 
// 4 AT module is totally ready 
// 5 ID of released calls 
// 6 Released call whose ID=<idx> 
// 7 The network service is available for an emergency call 
// 8 The network is lost 
// 9 Audio ON 
// 10 Show the status of each phonebook after init phrase 
// 11 Registered to network
int SerialGSM::GetGSMStatus(){
	char status[2];
	//Parse the Status Code
	if (strstr(inMessage, "SIND: ") != NULL){
		int pos = 6;
		if(strstr(inMessage, "+SIND:")) pos++; 

		//Read in all numbers
		int i = 0;
		while(inMessage[pos + i] != NULL and inMessage[pos + i] != ','){
			status[i] = inMessage[pos + i];
			i++;
		}

		lastStatusCode = atoi(status);
	}
	return lastStatusCode;
}

int SerialGSM::GetErrorCode(){
	char error[3];
    //Parse the Status Code
	if (strstr(inMessage, "ERROR: ") != NULL){
		int pos = 11;
		if(strstr(inMessage, "+")) pos++; 

		//Read in all numbers
		int i = 0;
		while(inMessage[pos + i] != NULL and inMessage[pos + i] != '\n'){
			error[i] = inMessage[pos + i];
			i++;
		}

		lastErrorCode = atoi(error);
	}
	return lastErrorCode;
}

int SerialGSM::ReceiveSMS(){
	static boolean insms=0;
	// Get the number of the sms sender in order to be able to reply
	if ( strstr(inMessage, "CMT: ") != NULL ){
		insms=1;
		int sf=6;
		if(strstr(inMessage, "+CMT:")) sf++; 
		for (int i=0;i < PHONESIZE;i++){
			senderNumber[i]=inMessage[sf+i];
		}
		senderNumber[PHONESIZE]='\0';
		return 0;
	}
	else{ 
		if(insms) {
			insms=0;
			return 1;
		}
	}
	return 0;
}

int SerialGSM::ReceiveCall(){
	static boolean inCall = 0;
	// Get the caller id
	if ( strstr(inMessage, "CLIP: ") != NULL ){
		inCall=1;
		int sf=6;
		if(strstr(inMessage, "+CLIP:")) sf++; 
		for (int i=0;i < PHONESIZE;i++){
			senderNumber[i]=inMessage[sf+i];
		}
		senderNumber[PHONESIZE]='\0';
		return 0;
	}
	else{ 
		if(inCall) {
			inCall=0;
			return 1;
		}
	}
	return 0;
}

//Returns true if successful, false if timed out
boolean SerialGSM::WaitResp(char * response, int timeout){	
	unsigned long waitStart = millis();
	boolean responseReceived = false;
	
	//Check for an incoming event
	boolean incoming = ReceiveCall() || ReceiveSMS();
	
	while((unsigned long)(millis() - waitStart) < timeout){
		ReadLine();
		
		if (!responseReceived){
			responseReceived = strstr(inMessage, response) != NULL;
		}
		
		//Update events
		GetErrorCode();
		GetGSMStatus();
		
		//Ensure incoming event is not truncated
		if(responseReceived && !incoming){
			if (verbose){
				Serial.print("GSM Returned: \"");
				Serial.print(response);
				Serial.println("\"");
			}
			
			memset(inMessage, 0, 160);
			
			return true;
		}
	}

	if (verbose){
		Serial.print("GSM Timeout waiting for: \"");
		Serial.print(response);
		Serial.println("\"");
	}

	return false;
}

boolean SerialGSM::Verbose(){
	return verbose;
}

void SerialGSM::Verbose(boolean var1){
	verbose=var1;
}

char * SerialGSM::Sender(){
	return senderNumber;
}


char * SerialGSM::Rcpt(){
	return recipient;
}

char * SerialGSM::Message(){
	return inMessage;
}


void SerialGSM::Sender(char * var1){
	sprintf(senderNumber,"%s",var1);
}


void SerialGSM::Rcpt(char * var1){
	sprintf(recipient,"%s",var1);
}

void SerialGSM::Boot(){
	int counter=0;
	while(counter++ < 15){
		if (verbose) Serial.print(".");
		delay(1000);
	}
	if (verbose) Serial.println();

}

