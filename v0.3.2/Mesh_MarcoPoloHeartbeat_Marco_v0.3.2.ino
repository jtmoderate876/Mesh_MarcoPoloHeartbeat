#include "Particle.h"

#define MAX_MESH_NODES 10  //Maximum number of nodes expected on the mesh. Make small as possible to preserve memory space.

// This strips the path from the filename
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

//SYSTEM_THREAD(ENABLED);

enum stats { TotalHeartbeats = 0, ResponsesReceived = 1 };

bool heartbeat = false;
bool cloudPub = false;

unsigned long beatInterval = 10000;
unsigned long lastBeatMillis = 0;
unsigned long beatTimeout = 8000;
unsigned long lastPoloMillis = 0;

//Variables to record known and reporting endpoints, or nodes.
uint8_t knownNodesCount = 0;
char knownNodes[MAX_MESH_NODES][32];
unsigned long knownNodesStats[MAX_MESH_NODES][2];  //Use to calculate response percentages. Use enum stats to acceess second dimension.

uint8_t reportingNodesCount = 0;
char reportingNodes[MAX_MESH_NODES][32];
unsigned long reportingNodesMillis[MAX_MESH_NODES];

//Variables to record when cloud connection is lost.
bool cloudLost = false;
unsigned long cloudLostMillis = 0;
unsigned long cloudResetTimeout = 600000; //10 min = 600000

unsigned long beatsSent = 0;

//Device info/Version variables.
const char version[] = "Mesh_MarcoPoloHeartbeat_Marco_v0.3.2";
char gDeviceInfo[120];  //adjust size as required


void setup() {
    Serial.begin(9600);
    
    Particle.variable("version", version);
    Particle.variable("deviceInfo",gDeviceInfo);
    Particle.publish("Marco-Polo heartbeat test started.");
    
    pinMode(D7, OUTPUT);
    pinMode(D4, INPUT_PULLDOWN);
    
    snprintf(gDeviceInfo, sizeof(gDeviceInfo)
        ,"App: %s, Date: %s, Time: %s, Sysver: %s"
        ,__FILENAME__
        ,__DATE__
        ,__TIME__
        ,(const char*)System.version()  // cast required for String
    );
    
    if (digitalRead(D4) == HIGH) {
        SelectExternalMeshAntenna();
    }

    Mesh.subscribe("Polo", ProcessBeat);
    
    ResetReportingNodes();
    
    Serial.printlnf("Version: %s", version);
    Serial.println("Marco-Polo heartbeat test started.");
}


void loop() {

    //Send heartbeat collection message every beatInterval.
    if (!heartbeat && ((millis() - lastBeatMillis) >= beatInterval)) {
        Serial.printlnf("Heartbeat started: %dms response window.", beatTimeout);
        ResetReportingNodes();
        
        heartbeat = true;
        lastBeatMillis = millis();
        lastPoloMillis = lastBeatMillis;
        digitalWrite(D7, HIGH);
        if (Mesh.ready()) {
            Serial.println("Mesh Available. sending Marco...");
            Mesh.publish("Marco");
        } else {
            Serial.println("Mesh not ready. Cannot send Marco...");
            heartbeat = false;
        }
    }
    
    //Turn off LED after beat timeout.
    if(heartbeat && ((millis() - lastBeatMillis) >= beatTimeout)) {
        heartbeat = false;
        cloudPub = true;
        digitalWrite(D7, LOW);

        Serial.println("Marco response period timed out.");
        ResponseWrapUp();
    }
    
    //Publish collected heartbeat results to cloud.
    if(cloudPub) {
        if (Particle.connected()) {
            Serial.print("Publishing results to cloud: ");
            
            char msg[80];
            snprintf(msg, arraySize(msg)-1, "Nodes:%d of %d;Millis:%d", reportingNodesCount, knownNodesCount, lastPoloMillis - lastBeatMillis);
            Serial.println(msg);
            
            bool pubSuccess = Particle.publish("MarcoPoloHeartbeat", msg, PRIVATE);
            if (!pubSuccess) { StartCloudLost(); }
            
            cloudPub = false;
            
            //TODO: Report which knownNodes did not report.
        }
        else {
            StartCloudLost();
        }
    }
    
    //Check for lost cloud. Reset if down for more than cloudResetTimeout (default 10 min).
    if (cloudLost) {
        if (Particle.connected()) {
            EndCloudLost();
        } else {
            if (millis() - cloudLostMillis > cloudResetTimeout) {
                Serial.println("Cloud did not re-establish before timeout. Resetting device...");
                delay(500);
                System.reset();
            }
        }
    }
}


void ProcessBeat(const char *name, const char *data) {
    //Record the device that is responding and exit fast.
    uint8_t count = reportingNodesCount++;
    snprintf(reportingNodes[count], arraySize(reportingNodes[count])-1, data);
    reportingNodesMillis[count] = millis();
    lastPoloMillis = millis();
}

void ResetReportingNodes() {
    for (int i; i < arraySize(reportingNodes); i++) {
        snprintf(reportingNodes[i], arraySize(reportingNodes[i])-1, "");
    }
    
    reportingNodesCount = 0;
}

void StartCloudLost() {
    if (!cloudLost) {
        cloudLostMillis = millis();
        Serial.println("Cloud lost. Starting timer.");
    }
    cloudPub = false;
    cloudLost = true;
}

void EndCloudLost() {
    cloudLost = false;
    Serial.println("Cloud Restored. Disabling timer.");
}

void ResponseWrapUp() {
    Serial.println("Cataloging responses.");
    //Loop through reporting nodes and catatalog any unknown nodes.
    for (int x = 0; x < arraySize(reportingNodes); x++) {
        if (x < reportingNodesCount) {
            Serial.printlnf("Response. index: %i; device: %s; millis: %u", x, reportingNodes[x], reportingNodesMillis[x] - lastBeatMillis);
        } else {
            break;
        }
        
        for (int i = 0; i < arraySize(knownNodes); i++) {
            //If we get to a blank array slot, record this node there.
            if (strcmp(knownNodes[i],"") == 0) {
                snprintf(knownNodes[i], arraySize(knownNodes[i])-1, reportingNodes[x]);
                knownNodesCount++;
                knownNodesStats[i][ResponsesReceived]++;
                break;
            }
            
            //If we the reporting know is known already stop looking for this reporting Node.
            if (strcmp(knownNodes[i],reportingNodes[x]) == 0) {
                knownNodesStats[i][ResponsesReceived]++;
                break;
            } 
        }
    }
    
    //Increment the number of total heartbeats.
    for (int i; i < knownNodesCount; i++) {
        knownNodesStats[i][TotalHeartbeats]++;
    }
}

void SelectExternalMeshAntenna() {
    #if (PLATFORM_ID == PLATFORM_ARGON)
    	digitalWrite(ANTSW1, 1);
    	digitalWrite(ANTSW2, 0);
    #elif (PLATFORM_ID == PLATFORM_BORON)
    	digitalWrite(ANTSW1, 0);
    #else
    	digitalWrite(ANTSW1, 0);
    	digitalWrite(ANTSW2, 1);
    #endif
}
