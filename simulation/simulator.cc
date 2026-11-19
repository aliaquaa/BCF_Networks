/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mobility-module.h"
#include "ns3/waypoint-mobility-model.h"
#include "ns3/flow-monitor-module.h"   // FlowMonitor (throughput/delay/jitter/loss)
#include "ns3/wifi-module.h"           // WifiHelper, WifiMacHelper, YansWifi*, Ssid

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TestSimulator");

int
main(int argc, char* argv[])
{
    int n = 50; // amount of user devices

    Time::SetResolution(Time::NS);

    NodeContainer staNode, apNode, serverNode;
    staNode.Create(n); 
    apNode.Create(1); // represents on-board access point (AP)
    serverNode.Create(1); // represents a satellite

    // log the traffic
    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

    Ptr<WaypointMobilityModel> ferry = CreateObject<WaypointMobilityModel>();
    ferry->AddWaypoint(Waypoint(Seconds(0),   Vector(0, 0, 0)));
    ferry->AddWaypoint(Waypoint(Seconds(200), Vector(1000, 0, 0)));
    ferry->AddWaypoint(Waypoint(Seconds(400), Vector(1000, 1000, 0)));

    // fixed spots on the boat, relative to the boat's center.
    // list allocator hands them out in install order: router first, then user
    Ptr<ListPositionAllocator> onFerry = CreateObject<ListPositionAllocator>();
    onFerry->Add(Vector(0, 0, 3));   // router/dish up on the cabin roof (vibes, do we even want this)
    onFerry->Add(Vector(4, 0, 1));   // user 4 m away on deck (this is also vibes)

    GroupMobilityHelper ferryMobility;
    ferryMobility.SetReferenceMobilityModel(ferry);
    ferryMobility.SetMemberPositionAllocator(onFerry);
    ferryMobility.SetMemberMobilityModel("ns3::ConstantPositionMobilityModel"); // no moving around on deck

    ferryMobility.Install(apNode);    // first allocator entry
    ferryMobility.Install(staNode);   // second entry
    // serverNode: insert satellite mobility here?


    // segment 1: wifi (user device <-> router)
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ac);
    YansWifiChannelHelper chan = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(chan.Create());
    Ssid ssid("home");
    WifiMacHelper mac;
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer staDev = wifi.Install(phy, mac, staNode);
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apWifiDev = wifi.Install(phy, mac, apNode);

    // segment 2: "satellite" (router <-> server)
    // this is a vibes-based satellite
    PointToPointHelper sat;
    sat.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    sat.SetChannelAttribute("Delay", StringValue("100ms")); //latency between router and satellite
    NetDeviceContainer satDevs = sat.Install(apNode.Get(0), serverNode.Get(0)); //since apNode and serverNode are containers of 1 node, index into the container and grab the nodes, then connect them with a channel

    // stack + two subnets, one per link
    InternetStackHelper stack;
    stack.Install(staNode); stack.Install(apNode); stack.Install(serverNode);

    Ipv4AddressHelper addr;
    addr.SetBase("10.1.1.0", "255.255.255.0"); // subnet for the user devices and router; room for 256 addresses
    Ipv4InterfaceContainer staIf = addr.Assign(staDev); // assign to user devices
    addr.Assign(apWifiDev); // assign next free address in same subnet to router (-1 address)
    addr.SetBase("10.1.2.0", "255.255.255.0"); // start a new subnet
    Ipv4InterfaceContainer srvIf = addr.Assign(satDevs); // assign to router and satellite

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();   // lets the AP forward between subnets

    // ---------- server ----------
    // echo server listens on udp port 9 and sends back whatever it receives
    UdpEchoServerHelper echoServer(9);

    // install on the "internet" node (the far side of the satellite link)
    ApplicationContainer serverApps = echoServer.Install(serverNode.Get(0));
    serverApps.Start(Seconds(1));    // up before any client sends
    serverApps.Stop(Seconds(20));    // must be >= client stop time

    // ---------- client ----------
    // target = server's ip on the sat subnet (index 1 = server end of the p2p link,
    // index 0 would be the router's side), port 9 to match the server
    UdpEchoClientHelper echoClient(srvIf.GetAddress(1), 9);
    echoClient.SetAttribute("MaxPackets", UintegerValue(10));   // total packets to send
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1))); // gap between packets
    echoClient.SetAttribute("PacketSize", UintegerValue(1024)); // payload bytes per packet

    // install on the wifi station
    ApplicationContainer clientApps = echoClient.Install(staNode);
    clientApps.Start(Seconds(3));    // wait for wifi association, any earlier packets get dropped
    clientApps.Stop(Seconds(40));

    Simulator::Stop(Seconds(41));
    Simulator::Run();
    Simulator::Destroy();


    return 0;
}
