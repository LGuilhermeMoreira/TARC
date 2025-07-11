/*
 * The topology used to simulate this attack contains 5 nodes as follows:
 * n0 -> alice (sender)
 * n1 -> eve (attacker)
 * n2 -> router (common router between alice and eve)
 * n3 -> router (router conneced to bob)
 * n4 -> bob (receiver)
     n1
        \ pp1 (100 Mbps, 2ms RTT)
         \
          \             -> pp1 (100 Mbps, 2ms RTT)
           \            |
            n2 ---- n3 ---- n4
           /    |
          /     -> pp2 (1.5 Mbps, 40ms RTT)
         /
        / pp1 (100 Mbps, 2ms RTT)
     n0

 * O código foi modificado para gerar um arquivo CSV ('validation_data.csv')
 * com features por pacote, pronto para validação de modelos de machine learning.
 * A geração de PCAP e do antigo log de throughput foi desativada.
*/

#include "ns3/nstime.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include <cstring>
#include <cstdlib>
#include <fstream>

#define TCP_SINK_PORT 9000
#define UDP_SINK_PORT 9001

// Experimentation parameters
#define BULK_SEND_MAX_BYTES 2097152
#define MAX_SIMULATION_TIME 100.0
#define ATTACKER_START 0.0
#define ATTACKER_RATE (std::string)"12000kb/s"
#define ON_TIME (std::string)"0.25"
#define BURST_PERIOD 1
#define OFF_TIME std::to_string(BURST_PERIOD - stof(ON_TIME))
#define SENDER_START 0.75 // Must be equal to OFF_TIME
#define TH_INTERVAL	0.1	// The time interval in seconds for measurement throughput

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpLowRateAttackCsv");

uint32_t oldTotalBytes=0;
uint32_t newTotalBytes;
std::ofstream csvFile;

void TraceThroughput (Ptr<Application> app, Ptr<OutputStreamWrapper> stream)
{
    Ptr <PacketSink> pktSink = DynamicCast <PacketSink> (app);
    newTotalBytes = pktSink->GetTotalRx ();
	// messure throughput in Kbps
	//fprintf(stdout,"%10.4f %f\n",Simulator::Now ().GetSeconds (), (newTotalBytes - oldTotalBytes)*8/0.1/1024);
  	*stream->GetStream() << Simulator::Now ().GetSeconds () << " \t " << (newTotalBytes - oldTotalBytes)*8.0/0.1/1024 << std::endl;
	oldTotalBytes = newTotalBytes;
	Simulator::Schedule (Seconds (TH_INTERVAL), &TraceThroughput, app, stream);
}

// Função que processa cada pacote recebido no nó de destino
void PacketTrace(std::string context, const Ipv4Header &ipHeader, Ptr<const Packet> packet, uint32_t interface)
{
    uint8_t protocol = ipHeader.GetProtocol();
    uint16_t srcPort = 0;
    uint16_t dstPort = 0;
    std::string transportLayerStr;
    int isAttack = 0; // 0 para legítimo (TCP), 1 para ataque (UDP)

    if (protocol == 6) { // TCP
        TcpHeader tcpHeader;
        if (packet->PeekHeader(tcpHeader)) {
            srcPort = tcpHeader.GetSourcePort();
            dstPort = tcpHeader.GetDestinationPort();
            transportLayerStr = "TCP";
            isAttack = 0;
        }
    } else if (protocol == 17) { // UDP
        UdpHeader udpHeader;
        if (packet->PeekHeader(udpHeader)) {
            srcPort = udpHeader.GetSourcePort();
            dstPort = udpHeader.GetDestinationPort();
            transportLayerStr = "UDP";
            isAttack = 1;
        }
    } else {
        return; // Ignora pacotes que não são TCP ou UDP
    }

    // Escreve as features no arquivo CSV no formato esperado pelo modelo
    csvFile << srcPort << ","
            << dstPort << ","
            << packet->GetSize() << ","      // Packet Length
            << "1" << ","                   // Packets/Time (placeholder, já que é por pacote)
            << transportLayerStr << ","       // Highest Layer
            << transportLayerStr << ","       // Transport Layer
            << isAttack << std::endl;       // target
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);
    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

    csvFile.open("validation_data.csv");
    csvFile << "Source Port,Dest Port,Packet Length,Packets/Time,Highest Layer,Transport Layer,target" << std::endl;

    NodeContainer nodes;
    nodes.Create(5);


    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));

    PointToPointHelper pp1, pp2;
    pp1.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    pp1.SetChannelAttribute("Delay", StringValue("1ms"));

    pp2.SetQueue ("ns3::DropTailQueue","MaxSize", StringValue ("50p"));
    pp2.SetDeviceAttribute("DataRate", StringValue("1.5Mbps"));
    pp2.SetChannelAttribute("Delay", StringValue("20ms"));


    NetDeviceContainer d02, d12, d23, d34;
    d02 = pp1.Install(nodes.Get(0), nodes.Get(2));
    d12 = pp1.Install(nodes.Get(1), nodes.Get(2));
    d23 = pp2.Install(nodes.Get(2), nodes.Get(3));
    d34 = pp1.Install(nodes.Get(3), nodes.Get(4));


    InternetStackHelper stack;
    stack.Install(nodes);


    Ipv4AddressHelper a02, a12, a23, a34;
    a02.SetBase("10.1.1.0", "255.255.255.0");
    a12.SetBase("10.1.2.0", "255.255.255.0");
    a23.SetBase("10.1.3.0", "255.255.255.0");
    a34.SetBase("10.1.4.0", "255.255.255.0");


    Ipv4InterfaceContainer i02, i12, i23, i34;
    i02 = a02.Assign(d02);
    i12 = a12.Assign(d12);
    i23 = a23.Assign(d23);
    i34 = a34.Assign(d34);


    OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(i34.GetAddress(1), UDP_SINK_PORT)));
    onoff.SetConstantRate(DataRate(ATTACKER_RATE));
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=" + ON_TIME + "]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=" + OFF_TIME + "]"));
    ApplicationContainer onOffApp = onoff.Install(nodes.Get(1));
    onOffApp.Start(Seconds(ATTACKER_START));
    onOffApp.Stop(Seconds(MAX_SIMULATION_TIME));


    BulkSendHelper bulkSend("ns3::TcpSocketFactory", InetSocketAddress(i34.GetAddress(1), TCP_SINK_PORT));
    bulkSend.SetAttribute("MaxBytes", UintegerValue(BULK_SEND_MAX_BYTES));
    ApplicationContainer bulkSendApp = bulkSend.Install(nodes.Get(0));
    bulkSendApp.Start(Seconds(SENDER_START));
    bulkSendApp.Stop(Seconds(MAX_SIMULATION_TIME));


    PacketSinkHelper UDPsink("ns3::UdpSocketFactory", Address(InetSocketAddress(Ipv4Address::GetAny(), UDP_SINK_PORT)));
    ApplicationContainer UDPSinkApp = UDPsink.Install(nodes.Get(4));
    UDPSinkApp.Start(Seconds(0.0));
    UDPSinkApp.Stop(Seconds(MAX_SIMULATION_TIME));


    PacketSinkHelper TCPsink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), TCP_SINK_PORT));
    ApplicationContainer TCPSinkApp = TCPsink.Install(nodes.Get(4));
    TCPSinkApp.Start(Seconds(0.0));
    TCPSinkApp.Stop(Seconds(MAX_SIMULATION_TIME));

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Conecta nossa função PacketTrace para ser chamada para cada pacote que chega na camada IP do nó receptor (n4)
    Ptr<Ipv4> ipv4_receiver = nodes.Get(4)->GetObject<Ipv4>();
    ipv4_receiver->TraceConnect("LocalDeliver", "PacketTraceContext", MakeCallback(&PacketTrace));

    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream2 = ascii.CreateFileStream("tcp.throughput");
    Simulator::Schedule (Seconds (0.1), &TraceThroughput, nodes.Get(4)->GetApplication(1), stream2);
    pp1.EnablePcapAll("PCAPs/tcplow/");


    Simulator::Stop (Seconds(MAX_SIMULATION_TIME));
    Simulator::Run();

    csvFile.close();
    
    Simulator::Destroy();
    return 0;
}