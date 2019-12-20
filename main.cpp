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
DigitalOut print_pin(PC_4);
DigitalOut synch_pin(PA_10);
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
const uint16_t DOWNLINK_PACKET_SIZE = 125;
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
    printf("sending NTP packet...\r\n");
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
                printf("Retrying NTP call... \r\n");
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
    printf("Time synched, Epoch: %ld \r\n", NTP_time);
    set_time(NTP_time);
    watchdog.kick();
}

uint32_t synch_epoch = 1576860400;

int main(void)
{
    button.fall(&printfunc);
    printf("Booting... \r\n");
    watchdog.start(5000);
    if (net.connect(30) != 0)
    { // 'connect' timeout in seconds (defaults to 60 sec)
        printf("Unable to connect.\r\n");
    }
    net.set_network(IP, NETMASK, GATEWAY); // include this for using static IP address
    // Sync RTC with NTP-server.
    printf("Initializing ethernet...\r\n");
    // Show the network address
    const char *ip = net.get_ip_address();
    const char *netmask = net.get_netmask();
    const char *gateway = net.get_gateway();

    SyncEpoch();

    printf("IP address: %s\r\n", ip ? ip : "None");
    printf("Netmask: %s\r\n", netmask ? netmask : "None");
    printf("Gateway: %s\r\n\r\n", gateway ? gateway : "None");
    // Bind to listen on port.
    bool synch = false;
    while (true)
    {   
        time_t now = time(NULL);
        if (now == synch_epoch && !synch){
            synch_pin.write(1);
            wait_ms(1);
            synch_pin.write(0);
            printf("Synched @ %u s\r\n", (unsigned int)now);
            synch = true;
        }
        
        watchdog.kick();
        if (print_time)
        {
            print_pin.write(1);
            uint64_t temp = RTC_us.read_high_resolution_us() + epoch_us;
            uint32_t temp_sec = temp / 1000000;
            uint32_t temp_us = temp - ((uint64_t)temp_sec * 1000000);
            printf("Seconds since January 1, 1970 = %u, us: %u \r\n",
                   (unsigned int)temp_sec, (unsigned int)temp_us);
            wait_ms(10);
            print_pin.write(0);
            print_time = false;
        };
 
    }
}
