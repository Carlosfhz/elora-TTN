/*
 * This program produces real-time traffic to a private instance of the things stack.
 * Key elements are preceded by a comment with lots of dashes ( ///////////// )
 */

#include "utilities.cc"


// ns3 imports
#include <cstdlib>
#include <ctime>

#include <iostream>
#include "ns3/core-module.h"
#include "ns3/csma-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/okumura-hata-propagation-loss-model.h"
#include "ns3/correlated-shadowing-propagation-loss-model.h"// added by me 
#include "ns3/building-allocator.h"//
#include "ns3/building-penetration-loss.h"//
#include "ns3/buildings-helper.h"//
#include "ns3/propagation-delay-model.h"
#include "ns3/tap-bridge-helper.h"
#include "ns3/coutad-loss.h"

// lorawan imports
#include "ns3/chirpstack-helper.h"
#include "ns3/TTN-helper.h"

#include "ns3/hex-grid-position-allocator.h"
#include "ns3/lorawan-helper.h"
#include "ns3/periodic-sender-helper.h"
#include "ns3/range-position-allocator.h"
#include "ns3/udp-forwarder-helper.h"
#include "ns3/urban-traffic-helper.h"

// cpp imports
#include <unordered_map>
#include <vector>  // Include vector for dynamic array
#include <algorithm> // For std::shuffle
#include <random>    // For std::mt19937


using namespace ns3;
using namespace lorawan;

NS_LOG_COMPONENT_DEFINE_EXAMPLE_WITH_UTILITIES("EloraExample-TTN");

/* Global declaration of connection helper for signal handling */
ChirpstackHelper csHelper;
TTNHelper ttnHelper;
 #define NS_select 1//1 for TTN - 0 for chirpstack
int
main(int argc, char* argv[])
{
    /***************************
     *  Simulation parameters  *
     ***************************/

    std::string tenant = "elora";
    std::string apiAddr = "localhost";
    //std::string apiAddr = "localhost";
    uint16_t apiPort = 1;
    std::string token = "";
    if(NS_select == 0){
        apiPort = 8090;
        token ="eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJjaGlycHN0YWNrIiwiaXNzIjoiY2hpcnBzdGFjayIsInN1YiI6ImUzNWZkOTFmLThmMjYtNDlkYS04MzNmLTBkY2I2ZjMyN2UzOCIsInR5cCI6ImtleSJ9.jNriwUu_VL3DRkO4gAjq9OSqz1amPYrkDD9b_ZFaN7M";
    

    }else{

        apiPort = 1885;
        token = "NNSXS.FJ2RBMB5FJESLNYSBXHL2LURMXVWEE4P26YM4LA.BZPXXZFS4V6B7RZVE52LRSVCJPXHX3Y6JRGMEFD3Z3VWUNSQQOKQ";

    }
    
    
    
    uint16_t destPort = 1700;

    double periods = 1; // H * D
    int gatewayRings = 2;//this to 2 i thinks
    //double range = 2150; // Max range for downlink (!) coverage probability > 0.98 (with okumura) *0.2 LATINCOM
    //double range2 =2150; // M    double range = 2540.25; // Max range for downlink (!) coverage probability > 0.98 (with okumura)*0.5
    double range = 250;
    //double range2 = 1000;
    int appPeriodSeconds = 360;
    float percentage = 50.0;

    int nDevices = 1;
    int nGateways = 3;
    std::string sir = "CROCE";
    bool initializeSF = true;
    bool testDev = true; // this was false for other experiments
    bool file = false; // Warning: will produce a file for each gateway
    bool log = false;
    int seedStream= 250;
    std::string title = "nose4";

    /* Expose parameters to command line */
    {
        CommandLine cmd(__FILE__);
        cmd.AddValue("tenant", "Chirpstack tenant name of this simulation", tenant);
        cmd.AddValue("apiAddr", "Chirpstack REST API endpoint IP address", apiAddr);
        cmd.AddValue("apiPort", "Chirpstack REST API endpoint IP address", apiPort);
        cmd.AddValue("token", "Chirpstack API token (to be generated in Chirpstack UI)", token);
        cmd.AddValue("destPort", "Port used by the Chirpstack Gateway Bridge", destPort);
        cmd.AddValue("periods", "Number of periods to simulate (1 period = 1 hour)", periods);
        cmd.AddValue("rings", "Number of gateway rings in hexagonal topology", gatewayRings);
        cmd.AddValue("range", "Radius of the device allocation disk around a gateway)", range);
        cmd.AddValue("devices", "Number of end devices to include in the simulation", nDevices);
        cmd.AddValue("period", "period", appPeriodSeconds);
        cmd.AddValue("gateways", "Number of gateways to include in the simulation", nGateways);
        cmd.AddValue("perConfirmed", "percentage of end devices using confirm traffic", percentage);
        cmd.AddValue("seed", "percentage of end devices using confirm traffic", seedStream);

        cmd.AddValue("sir", "Signal to Interference Ratio matrix used for interference", sir);
        cmd.AddValue("initSF", "Whether to initialize the SFs", initializeSF);
        cmd.AddValue("adr", "ns3::BaseEndDeviceLorawanMac::ADRBit");
        cmd.AddValue("test", "Use test devices (5s period, 5B payload)", testDev);
        cmd.AddValue("file", "Whether to enable .pcap tracing on gateways", file);
        cmd.AddValue("title", "Whether to enable .pcap tracing on gateways", title);

        cmd.AddValue("log", "Whether to enable logs", log);
        cmd.Parse(argc, argv);
    }

    /* Apply global configurations */
    ///////////////// Real-time operation, necessary to interact with the outside world.
    GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::RealtimeSimulatorImpl"));
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));
    //Config::SetDefault("ns3::BaseEndDeviceLorawanMac::ADRBackoff", BooleanValue(true));
    Config::SetDefault("ns3::BaseEndDeviceLorawanMac::EnableCryptography", BooleanValue(true));
    ///////////////// Needed to manage the variance introduced by real world interaction
    Config::SetDefault("ns3::ClassAEndDeviceLorawanMac::RecvWinSymb", UintegerValue(16));
    //percentage = 50.0;
    /* Logging options */
    if (log)
    {
        //!> Requirement: build ns3 with debug option
        //LogComponentEnable("CoutadLoss", LOG_LEVEL_ALL);

        //LogComponentEnable("UdpForwarder", LOG_LEVEL_ALL);
        //LogComponentEnable("GatewayLoraPhy",LOG_LEVEL_DEBUG);
        //LogComponentEnable("TTNHelper",LOG_LEVEL_ALL);
        //LogComponentEnable("UrbanTrafficHelper",LOG_LEVEL_DEBUG);
        //LogComponentEnable("LorawanHelper",LOG_LEVEL_DEBUG);
        //LogComponentEnable("RecvWindowManager",LOG_LEVEL_DEBUG);
        //LogComponentEnable("ClassAEndDeviceLorawanMac", LOG_LEVEL_DEBUG);
        //LogComponentEnable("BaseEndDeviceLorawanMac", LOG_LEVEL_DEBUG);
        //LogComponentEnable ("GatewayLorawanMac", LOG_LEVEL_INFO);
        //LogComponentEnable("LoraPacketTracker",LOG_LEVEL_DEBUG);
        //LogComponentEnable("CoutadLoss",LOG_LEVEL_ALL);
        LogComponentEnable("ClassAEndDeviceLorawanMac",LOG_LEVEL_INFO);
        /* Monitor state changes of devices */
        //LogComponentEnable("EloraExample", LOG_LEVEL_ALL);
        /* Formatting */
        LogComponentEnableAll(LOG_PREFIX_FUNC);
        LogComponentEnableAll(LOG_PREFIX_NODE);
        LogComponentEnableAll(LOG_PREFIX_TIME);
    }

    /*******************
     *  Radio Channel  * 
     *******************/
/* 
    Ptr<OkumuraHataPropagationLossModel> loss;
    //Ptr<NakagamiPropagationLossModel> rayleigh;
    Ptr<CorrelatedShadowingPropagationLossModel> shadowing;
    Ptr<LoraChannel> channel;
    {
        // Delay obtained from distance and speed of light in vacuum (constant)
        Ptr<PropagationDelayModel> delay = CreateObject<ConstantSpeedPropagationDelayModel>();

        // This one is empirical and it encompasses average loss due to distance, shadowing (i.e.
        // obstacles), weather, height
        loss = CreateObject<OkumuraHataPropagationLossModel>(); //be aware I modify this in the code so check later
                                                    
        loss->SetAttribute("Frequency", DoubleValue(868100000.0));
        loss->SetAttribute("Environment", EnumValue(UrbanEnvironment));//Urban, SubUrban, OpenAreas open areas used in latincom
        loss->SetAttribute("CitySize", EnumValue(SmallCity));//For latincom was Large

        // Here we can add variance to the propagation model with multipath Rayleigh fading // I comented this so I ca add the shadowing         rayleigh = CreateObject<NakagamiPropagationLossModel>();
        rayleigh->SetAttribute("m0", DoubleValue(1.0));
        rayleigh->SetAttribute("m1", DoubleValue(1.0));
        rayleigh->SetAttribute("m2", DoubleValue(1.0)); 

        channel = CreateObject<LoraChannel>(loss, delay);
    }
 */




    Ptr<LoraChannel> channel;
    Ptr<CoutadLoss> loss;

/* 
    Ptr<CorrelatedShadowingPropagationLossModel> shadowing;
    Ptr<BuildingPenetrationLoss> buildingLoss;
    Ptr<LogDistancePropagationLossModel> loss; */


    {

        // Delay obtained from distance and speed of light in vacuum (constant)
        Ptr<PropagationDelayModel> delay = CreateObject<ConstantSpeedPropagationDelayModel>();

        // This one is empirical and it encompasses average loss due to distance, shadowing (i.e.
        // obstacles), weather, height
/*         loss =CreateObject<LogDistancePropagationLossModel>();
                                                    

        loss->SetPathLossExponent(2.75);
        loss->SetReference(1, 74.85);

        shadowing =   CreateObject<CorrelatedShadowingPropagationLossModel>();
        buildingLoss = CreateObject<BuildingPenetrationLoss>();
        loss->SetNext(shadowing);

        // Add the effect to the channel propagation loss

        shadowing->SetNext(buildingLoss); */
        loss =CreateObject<CoutadLoss>();
        loss->DoAssignStreams(seedStream);
        loss->SetNGateways(nGateways);
        channel = CreateObject<LoraChannel>(loss, delay);
        
    }
 





    /*************************
     *  Position & mobility  *
     *************************/
/* 
    MobilityHelper mobilityEd;
    MobilityHelper mobilityGw;
    Ptr<RangePositionAllocator> rangeAllocator;
    {
        // Gateway mobility
        mobilityGw.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        // In hex tiling, distance = range * cos (pi/6) * 2 to have no holes
        double gatewayDistance = range * std::cos(M_PI / 6) * 2;
        auto hexAllocator = CreateObject<HexGridPositionAllocator>();
        hexAllocator->SetAttribute("Z", DoubleValue(1.0));
        hexAllocator->SetAttribute("distance", DoubleValue(gatewayDistance));
        mobilityGw.SetPositionAllocator(hexAllocator);

        // End Device mobility
        mobilityEd.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        // We define rho to generalize the allocation disk for any number of gateway rings
        double rho = range2 + 2.0 * gatewayDistance * (gatewayRings - 1);
        rangeAllocator = CreateObject<RangePositionAllocator>();
        rangeAllocator->SetAttribute("rho", DoubleValue(rho));
        //rangeAllocator->SetAttribute("ZRV",StringValue("ns3::UniformRandomVariable[Min=1|Max=10]"));

        rangeAllocator->SetAttribute("range", DoubleValue(range2));

        rangeAllocator->SetAttribute("Z",DoubleValue(1));
        //rangeAllocator->SetAttribute("X",StringValue("ns3::UniformRandomVariable[Min=1|Max=10]"));

        //rangeAllocator->SetAttribute("Y",StringValue("ns3::UniformRandomVariable[Min=1|Max=10]"));

        mobilityEd.SetPositionAllocator(rangeAllocator);
    }



 */








MobilityHelper mobilityEd;
MobilityHelper mobilityGw;
//Ptr<RangePositionAllocator> rangeAllocator;
Ptr<RandomRectanglePositionAllocator> positionAllocator;
Ptr<ListPositionAllocator> gwallocator = CreateObject<ListPositionAllocator>();

{
    // Gateway mobility
    mobilityGw.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // Set gateway distance to 250 meters
    double gatewayDistance = 250.0;
/*     auto hexAllocator = CreateObject<HexGridPositionAllocator>();
    hexAllocator->SetAttribute("Z", DoubleValue(1.0));
    hexAllocator->SetAttribute("distance", DoubleValue(gatewayDistance));
    mobilityGw.SetPositionAllocator(hexAllocator); */



    if(nGateways==1){
        gwallocator->Add(Vector(0.0, 0.0, 1.5));
    }
    else if(nGateways==2){

        gwallocator->Add(Vector(gatewayDistance, 0.0, 1.5));
        gwallocator->Add(Vector(-1*gatewayDistance, 0.0, 1.5));

    }
    else if(nGateways==3){
        gwallocator->Add(Vector(gatewayDistance, 0.0, 1.5));
        gwallocator->Add(Vector(-1*gatewayDistance, 0.0, 1.5));
        gwallocator->Add(Vector(0.0, gatewayDistance, 1.5));

    }
    else if(nGateways==4){
        gwallocator->Add(Vector(gatewayDistance, 0.0, 1.5));
        gwallocator->Add(Vector(-1*gatewayDistance, 0.0, 1.5));
        gwallocator->Add(Vector(0.0, gatewayDistance, 1.5));
        gwallocator->Add(Vector(0.0, -1*gatewayDistance, 1.5));

    }


    // Make it so that nodes are at a certain height > 0
    mobilityGw.SetPositionAllocator(gwallocator);


    // End Device mobility
    mobilityEd.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // Set the maximum range to fit end devices in a 1000m x 1000m area
/*     double rho = 500.0;  // Half the side of the square area
    rangeAllocator = CreateObject<RangePositionAllocator>();
    rangeAllocator->SetAttribute("rho", DoubleValue(rho));
    rangeAllocator->SetAttribute("range", DoubleValue(rho));
    rangeAllocator->SetAttribute("Z", DoubleValue(1.0));  // Z-coordinate */

    //mobilityEd.SetPositionAllocator(rangeAllocator);
    positionAllocator = CreateObject<RandomRectanglePositionAllocator>();
    positionAllocator->AssignStreams(seedStream);
    positionAllocator->SetAttribute("X",StringValue("ns3::UniformRandomVariable[Min=-500|Max=500]"));
    positionAllocator->SetAttribute("Y",StringValue("ns3::UniformRandomVariable[Min=-500|Max=500]"));
    positionAllocator->SetAttribute("Z", DoubleValue(1));
    mobilityEd.SetPositionAllocator(positionAllocator);


}

    /******************
     *  Create Nodes  *
     ******************/
    //int nGateways = 3 * gatewayRings * gatewayRings - 3 * gatewayRings + 1;
    
    Ptr<Node> exitnode;
    NodeContainer gateways;
    NodeContainer endDevices;
    {
        exitnode = CreateObject<Node>();

        gateways.Create(nGateways);
        mobilityGw.Install(gateways);
        //rangeAllocator->SetNodes(gateways);

        endDevices.Create(nDevices);
        mobilityEd.Install(endDevices);
    }

    /************************
     *  Create Net Devices  *
     ************************/

    /* Csma between gateways and tap-bridge (represented by exitnode) */
    {
        NodeContainer csmaNodes(NodeContainer(exitnode), gateways);

        // Connect the bridge to the gateways with csma
        CsmaHelper csma;
        csma.SetChannelAttribute("DataRate", DataRateValue(DataRate(5000000)));
        csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));
        csma.SetDeviceAttribute("Mtu", UintegerValue(1500));
        auto csmaNetDevs = csma.Install(csmaNodes);

        // Install and initialize internet stack on gateways and bridge nodes
        InternetStackHelper internet;
        internet.Install(csmaNodes);

        Ipv4AddressHelper addresses;
        addresses.SetBase("10.1.2.0", "255.255.255.0");
        addresses.Assign(csmaNetDevs);

        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    }




  
    ///////////////// Attach a Tap-bridge to outside the simulation to the server csma device
    TapBridgeHelper tapBridge;
    tapBridge.SetAttribute("Mode", StringValue("ConfigureLocal"));
    tapBridge.SetAttribute("DeviceName", StringValue("ns3-tap"));
    tapBridge.Install(exitnode, exitnode->GetDevice(0));

    /* Radio side (between end devicees and gateways) */

    LorawanHelper helper;
    helper.EnablePacketTracking(); // Output filename
    
    NetDeviceContainer gwNetDev;
    {
        // Physiscal layer settings
        LoraPhyHelper phyHelper;
        phyHelper.SetInterference("IsolationMatrix", EnumValue(sirMap.at(sir)));
        phyHelper.SetChannel(channel);

        // Create a LoraDeviceAddressGenerator
        /////////////////// Enables full parallelism between ELoRa instances
        uint8_t nwkId = RngSeedManager::GetRun();
        auto addrGen = CreateObject<LoraDeviceAddressGenerator>(nwkId);

        // Mac layer settings
        LorawanMacHelper macHelper;
        macHelper.SetRegion(LorawanMacHelper::DefaultChannels);
        macHelper.SetAddressGenerator(addrGen);

        // Create the LoraNetDevices of the gateways
        phyHelper.SetType("ns3::GatewayLoraPhy");
        macHelper.SetType("ns3::GatewayLorawanMac");
        gwNetDev = helper.Install(phyHelper, macHelper, gateways);

        // Create the LoraNetDevices of the end devices
        phyHelper.SetType("ns3::EndDeviceLoraPhy");
        macHelper.SetType("ns3::ClassAEndDeviceLorawanMac");
        helper.Install(phyHelper, macHelper, endDevices);
    }

    /*************************
     *  Create Applications  *
     *************************/

    // Install UDP forwarders in gateways
    UdpForwarderHelper forwarderHelper = UdpForwarderHelper();
    forwarderHelper.EnablePacketTracking(); // Output filename

    forwarderHelper.SetAttribute("RemoteAddress", AddressValue(Ipv4Address("10.1.2.1")));
    forwarderHelper.SetAttribute("RemotePort", UintegerValue(destPort));
    forwarderHelper.Install(gateways);


    {

        
        // Install applications in EDs
        if (false)
        {
            //PeriodicSenderHelper appHelper;
            //appHelper.SetPeriodGenerator(CreateObjectWithAttributes<ConstantRandomVariable>("Constant", DoubleValue(150.0)));
            //appHelper.SetPeriod(Seconds(150.0));
            //appHelper.SetPacketSizeGenerator(CreateObjectWithAttributes<ConstantRandomVariable>("Constant", DoubleValue(20.0)));
            //appHelper.SetPacketSize(20);
            //appHelper.Install(endDevices);
            PeriodicSenderHelper appHelper = PeriodicSenderHelper();
            appHelper.SetPeriod(Seconds(appPeriodSeconds));
            appHelper.SetPacketSize(20);
            ApplicationContainer appContainer = appHelper.Install(endDevices);
        }
        else
        {
            UrbanTrafficHelper appHelper;
            appHelper.SetDeviceGroups(JustPoisson);
            appHelper.DoAssignStreams(seedStream);// for other sim was not here, take care 

            appHelper.Install(endDevices);
        }
    }

    /***************************
     *  Simulation and metrics *
     ***************************/

 
 
    if(NS_select == 0){
             ///////////////////// Signal handling
        OnInterrupt([](int signal) { csHelper.CloseConnection(signal); });
        ///////////////////// Register tenant, gateways, and devices on the real server
        csHelper.SetTenant(tenant);
        csHelper.InitConnection(apiAddr, apiPort, token);
        csHelper.Register(NodeContainer(endDevices, gateways));

    }else{

        OnInterrupt([](int signal) { ttnHelper.CloseConnection(signal); });

        ///////////////////// Register tenant, gateways, and devices on the real server
        //ttnHelper.SetApp(tenant);
        ttnHelper.InitConnection(apiAddr, apiPort, token);
        ttnHelper.SetNodes( nDevices,nGateways);

        ttnHelper.Register(NodeContainer(endDevices, gateways));

    } 

    

/*
    int cnt = 0;
    int count_to_print;
    // Calcular cuántos dispositivos modificar basado en el porcentaje (50%)
    count_to_print = static_cast<int>(nDevices * 50.0);
    //Config::SetDefault("ns3::BaseEndDeviceLorawanMac::ADRBit", BooleanValue(true));

    // Semilla para la generación de números aleatorios
    std::srand(seedStream);

     if (percentage > 0) {
        cnt = 0;  // Asegurarse de que el contador esté inicializado
        for (auto j = endDevices.Begin(); j != endDevices.End(); ++j) {
            // Si ya hemos modificado suficientes dispositivos, salir del bucle
            if (cnt >= count_to_print) {
                break;
            }
            auto node = *j;
            auto loraNetDevice = DynamicCast<LoraNetDevice>(node->GetDevice(0));
            auto mac = DynamicCast<BaseEndDeviceLorawanMac>(loraNetDevice->GetMac());
            // Decidir aleatoriamente si modificar este dispositivo
            if (std::rand() % nDevices < count_to_print) {


                // Establecer FType para el dispositivo seleccionado
                mac->SetFType(LorawanMacHeader::CONFIRMED_DATA_UP);
                cnt++;  // Incrementar el contador de dispositivos modificados
            }
            
             else{
                mac->SetADRBackoff(true);// I change the backof so it would take way less time to fire
            } 
        }
    } */


	int cnt = 0; // Counter for modified devices
	int count_to_modify = static_cast<int>(nDevices * percentage / 100.0);

	// Seed the random number generator with a fixed seed for reproducibility
	std::srand(seedStream); // Replace `seedStream` with your desired fixed seed

	// Use std::vector instead of VLA
	std::vector<bool> isModified(nDevices, false);

	while (cnt < count_to_modify) {
		int index = std::rand() % nDevices; // Randomly pick a device index

		// Skip already modified devices
		if (isModified[index]) continue;

		// Access the device and modify it
		auto node = *(endDevices.Begin() + index);
		auto loraNetDevice = DynamicCast<LoraNetDevice>(node->GetDevice(0));
		auto mac = DynamicCast<BaseEndDeviceLorawanMac>(loraNetDevice->GetMac());

		mac->SetFType(LorawanMacHeader::CONFIRMED_DATA_UP);
		isModified[index] = true; // Mark this device as modified
		cnt++; // Increment the counter
	}

	// Apply ADRBackoff to all unmodified devices
	size_t index = 0;
	for (auto j = endDevices.Begin(); j != endDevices.End(); ++j, ++index) {
		auto node = *j;
		auto loraNetDevice = DynamicCast<LoraNetDevice>(node->GetDevice(0));
		auto mac = DynamicCast<BaseEndDeviceLorawanMac>(loraNetDevice->GetMac());

		if (!isModified[index]) {
			mac->SetADRBackoff(true); // Set ADRBackoff for non-modified devices
		}
	}






    // Initialize SF emulating the ADR algorithm, then add variance to path loss
    std::vector<int> devPerSF(1, nDevices);
    //loss->SetNext(shadowing);

    if (initializeSF)
    {
        devPerSF = LorawanMacHelper::SetSpreadingFactorsUpAVG(endDevices, gateways, channel);
    }

    //loss->SetNext(rayleigh); // this was used in latincom comented in favor of shadowing 

    //loss->SetNext(shadowing);

    /////////////////////////////////// Trace settings
        // Connect trace sources
/*     for (NodeContainer::Iterator j = endDevices.Begin(); j != endDevices.End(); ++j)
    {
        Ptr<Node> node = *j;
        Ptr<LoraNetDevice> loraNetDevice = DynamicCast<LoraNetDevice>(node->GetDevice(0));
        Ptr<LoraPhy> phy = loraNetDevice->GetPhy();
    } */

#ifdef NS3_LOG_ENABLE
    // Print current configuration
    PrintConfigSetup(nDevices, range, gatewayRings, devPerSF);
    helper.EnableSimulationTimePrinting(Seconds(3600));
#endif // NS3_LOG_ENABLE

    Config::ConnectWithoutContext(
        "/NodeList/*/DeviceList/0/$ns3::LoraNetDevice/Phy/$ns3::EndDeviceLoraPhy/EndDeviceState",
        MakeCallback(&OnStateChange));

    if (file)
    {
        helper.EnablePcap("lora", gwNetDev);
    }




    Simulator::Stop(Hours(1) * periods);


     // Existing code to print positions of End Devices
    //std::cout << "End Devices' Positions and Periodicity:" << std::endl;
    for (NodeContainer::Iterator j = endDevices.Begin(); j != endDevices.End(); ++j)
    {
        Ptr<Node> object = *j;
        Ptr<MobilityModel> mobility = object->GetObject<MobilityModel>();
        Vector pos = mobility->GetPosition(); // Get the position

        // New addition: Try to print out the periodicity
        double period = -1; // Use -1 to indicate that the period is unknown or not set
        for (uint32_t k = 0; k < object->GetNApplications(); ++k) {
            Ptr<Application> app = object->GetApplication(k);
            Ptr<PeriodicSender> periodicSender = DynamicCast<PeriodicSender>(app);
            if (periodicSender) {
                // Assuming PeriodicSender has a method GetInterval which returns a Time object
                period = periodicSender->GetInterval().GetSeconds();
                break; // Assuming only one PeriodicSender per device, we break after finding it
            }
        }

        auto loraNetDevice = DynamicCast<LoraNetDevice>(object->GetDevice(0));
        auto mac = DynamicCast<BaseEndDeviceLorawanMac>(loraNetDevice->GetMac());
        int DataRate_out =  int(mac->GetDataRate());


        // Print both position and periodicity
        std::cout << "End Device " << object->GetId() << ": Position(" << pos.x << ", " << pos.y << ", " << pos.z << ")"
              << ", Periodicity: " << (period >= 0 ? std::to_string(period) + " seconds" : "Not set")<<", Spreading Factor "<< 12 - DataRate_out << std::endl;
    } 
    for (NodeContainer::Iterator j = gateways.Begin(); j != gateways.End(); ++j)
    {
        Ptr<Node> object = *j;
        Ptr<MobilityModel> mobility = object->GetObject<MobilityModel>();
        Vector pos = mobility->GetPosition(); // Get the position
        // Print both position and periodicity
        std::cout << "Gateways " << object->GetId() << ": Position(" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
    } 

    LoraPacketTracker& tracker = helper.GetPacketTracker();
    LoraPacketTracker& tracker_2 = forwarderHelper.GetPacketTracker();

    tracker.setNGateways(nGateways);
    std::cout << "--Start--"<< std::endl;
    Simulator::Run();
    std::cout << "--Finish--"<< std::endl;
    Time currentTime = Simulator::Now();
    DevPktCount devPktCount_DL;
    tracker.CountAllDevicesPackets_DL(Seconds(0), currentTime,devPktCount_DL);
    



     // Start simulation
    NS_LOG_INFO("Printing Statistics");
    std::cout << "Uplink Statistics"<< std::endl;
    std::cout << tracker.PrintSimulationStatistics(Seconds(0)) << std::endl;
    std::cout << "##########################################"<< std::endl;


    ///////////////////////////
    // Print results to file //
    ///////////////////////////
    NS_LOG_INFO("Computing performance metrics...");  
    NS_LOG_INFO("Printing...");
    //std::cout << tracker.CountMacPacketsGlobally(Seconds(0), Hours(1) * periods) << std::endl;
    
    NS_LOG_INFO("Gateway Infor...");
    DevPktCount devPktCount;
    tracker.CountAllDevicesPackets(Seconds(0),currentTime, devPktCount);
    std::cout << "Downlink Statistics"<< std::endl;
    tracker_2.printTraces();
    std::cout << tracker.PrintSimulationStatistics_DL(Seconds(0)) << std::endl;
    std::cout << "##########################################"<< std::endl;
    std::cout <<  tracker.CountMacPacketsGlobally(Seconds(0),currentTime)<< std::endl;
    std::cout <<  tracker.CountMacPacketsGloballyCpsr(Seconds(0),currentTime)<< std::endl;


    std::stringstream ss;
    std::stringstream ss1;
    std::stringstream ss2;
    std::stringstream ss3;


    ss <<title << "_EndDevicesOut" << ".csv";
    helper.DoPrintDeviceStatus(endDevices,gateways,ss.str());


    ss1  <<title<< "_GatewayOut" << ".csv";

    helper.DoPrintGwsPerformance(gateways, ss1.str());

    ss2<<title << "_GlobalPerf"<< ".txt";
    helper.DoPrintGlobalPerformance(ss2.str());


    ss3<<title<<"_log_uplinks"<<".csv";
    tracker.LogUplinks(Seconds(0),currentTime,gateways,endDevices,ss3.str());
    Simulator::Destroy();

    return 0;
}
