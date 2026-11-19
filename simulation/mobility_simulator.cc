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
#include "ns3/mobility-module.h"
#include <fstream>
#include <sstream>
#include <string>

// Default Network Topology
//
//       10.1.1.0
// n0 -------------- n1
//    point-to-point
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("FirstScriptExample");


void
LoadFerryWaypoints(
    Ptr<WaypointMobilityModel> mobility,
    const std::string& filename)
{
    std::ifstream file(filename);

    if (!file.is_open())
    {
        NS_FATAL_ERROR("Could not open GPS file: " << filename);
    }

    std::string line;

    // Skip the CSV header.
    std::getline(file, line);

    while (std::getline(file, line))
    {
        std::stringstream ss(line);

        double time;
        double x;
        double y;
        double z;
        int segment;

        char comma;

        // Read CSV columns.
        if (!(ss >> time >> comma
                 >> x >> comma
                 >> y >> comma
                 >> z >> comma
                 >> segment))
        {
            continue;
        }

        // Add the GPS position as an ns-3 waypoint.
        mobility->AddWaypoint(
            Waypoint(
                Seconds(time),
                Vector(x, y, z)
            )
        );
    }

    file.close();
}


void
PrintFerryPosition(Ptr<Node> node)
{
    Ptr<MobilityModel> model =
        node->GetObject<MobilityModel>();

    Vector position = model->GetPosition();

    NS_LOG_UNCOND(
        "Time: " << Simulator::Now().GetSeconds()
        << " s | Position: "
        << position.x << ", "
        << position.y << ", "
        << position.z
    );
}


int
main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);
    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

    NodeContainer nodes;
    nodes.Create(2);

    // Install mobility on the ferry node.
    MobilityHelper mobility;
    mobility.SetMobilityModel(
        "ns3::WaypointMobilityModel");

    mobility.Install(nodes.Get(0));

    // Retrieve the installed model.
    Ptr<WaypointMobilityModel> ferryMobility =
        nodes.Get(0)->GetObject<WaypointMobilityModel>();

    // Load actual GPS coordinates.
    LoadFerryWaypoints(
        ferryMobility,
        "BCF_networks/network_testing/gps_routes/vic_to_van.csv");

        // Check the ferry position at different simulation times.
    Simulator::Schedule(
        Seconds(0),
        &PrintFerryPosition,
        nodes.Get(0));

    Simulator::Schedule(
        Seconds(60),
        &PrintFerryPosition,
        nodes.Get(0));

    Simulator::Schedule(
        Seconds(300),
        &PrintFerryPosition,
        nodes.Get(0));

    Simulator::Schedule(
        Seconds(600),
        &PrintFerryPosition,
        nodes.Get(0));

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("12Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install(nodes);

    InternetStackHelper stack;

    // Disableing IPv6 because it is not necessary to show what we want to demonstrate here.
    // Note:Normal networks typically have both IPv4 and IPv6 enabled.
    stack.SetIpv6StackInstall(false);

    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    UdpEchoServerHelper echoServer(9);

    ApplicationContainer serverApps = echoServer.Install(nodes.Get(1));
    serverApps.Start(Seconds(1));
    serverApps.Stop(Seconds(900));

    UdpEchoClientHelper echoClient(interfaces.GetAddress(1), 9);
    echoClient.SetAttribute("MaxPackets", UintegerValue(1));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApps = echoClient.Install(nodes.Get(0));
    clientApps.Start(Seconds(2));
    clientApps.Stop(Seconds(900));

    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
