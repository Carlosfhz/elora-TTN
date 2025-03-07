/*
 * Copyright (c) 2018 University of Padova
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Davide Magrin <magrinda@dei.unipd.it>
 *
 * 23/12/2022
 * Modified by: Alessandro Aimi <alessandro.aimi@orange.com>
 *                              <alessandro.aimi@cnam.fr>
 */

#ifndef LORA_PACKET_TRACKER_H
#define LORA_PACKET_TRACKER_H

#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/node-container.h"

#include <map>
#include <string>

namespace ns3
{
namespace lorawan
{

enum PhyPacketOutcome
{
    RECEIVED,
    INTERFERED,
    NO_MORE_RECEIVERS,
    UNDER_SENSITIVITY,
    LOST_BECAUSE_TX,
    DUTY_CYCLE,
    UNSET
};

struct PacketStatus
{
    Ptr<const Packet> packet;
    uint32_t senderId;
    Time sendTime;
    uint32_t Rxwin = 0;
    uint8_t type = 0;
    std::map<int, enum PhyPacketOutcome> outcomes;
};

struct InfoComplete{
    int SF;
    int64_t ReceiveTime;
    double snr; 
    bool received;
    int code = 0;
    uint32_t receiverId;
};

struct PacketInfo
{
    Ptr<const Packet> packet;
    uint32_t senderId;
    uint16_t fcnt;
    int type;
    int64_t sendTime;
    std::map<uint32_t, InfoComplete> pktInfoCom;
};

struct MacPacketStatus
{
    Ptr<const Packet> packet;
    uint32_t senderId;
    Time sendTime;
    Time receivedTime;
    uint32_t Rxwin = 0;

    std::map<int, Time> receptionTimes;
};

struct RetransmissionStatus
{
    Time firstAttempt;
    Time finishTime;
    uint8_t reTxAttempts;
    bool successful;
};

struct DeviceAddress
    {
        char name[50];
        int roll_no;
    };

typedef std::map<Ptr<const Packet>, MacPacketStatus> MacPacketData;
typedef std::map<Ptr<const Packet>, PacketStatus> PhyPacketData;
typedef std::map<Ptr<const Packet>, PacketInfo> PacketDataUL;//added by carlos
typedef std::map<Ptr<const Packet>, RetransmissionStatus> RetransmissionData;

struct devCount_t
{
    int sent = 0;
    int Csent = 0;
    int UCsent = 0;
    int received = 0;
    int Creceived = 0;
    int UCreceived = 0;
    int Rx1 = 0;
    int Rx2 = 0;
    int Rx1_s =0;
    int Rx2_s =0;
    int Rx1_DC =0;
    int HDLossCUL=0; // confirmed ul loss due to HD
    int HDLossUUL=0; // unconfirmed ul loss due to HD
    int Rx2_DC =0;
};

using DevPktCount = std::unordered_map<uint32_t, devCount_t>;

struct phyCount_t
{
    std::vector<int> v = std::vector<int>(6, 0);
};

using GwsPhyPktCount = std::map<uint32_t, phyCount_t>;

struct phyPrint_t
{
    std::string s = "0 0 0 0 0 0";
};

using GwsPhyPktPrint = std::unordered_map<uint32_t, phyPrint_t>;

class LoraPacketTracker
{
  public:
    LoraPacketTracker();
    ~LoraPacketTracker();

    /////////////////////////
    // PHY layer callbacks //
    /////////////////////////
    // Packet transmission callback
    void TransmissionCallback(Ptr<const Packet> packet, uint32_t systemId);
    // Packet outcome traces
    void PacketReceptionCallback(Ptr<const Packet> packet, uint32_t systemId);
    void InterferenceCallback(Ptr<const Packet> packet, uint32_t systemId);
    void NoMoreReceiversCallback(Ptr<const Packet> packet, uint32_t systemId);
    void UnderSensitivityCallback(Ptr<const Packet> packet, uint32_t systemId);
    void LostBecauseTxCallback(Ptr<const Packet> packet, uint32_t systemId);

    /////////////////////////
    // MAC layer callbacks //
    /////////////////////////
    // Packet transmission at an EndDevice
    void MacTransmissionCallback(Ptr<const Packet> packet,uint32_t RX);
    void RequiredTransmissionsCallback(uint8_t reqTx,
                                       bool success,
                                       Time firstAttempt,
                                       Ptr<Packet> packet);
    // Packet reception at the Gateway
    void MacGwReceptionCallback(Ptr<const Packet> packet);
    // Added by me Duty cycle counter
    void MacGwDutyCallback(Ptr<const Packet> packet);
    ///////////////////////////////
    // Packet counting functions //
    ///////////////////////////////
    void GetDeviceAddress(Ptr<const Packet> packet, uint32_t *addr_info);//added by carlos
    void setNGateways(int ngateway);
    bool IsUplink(Ptr<const Packet> packet);

    // void CountRetransmissions (Time transient, Time simulationTime, MacPacketData
    //                            macPacketTracker, RetransmissionData reTransmissionTracker,
    //                            PhyPacketData packetTracker);

    /**
     * Count packets to evaluate the performance at PHY level of a specific
     * gateway.
     */
    std::vector<int> CountPhyPacketsPerGw(Time startTime, Time stopTime, int systemId);
    std::string PrintPhyPacketsPerGw(Time startTime, Time stopTime, int systemId);

    void CountPhyPacketsAllGws(Time startTime, Time stopTime, GwsPhyPktCount& output);
    void PrintPhyPacketsAllGws(Time startTime, Time stopTime, GwsPhyPktPrint& output);

    std::string PrintPhyPacketsGlobally(Time startTime, Time stopTime);

    /**
     * Count packets to evaluate the performance at MAC level of a specific
     * gateway.
     */
    std::string CountMacPacketsPerGw(Time startTime, Time stopTime, int systemId);
    std::string PrintMacPacketsPerGw(Time startTime, Time stopTime, int systemId);
    std::string CountDLPackets(Time startTime, Time stopTime); //added by me
    /**
     * Count the number of retransmissions that were needed to correctly deliver a
     * packet and receive the corresponding acknowledgment.
     */
    std::string CountRetransmissions(Time startTime, Time stopTime);

    /**
     * Count packets to evaluate the global performance at MAC level of the whole
     * network. In this case, a MAC layer packet is labeled as successful if it
     * was successful at at least one of the available gateways.
     *
     * This returns a string containing the number of sent packets and the number
     * of packets that were received by at least one gateway.
     */
    std::string CountMacPacketsGlobally(Time startTime, Time stopTime);

    /**
     * Count packets to evaluate the global performance at MAC level of the whole
     * network. In this case, a MAC layer packet is labeled as successful if it
     * was successful at at least one of the available gateways, and if
     * the corresponding acknowledgment was correctly delivered at the device.
     *
     * This returns a string containing the number of sent packets and the number
     * of packets that generated a successful acknowledgment.
     */
    std::string CountMacPacketsGloballyCpsr(Time startTime, Time stopTime);

    std::string PrintDevicePackets(Time startTime, Time stopTime, uint32_t devId);
    void LogUplinks(Time startTime, Time stopTime,NodeContainer gateways,NodeContainer endDevices, std::string filename);

    void CountAllDevicesPackets(Time startTime, Time stopTime, DevPktCount& out);

    void CountAllDevicesPackets_DL(Time startTime, Time stopTime, DevPktCount& out);


    std::string PrintSimulationStatistics(Time startTime = Seconds(0));
    std::string PrintSimulationStatistics_DL(Time startTime = Seconds(0), std::string filenames= "simulation_statistics.csv");
    
    void IntTrace_correct(int32_t oldValue, int32_t newValue);
    void IntTrace_Tx_loss(int32_t oldValue, int32_t newValue);
    void IntTrace_correct_Tx(int32_t oldValue, int32_t newValue);

    void printTraces()const;

    void EnableOldPacketsCleanup(Time oldPacketThreshold = Hours(12));

  private:
    void CleanupOldPackets();

    PhyPacketData m_packetTracker;

    PacketDataUL m_ULpacketTracker;
    PhyPacketData m_packetTracker_DL;//added by me
    MacPacketData m_macPacketTracker;
    MacPacketData m_macPacketTracker_DL;//Added by me 
    MacPacketData m_macPacketTracker_DL_fail;//Added by me 
    PacketDataUL m_DLpacketTracker; //this type is to have all the info for the DL but is called


    int32_t correctSchedule =0;
    int32_t TxLoss = 0;
    int32_t correctTx = 0;


    RetransmissionData m_reTransmissionTracker;
    int m_gateways;
    Time m_oldPacketThreshold;
    Time m_lastPacketCleanup;
};
} // namespace lorawan
} // namespace ns3
#endif
