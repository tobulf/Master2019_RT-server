#include "mbed.h"
#include <string>
#include "UipEthernet.h"
#include "UdpSocket.h"
#include "IpAddress.h"
#include "DnsClient.h"
#include "TcpServer.h"
#include "TcpClient.h"
#include "base64.h"
#include "JSON.h"

using namespace std;

//#define DEBUG
#define RTT_CORRECTION
// Static IP address must be unique and compatible with your network.
#define IP "129.241.187.18"
#define GATEWAY "129.241.187.1"
#define NETMASK "255.255.255.0"
#define PORT 80     
//Enable print:

Serial pc(USBTX, USBRX, 115200);
InterruptIn button(PC_13);
DigitalOut output_pin(PC_4);
const uint8_t MAC[6] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x06};
UipEthernet net(MAC, D11, D12, D13, D10); // mosi, miso, sck, cs
TcpServer server;                         // Ethernet server
TcpClient *client;
UdpSocket socket(&net);
char receiveBuf[1024];
const int OFF = 0;
const int ON = 1;
IpAddress timeServerIp(79, 160, 13, 250); // NTP server IP: 0.no.pool.ntp.org
Timer RTC_us;
Timer RTT_timer;
Timer Callback_timer;
Timer print_timer;

bool print_time = false;
uint64_t epoch_us;
uint16_t NTP_port = 123;
uint16_t UL_port = 23073;
uint16_t DL_port = 23074;
uint16_t TX_RTT_port = 23075;
uint16_t RX_RTT_port = 23076;

uint8_t uplink_DR;
float uplink_freq; 
uint8_t uplink_payload_size;
uint8_t uplink_GW_t;
string uplink_port;
string uplink_devEUI;
string uplink_payload;

uint32_t airtime;

const int NTP_PACKET_SIZE = 48;
uint8_t NTP_packetBuffer[NTP_PACKET_SIZE];
const uint16_t DOWNLINK_PACKET_SIZE = 136;
uint8_t Downlink_packetBuffer[DOWNLINK_PACKET_SIZE];

Watchdog &watchdog = Watchdog::get_instance();

string callback_msg;

void printfunc()
{
    print_time = true;
}

void SyncEpoch()
{
    bool sync = false;
    #ifdef TESTING
    printf("sending NTP packet...\r\n");
    #endif
    while (!sync)
    {
        watchdog.kick();
        memset(NTP_packetBuffer, 0, NTP_PACKET_SIZE);
        NTP_packetBuffer[0] = 0b11100011; // LI, Version, Mode
        NTP_packetBuffer[1] = 0;          // Stratum, or type of clock
        NTP_packetBuffer[2] = 6;          // Polling Interval
        NTP_packetBuffer[3] = 0xEC;       // Peer Clock Precision
        NTP_packetBuffer[12] = 49;
        NTP_packetBuffer[13] = 0x4E;
        NTP_packetBuffer[14] = 49;
        NTP_packetBuffer[15] = 52;
        socket.beginPacket(timeServerIp, NTP_port);
        socket.write(NTP_packetBuffer, NTP_PACKET_SIZE);
        socket.endPacket();
        Timer t;
        t.start();
        bool waiting = true;
        while (waiting)
        {
            int res = socket.parsePacket();
            if (res)
            {
                socket.read(NTP_packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
                socket.close();
                waiting = false;
                sync = true;
            }
            if (t.read() > 3)
            {
                t.stop();
                waiting = false;
                #ifdef TESTING
                printf("Retrying NTP call... \r\n");
                #endif
            }
        }
    }

    uint32_t NTP_time = (uint32_t)NTP_packetBuffer[40] << 24
    | (uint32_t)NTP_packetBuffer[41] << 16
    | (uint32_t)NTP_packetBuffer[42] << 8
    | (uint8_t)NTP_packetBuffer[43];
    NTP_time = NTP_time - 2208988800;
    set_time(NTP_time);
    epoch_us = NTP_time;
    epoch_us = epoch_us * 1000000;
    RTC_us.start();
    #ifdef TESTING
    printf("Time synched, Epoch: %ld \r\n", NTP_time);
    #endif
    set_time(NTP_time);
    watchdog.kick();
}


uint16_t getRTT(){
    socket.begin(RX_RTT_port);
    printf("waiting for RTT answer \r\n");
    Timer t;
    t.start();
    while(!socket.parsePacket()){
        if(t.read_ms() > 1000){
            printf("no answer from RTT:...\r\n");
            break;
        }
    };
    RTT_timer.stop();
    RTT_timer.reset();
    return RTT_timer.read_high_resolution_us();
};



void parseJSON(Json* jsonOBJ){
    const char * payloadStart  = jsonOBJ->tokenAddress (2);
    int payloadLength = jsonOBJ->tokenLength (2);
    char freqValue [ payloadLength ];
    strncpy ( freqValue, payloadStart, payloadLength );
    freqValue[ payloadLength ] = 0;
    uplink_freq = stof(string(freqValue));

    payloadStart  = jsonOBJ->tokenAddress (4);
    payloadLength = jsonOBJ->tokenLength (4);
    char datrValue [ payloadLength ];
    strncpy ( datrValue, payloadStart, payloadLength );
    datrValue[ payloadLength ] = 0;
    uplink_DR = stoi(string(datrValue));

    payloadStart  = jsonOBJ->tokenAddress (6);
    payloadLength = jsonOBJ->tokenLength (6);
    char sizeValue [ payloadLength ];
    strncpy ( sizeValue, payloadStart, payloadLength );
    sizeValue[ payloadLength ] = 0;
    uplink_payload_size = stoi(string(sizeValue));

    payloadStart  = jsonOBJ->tokenAddress (8);
    payloadLength = jsonOBJ->tokenLength (8);
    char portValue [ payloadLength ];
    strncpy ( portValue, payloadStart, payloadLength );
    portValue[ payloadLength ] = 0;
    uplink_port = string(portValue);

    payloadStart  = jsonOBJ->tokenAddress (10);
    payloadLength = jsonOBJ->tokenLength (10);
    char EUIValue [ payloadLength ];
    strncpy ( EUIValue, payloadStart, payloadLength );
    EUIValue[ payloadLength ] = 0;
    uplink_devEUI = string(EUIValue);
    
    payloadStart  = jsonOBJ->tokenAddress (12);
    payloadLength = jsonOBJ->tokenLength (12);
    char payloadValue [ payloadLength ];
    strncpy ( payloadValue, payloadStart, payloadLength );
    payloadValue[ payloadLength ] = 0;
    uplink_payload = string(payloadValue);

    payloadStart  = jsonOBJ->tokenAddress (14);
    payloadLength = jsonOBJ->tokenLength (14);
    char processtimeValue [ payloadLength ];
    strncpy ( processtimeValue, payloadStart, payloadLength );
    processtimeValue[ payloadLength ] = 0;
    uplink_GW_t = stoi(string(processtimeValue));

};

uint32_t calculate_airtime(uint8_t payload_size, uint8_t SF, uint8_t CR){
    double T_sym = pow(2,SF)/0.125;
    double T_preamble = (8+4.25)*T_sym;
    double upper_fraq = (8*payload_size) - (4*SF) + 44;
    double lower_fraq;
    if(SF > 10){
        lower_fraq = 4*(SF-2); 
    }
    else{
        lower_fraq = 4*SF; 
    }
    double payload_length = 8+((CR+4)*ceil((upper_fraq/lower_fraq)));
    uint32_t T_tot =  (uint32_t)(T_preamble + (T_sym*payload_length));
    return T_tot;
}

string createCallbackObject(uint64_t T_sync, uint32_t T_callback){
    string callbackMsg = 
    "{\"port\":"+uplink_port
    +",\"eui\":\""+uplink_devEUI
    +"\",\"payload\": ["
    +to_string((uint8_t)((T_sync>>48) & 0xFF))+
    ","+to_string((uint8_t)((T_sync>>40) & 0xFF))+
    ","+to_string((uint8_t)((T_sync>>32) & 0xFF))+
    ","+to_string((uint8_t)((T_sync>>24) & 0xFF))+
    ","+to_string((uint8_t)((T_sync>>16) & 0xFF))+
    ","+to_string((uint8_t)((T_sync>>8) & 0xFF))+
    ","+to_string((uint8_t)(T_sync & 0xFF))+
    ","+to_string((uint8_t)((T_callback>>16) & 0xFF))+
    ","+to_string((uint8_t)((T_callback>>8) & 0xFF))+
    ","+to_string((uint8_t)(T_callback & 0xFF))+"]} ";
    return callbackMsg;
};
/**
 * @brief
 * @note
 * @param
 * @retval
 */

int main(void)
{
    button.fall(&printfunc);
    #ifdef TESTING
    printf("Booting... \r\n");
    #endif
    watchdog.start(5000);
    if (net.connect(30) != 0)
    { // 'connect' timeout in seconds (defaults to 60 sec)
        printf("Unable to connect.\r\n");
    }
    net.set_network(IP, NETMASK, GATEWAY); // include this for using static IP address
    // Sync RTC with NTP-server.
    #ifdef TESTING
    printf("Initializing ethernet...\r\n");
    #endif
    // Show the network address
    const char *ip = net.get_ip_address();
    const char *netmask = net.get_netmask();
    const char *gateway = net.get_gateway();

    SyncEpoch();
    #ifdef TESTING
    printf("IP address: %s\r\n", ip ? ip : "None");
    printf("Netmask: %s\r\n", netmask ? netmask : "None");
    printf("Gateway: %s\r\n\r\n", gateway ? gateway : "None");
    // Bind to listen on port.
    #endif
    if(!socket.begin(UL_port)){
        #ifdef TESTING
        printf("Socket not available... \r\n");
        #endif
    };
    #ifdef TESTING
    printf("Listening on UDP, port %u \r\n", UL_port);
    printf("------------------------------------------------------------------\r\n");
    #endif
    IpAddress HostIP;
    while (true)
    {
        if(socket.parsePacket()){
            Callback_timer.start();
            socket.read(Downlink_packetBuffer, DOWNLINK_PACKET_SIZE);
            HostIP = socket.remoteIP();
            uint32_t hostAdress =  (uint32_t)HostIP.rawAddress();
            #ifdef DEBUG
            printf("packet received from %d.%d.%d.%d %s \r\n",(int)hostAdress&0xFF000000>>24,
            (int)hostAdress&0xFF0000>>16,
            (int)hostAdress&0xFF00>>8,
            (int)hostAdress&0xFF,
            Downlink_packetBuffer);
            #endif
            Json object((const char*) Downlink_packetBuffer, DOWNLINK_PACKET_SIZE);
            string callbackJson;
            if(object.isValidJson()){
                parseJSON(&object);
                airtime = calculate_airtime(uplink_payload_size, uplink_DR, 1);
                #ifdef DEBUG
                printf("%d %d %s %s \r\n", 
                uplink_DR, 
                uplink_payload_size, 
                uplink_devEUI.c_str(), 
                uplink_payload.c_str());
                printf("Airtime: %ul \r\n", airtime);
                #endif
                
            }
            else{
                Callback_timer.stop();
                Callback_timer.reset();
                socket.flush();
                socket.close();
                socket.begin(UL_port);
                watchdog.kick();
                continue;
            }
            socket.flush();
            #ifdef RTT_CORRECTION
            int garbadge_length = (DOWNLINK_PACKET_SIZE+89)/2;
            uint8_t garbadge[garbadge_length];
            socket.beginPacket(HostIP, TX_RTT_port);
            socket.write((const uint8_t*)garbadge,garbadge_length);
            watchdog.kick();
            if(!socket.endPacket()){
                printf("something went wrong sending UDP RTT... \r\n");
                Callback_timer.stop();
                Callback_timer.reset();
                socket.flush();
                socket.close();
                socket.begin(UL_port);
                watchdog.kick();
                continue;
            }
            RTT_timer.start();
            socket.flush();
            socket.close();
            socket.begin(RX_RTT_port);
            while(!socket.parsePacket());
            socket.read(Downlink_packetBuffer, DOWNLINK_PACKET_SIZE);
            socket.flush();
            watchdog.kick();

            RTT_timer.stop();
            Callback_timer.stop();
            uint64_t timestamp = RTC_us.read_high_resolution_us()
            + epoch_us;
            uint32_t T_callback = Callback_timer.read_high_resolution_us() + RTT_timer.read_high_resolution_us() + ((uint32_t)uplink_GW_t*1000) + airtime;
            callbackJson = createCallbackObject(timestamp, T_callback);
            //#ifdef DEBUG
            uint32_t temp_sec = timestamp / 1000000;
            uint32_t temp_us = timestamp - ((uint64_t)temp_sec * 1000000);
            uint16_t temp_ms = temp_us/1000;
            temp_us = temp_us - (uint32_t)temp_ms*1000;
            #ifdef TESTING
            printf("Seconds since January 1, 1970 = %u ms: %u us: %u \r\n",
            (unsigned int)temp_sec, (unsigned int)temp_ms, (unsigned int)temp_us);
            printf("RTT: %d \r\n",RTT_timer.read_us());
            printf("Tot callback time: %u us\r\n", T_callback);
            #endif
            //#endif
            RTT_timer.reset();
            #endif
            watchdog.kick();
            socket.beginPacket(HostIP, DL_port);
            socket.write((const uint8_t*) callbackJson.c_str(), callbackJson.length());
            if(!socket.endPacket()){
                printf("something went wrong sending UDP Uplink... \r\n");
                Callback_timer.stop();
                Callback_timer.reset();
                socket.flush();
                socket.close();
                socket.begin(UL_port);
                watchdog.kick();
                continue;
            };
            watchdog.kick();
            socket.flush();
            socket.close();
            socket.begin(UL_port);
            Callback_timer.stop();
            #ifdef DEBUG
            printf("Callback time: %d us\r\n", Callback_timer.read_us());
            #endif
            #ifdef TESTING
            char buffer[32];
            time_t seconds = time(NULL);
            strftime(buffer, 32, "%H:%M:%S %p\n", localtime(&seconds));
            printf("Uplink received from %s \r\n", uplink_devEUI.c_str());
            printf("Time GMT0: %s \r", buffer);
            #endif
            Callback_timer.reset();
            print_timer.start();
        };
        watchdog.kick();
        if (print_time || print_timer.read() > 2){
            if (output_pin.read()) {
                output_pin.write(0);
            }
            else{
                output_pin.write(1);
            }
            uint64_t temp = RTC_us.read_high_resolution_us() + epoch_us;
            print_timer.stop();
            print_timer.reset();
            uint32_t temp_sec = temp / 1000000;
            uint32_t temp_us = temp - ((uint64_t)temp_sec * 1000000);
            printf("%u, %u \r\n",
                   (unsigned int)temp_sec, (unsigned int)temp_us);
            
            print_time = false;
        };
 
    }
}
