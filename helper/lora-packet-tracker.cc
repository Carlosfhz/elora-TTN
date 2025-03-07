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

#include "lora-packet-tracker.h"

#include "ns3/log.h"
#include "ns3/lora-phy.h"
#include "ns3/lora-tag.h"
#include "ns3/lorawan-mac-header.h"
#include "ns3/simulator.h"
#include "ns3/lora-frame-header.h"//added by me
#include "ns3/lora-device-address.h" //added by me
#include "ns3/address.h" //added by me


//added by me
#include "ns3/lora-application.h"
#include "ns3/loratap-header.h"
#include "ns3/class-a-end-device-lorawan-mac.h"
#include "lorawan-helper.h"



#include <fstream>
#include <iostream>
#include <bitset>//added by me

namespace ns3
{
namespace lorawan
{
NS_LOG_COMPONENT_DEFINE("LoraPacketTracker");

LoraPacketTracker::LoraPacketTracker()
    : m_oldPacketThreshold(Seconds(0)),
      m_lastPacketCleanup(Seconds(0))
{
    NS_LOG_FUNCTION(this);
}

LoraPacketTracker::~LoraPacketTracker()
{
    NS_LOG_FUNCTION(this);
    m_packetTracker.clear();
    m_macPacketTracker.clear();
    m_reTransmissionTracker.clear();
}

/////////////////
// MAC metrics //
/////////////////

void
LoraPacketTracker::MacTransmissionCallback(Ptr<const Packet> packet,uint32_t RX)
{
    if (IsUplink(packet))
    {

        //NS_LOG_INFO("UPLINK new packet "<< packet <<" was sent by the MAC layer");

        MacPacketStatus status;
        status.packet = packet;
        status.sendTime = Simulator::Now();
        status.senderId = Simulator::GetContext();
        status.receivedTime = Time::Max();

        m_macPacketTracker.insert(std::pair<Ptr<const Packet>, MacPacketStatus>(packet, status));
        CleanupOldPackets();
    }else{ //added by me 

        NS_LOG_INFO("DOWNLINK new packet "<< packet <<" was sent by the MAC layer RX: "<< RX);
    
        MacPacketStatus status;
        status.packet = packet;
        status.sendTime = Simulator::Now();
        status.senderId = Simulator::GetContext();
        status.receivedTime = Time::Max();
        status.Rxwin = RX;
        m_macPacketTracker_DL.insert(std::pair<Ptr<const Packet>, MacPacketStatus>(packet, status));
        CleanupOldPackets();
    }
}

void
LoraPacketTracker::RequiredTransmissionsCallback(uint8_t reqTx,
                                                 bool success,
                                                 Time firstAttempt,
                                                 Ptr<Packet> packet)
{
    //NS_LOG_INFO("Finished retransmission attempts for a packet");
    //NS_LOG_DEBUG("UPLINK- Packet: " << packet << ", ReqTx: " << unsigned(reqTx) << ", succ: " << success << ", firstAttempt: " << firstAttempt.GetSeconds());

 
    RetransmissionStatus entry;
    entry.firstAttempt = firstAttempt;
    entry.finishTime = Simulator::Now();
    entry.reTxAttempts = reqTx;
    entry.successful = success;

    m_reTransmissionTracker.insert(std::pair<Ptr<Packet>, RetransmissionStatus>(packet, entry));
    CleanupOldPackets();
}

void
LoraPacketTracker::MacGwReceptionCallback(Ptr<const Packet> packet) //this is now both ED and GW
{
    if (IsUplink(packet))
    {
        //NS_LOG_INFO("A packet was successfully received" << " at the MAC layer of gateway " << Simulator::GetContext());
        //NS_LOG_INFO("UPLINKS - " << packet <<" packet was successfully received" << " at the MAC layer of gateway " << Simulator::GetContext());

        // Find the received packet in the m_macPacketTracker
        auto it = m_macPacketTracker.find(packet);
        if (it != m_macPacketTracker.end())
        {
            //receptionTimes allows us to get the id and received time so use that.
            (*it).second.receptionTimes.insert(std::pair<int, Time>(Simulator::GetContext(), Simulator::Now()));
            if (Simulator::Now() < (*it).second.receivedTime)
            {
                (*it).second.receivedTime = Simulator::Now();
            }
        }



    }else
        {
        auto it = m_macPacketTracker_DL.find(packet);
        if (it != m_macPacketTracker_DL.end())
        {
            (*it).second.receptionTimes.insert(std::pair<int, Time>(Simulator::GetContext(), Simulator::Now()));
            if (Simulator::Now() < (*it).second.receivedTime)
            {
                (*it).second.receivedTime = Simulator::Now();
            }
            
        }




            //NS_ABORT_MSG("Packet not found in tracker");
    }
    
}
//////////////////////////////////////////////////////////////////////
void
LoraPacketTracker::MacGwDutyCallback(Ptr<const Packet> packet)
{

        if (IsUplink(packet)){
            //NS_LOG_DEBUG("UPLINK - Packet " << packet << " Cannot transmit due to duty cycle ");
        }
        else{
            //NS_LOG_DEBUG("DOWNLINK- LOSSS - Packet " << packet << " Cannot transmit due to duty cycle ");
            uint32_t Id = Simulator::GetContext();


            PacketStatus status;
            status.packet = packet;
            status.sendTime = Simulator::Now();
            status.senderId = Id;

            m_packetTracker_DL.insert(std::pair<Ptr<const Packet>, PacketStatus>(packet, status)); // hay que inscribirlo antes despues viene la parte de mac 
            CleanupOldPackets();

            std::map<Ptr<const Packet>, PacketStatus>::iterator it = m_packetTracker_DL.find(packet);

            uint32_t addr_info[2];
            GetDeviceAddress(packet,addr_info);

            uint32_t nwkAddr = addr_info[1] + m_gateways +1;

            (*it).second.outcomes.insert(std::pair<int, enum PhyPacketOutcome>(nwkAddr, DUTY_CYCLE));


        }


}

//////////////////////////////////////////////////////////////////////

/////////////////
// PHY metrics //
/////////////////

void
LoraPacketTracker::TransmissionCallback(Ptr<const Packet> packet, uint32_t edId)
{
    LorawanMacHeader mHdr;
    //LoraFrameHeader fHdr;


    if (IsUplink(packet))
    {
        //NS_LOG_DEBUG("PHY packet " << packet << " was transmitted by device " << edId);
        // Create a packetStatus
        Ptr<Packet> copy = packet->Copy();
        copy->RemoveHeader(mHdr);
        uint8_t typeFrame = mHdr.GetFType();
        PacketStatus status;
        status.type = typeFrame;
        status.packet = packet;
        status.sendTime = Simulator::Now();
        status.senderId = edId;

        m_packetTracker.insert(std::pair<Ptr<const Packet>, PacketStatus>(packet, status));
        
        PacketInfo info_first;
        info_first.packet = packet;
        info_first.senderId = edId;
        info_first.sendTime = Simulator::Now().GetNanoSeconds();
        Ptr<Packet> packetCopy = packet->Copy();
        info_first.type = int(typeFrame);
        //int deserialized = packetCopy->RemoveHeader(fHdr);
       // NS_LOG_INFO("des " << deserialized);

        //packet->Copy()->RemoveHeader(fHdr);
        info_first.fcnt = 0;
        m_ULpacketTracker.insert(std::pair<Ptr<const Packet>, PacketInfo>(packet, info_first));
        CleanupOldPackets();
    }else{//this may not work because it has only for end devices not all devices transmiting
        //NS_LOG_DEBUG("DOWNLINK PHY packet " << packet << " was transmitted by device " << edId);
        // Create a packetStatus




        PacketStatus status;
        status.packet = packet;
        status.sendTime = Simulator::Now();
        status.senderId = edId;

        m_packetTracker_DL.insert(std::pair<Ptr<const Packet>, PacketStatus>(packet, status));

        

        Ptr<Packet> copy = packet->Copy();
        copy->RemoveHeader(mHdr);
        uint8_t typeFrame = mHdr.GetFType();
        PacketInfo info_first;
        info_first.packet = packet;
        info_first.senderId = edId;
        info_first.sendTime = Simulator::Now().GetNanoSeconds();
        Ptr<Packet> packetCopy = packet->Copy();
        info_first.type = int(typeFrame);
        
        //int deserialized = packetCopy->RemoveHeader(fHdr);
       // NS_LOG_INFO("des " << deserialized);

        //packet->Copy()->RemoveHeader(fHdr);
        info_first.fcnt = 0;
        m_DLpacketTracker.insert(std::pair<Ptr<const Packet>, PacketInfo>(packet, info_first));
        CleanupOldPackets();


    }
}

void
LoraPacketTracker::PacketReceptionCallback(Ptr<const Packet> packet, uint32_t gwId)
{
    LoraTag tag;
    if (IsUplink(packet))
    {
        // Remove the successfully received packet from the list of sent ones
        //NS_LOG_INFO("UPLINK PHY packet " << packet << " was successfully received at gateway " << gwId);

        std::map<Ptr<const Packet>, PacketStatus>::iterator it = m_packetTracker.find(packet);
        (*it).second.outcomes.insert(std::pair<int, enum PhyPacketOutcome>(gwId, RECEIVED));

        /// added by carlos
        InfoComplete Info;
        Info.receiverId = gwId;
        Ptr<Packet> copy = packet->Copy();
        packet->Copy()->RemovePacketTag(tag); // Needed in case of retx!
        Info.SF = 12 - int(tag.GetDataRate()) ;
        Info.ReceiveTime =  tag.GetReceptionTime().GetNanoSeconds();
        Info.received = true;
        Info.snr  = tag.GetSnr();
        //std::advance(it3, index);
 
        
        std::map<Ptr<const Packet>, PacketInfo>::iterator it2 = m_ULpacketTracker.find(packet);

        (*it2).second.pktInfoCom.insert(std::pair<uint32_t, struct InfoComplete>(gwId, Info));


        ///




        
    }else{//in fact this is checking the MAC I dont remember why I sent to this callback, maybe because the other was called GW, but I should have created a MAC for ED
        //NS_LOG_INFO("DOWNLINK PHY packet " << packet << " was successfully received at end device " << gwId);

        std::map<Ptr<const Packet>, PacketStatus>::iterator it = m_packetTracker_DL.find(packet);
        (*it).second.outcomes.insert(std::pair<int, enum PhyPacketOutcome>(gwId, RECEIVED));


        InfoComplete Info;
        Info.receiverId = gwId;
        Ptr<Packet> copy = packet->Copy();
        packet->Copy()->RemovePacketTag(tag); // Needed in case of retx!
        Info.SF = 12 - int(tag.GetDataRate()) ;
        Info.ReceiveTime =  tag.GetReceptionTime().GetNanoSeconds();
        Info.received = true;

        Info.snr  = tag.GetSnr();
        //std::advance(it3, index);
 
        std::map<Ptr<const Packet>, PacketInfo>::iterator it2 = m_DLpacketTracker.find(packet);

        (*it2).second.pktInfoCom.insert(std::pair<uint32_t, struct InfoComplete>(gwId, Info));

    }
}

void
LoraPacketTracker::InterferenceCallback(Ptr<const Packet> packet, uint32_t Id)
{
    LoraTag tag;

    if (IsUplink(packet))
    {
        //NS_LOG_INFO("PHY packet " << packet << " was interfered at gateway " << gwId);

        std::map<Ptr<const Packet>, PacketStatus>::iterator it = m_packetTracker.find(packet);
        (*it).second.outcomes.insert(std::pair<int, enum PhyPacketOutcome>(Id, INTERFERED));
        /// added by carlos
/*         
        InfoComplete Info;
        Info.receiverId = Id;
        Ptr<Packet> copy = packet->Copy();
        packet->Copy()->RemovePacketTag(tag); // Needed in case of retx!
        Info.SF = 12 - int(tag.GetDataRate()) ;
        Info.ReceiveTime =  tag.GetReceptionTime().GetNanoSeconds();
        Info.received = false;
        Info.snr  = tag.GetSnr();
        Info.code = 3;
        //std::advance(it3, index);

        std::map<Ptr<const Packet>, PacketInfo>::iterator it2 = m_ULpacketTracker.find(packet);

        (*it2).second.pktInfoCom.insert(std::pair<uint32_t, struct InfoComplete>(Id, Info)); */
        //


 
    }else{
        uint32_t addr_info[2];
        GetDeviceAddress(packet,addr_info);

        uint32_t nwkAddr = addr_info[1] + m_gateways +1;
        //NS_LOG_INFO("DOWNLINK LOSS - "<< " PHY packet "<< packet << "Interfered at End Device "<< Id<< " EDAddress: " << nwkAddr);

        if(nwkAddr == Id){
            std::map<Ptr<const Packet>, PacketStatus>::iterator it = m_packetTracker_DL.find(packet);
            (*it).second.outcomes.insert(
            std::pair<int, enum PhyPacketOutcome>(Id, INTERFERED));


            InfoComplete Info;



            Info.receiverId = Id;
            Ptr<Packet> copy = packet->Copy();
            packet->Copy()->RemovePacketTag(tag); // Needed in case of retx!
            Info.SF = 12 - int(tag.GetDataRate()) ;

            LoraPhyTxParameters params;
            params.sf = tag.GetTxParameters().sf;
            params.lowDataRateOptimizationEnabled = LoraPhy::GetTSym(params) > MilliSeconds(16);
            int64_t duration =  LoraPhy::GetTimeOnAir(packet->Copy(), params).GetNanoSeconds();



            //Info.ReceiveTime =  tag.GetReceptionTime().GetNanoSeconds() +duration;
            Info.received = false;

            Info.snr  = tag.GetSnr();
            //std::advance(it3, index);
    
            std::map<Ptr<const Packet>, PacketInfo>::iterator it2 = m_DLpacketTracker.find(packet);
            Info.ReceiveTime =  (*it2).second.sendTime +duration;


            (*it2).second.pktInfoCom.insert(std::pair<uint32_t, struct InfoComplete>(Id, Info));

        
        }
    }
}

void
LoraPacketTracker::NoMoreReceiversCallback(Ptr<const Packet> packet, uint32_t Id)
{        
    LoraTag tag;

    if (IsUplink(packet))
    {
        //NS_LOG_INFO("PHY packet " << packet << " was lost because no more receivers at gateway "<< gwId);
        std::map<Ptr<const Packet>, PacketStatus>::iterator it = m_packetTracker.find(packet);
        (*it).second.outcomes.insert(
            std::pair<int, enum PhyPacketOutcome>(Id, NO_MORE_RECEIVERS));


                /// added by carlos
/*         InfoComplete Info;
        Info.receiverId = Id;
        Ptr<Packet> copy = packet->Copy();
        packet->Copy()->RemovePacketTag(tag); // Needed in case of retx!
        Info.SF = 12 - int(tag.GetDataRate()) ;
        Info.ReceiveTime =  tag.GetReceptionTime().GetNanoSeconds();
        Info.received = false;
        Info.snr  = tag.GetSnr();
        Info.code = 4;


        //std::advance(it3, index);

        std::map<Ptr<const Packet>, PacketInfo>::iterator it2 = m_ULpacketTracker.find(packet);

        (*it2).second.pktInfoCom.insert(std::pair<uint32_t, struct InfoComplete>(Id, Info)); */
        //
    }else{
        uint32_t addr_info[2];
        GetDeviceAddress(packet,addr_info);

        uint32_t nwkAddr = addr_info[1] + m_gateways +1;
        //NS_LOG_INFO("DOWNLINK LOSS - "<< " PHY packet "<< packet << " NO_MORE_RECEIVERS at End Device "<< Id<< " EDAddress: " << nwkAddr);

        if(nwkAddr == Id){

            

            std::map<Ptr<const Packet>, PacketStatus>::iterator it = m_packetTracker_DL.find(packet);
            (*it).second.outcomes.insert(
            std::pair<int, enum PhyPacketOutcome>(Id, NO_MORE_RECEIVERS));

        
        }
    }
}

void
LoraPacketTracker::UnderSensitivityCallback(Ptr<const Packet> packet, uint32_t Id)
{

    LoraTag tag;
    if (IsUplink(packet))
    {
        //NS_LOG_INFO("PHY packet " << packet << " was lost because under sensitivity at gateway "<< gwId);
        std::map<Ptr<const Packet>, PacketStatus>::iterator it = m_packetTracker.find(packet);

        (*it).second.outcomes.insert(
            std::pair<int, enum PhyPacketOutcome>(Id, UNDER_SENSITIVITY));

        /// added by carlos
        InfoComplete Info;
/*         Info.receiverId = Id;
        Ptr<Packet> copy = packet->Copy();
        packet->Copy()->RemovePacketTag(tag); // Needed in case of retx!
        Info.SF = 12 - int(tag.GetDataRate()) ;
        Info.ReceiveTime =  tag.GetReceptionTime().GetNanoSeconds();
        Info.received = false;
        Info.snr  = tag.GetSnr();
        Info.code = 2;


        //std::advance(it3, index);

        std::map<Ptr<const Packet>, PacketInfo>::iterator it2 = m_ULpacketTracker.find(packet);

        (*it2).second.pktInfoCom.insert(std::pair<uint32_t, struct InfoComplete>(Id, Info)); */
        //




    }else{

        uint32_t addr_info[2];
        GetDeviceAddress(packet,addr_info);
        
        uint32_t nwkAddr = addr_info[1] + m_gateways +1;
        if(nwkAddr == Id){
            std::map<Ptr<const Packet>, PacketStatus>::iterator it = m_packetTracker_DL.find(packet);
            //NS_LOG_INFO("DOWNLINK LOSS - "<< " PHY packet "<< packet << " under sensitivity at End Device "<< Id<< " EDAddress: " << nwkAddr);
            (*it).second.outcomes.insert(
            std::pair<int, enum PhyPacketOutcome>(Id, UNDER_SENSITIVITY));

            
            InfoComplete Info;
            Info.receiverId = Id;





            Ptr<Packet> copy = packet->Copy();
            packet->Copy()->RemovePacketTag(tag); // Needed in case of retx!
            Info.SF = 12 - int(tag.GetDataRate()) ;
            //Info.ReceiveTime =  tag.GetReceptionTime().GetNanoSeconds()+duration;
            Info.received = false;
            LoraPhyTxParameters params;
            params.sf = tag.GetTxParameters().sf;
            params.lowDataRateOptimizationEnabled = LoraPhy::GetTSym(params) > MilliSeconds(16);
            int64_t duration =  LoraPhy::GetTimeOnAir(packet->Copy(), params).GetNanoSeconds();


            Info.snr  = tag.GetSnr();
            //std::advance(it3, index);
    
            std::map<Ptr<const Packet>, PacketInfo>::iterator it2 = m_DLpacketTracker.find(packet);
            Info.ReceiveTime =  (*it2).second.sendTime +duration;

            (*it2).second.pktInfoCom.insert(std::pair<uint32_t, struct InfoComplete>(Id, Info));
        }
    }
}

void
LoraPacketTracker::LostBecauseTxCallback(Ptr<const Packet> packet, uint32_t gwId)
{
    if (IsUplink(packet))
    {
        //NS_LOG_INFO("PHY packet " << packet << " was lost because of GW transmission at gateway "<< gwId);
        LoraTag tag;
        std::map<Ptr<const Packet>, PacketStatus>::iterator it = m_packetTracker.find(packet);
        (*it).second.outcomes.insert(std::pair<int, enum PhyPacketOutcome>(gwId, LOST_BECAUSE_TX));
        /// added by carlos
        InfoComplete Info;
        Info.receiverId = gwId;
        Ptr<Packet> copy = packet->Copy();
        packet->Copy()->RemovePacketTag(tag); // Needed in case of retx!
        Info.SF = 12 - int(tag.GetDataRate()) ;
        Info.ReceiveTime =  tag.GetReceptionTime().GetNanoSeconds();
        Info.received = false;
        Info.snr  = tag.GetSnr();
        Info.code = 1;


        //std::advance(it3, index);

        std::map<Ptr<const Packet>, PacketInfo>::iterator it2 = m_ULpacketTracker.find(packet);

        (*it2).second.pktInfoCom.insert(std::pair<uint32_t, struct InfoComplete>(gwId, Info));





    }
}

/////////////////////////////////////////////




//////////////////////////////////////////////

void LoraPacketTracker::GetDeviceAddress(Ptr<const Packet> packet, uint32_t *addr_info){// Added by carlos
        // Work on a copy of the packet
        Ptr<Packet> packetCopy = packet->Copy();
        // Remove MIC (currently we do not check it)
        packetCopy->RemoveAtEnd(4);
        LorawanMacHeader mHdr;
        packetCopy->RemoveHeader(mHdr);
        //NS_LOG_DEBUG("Mac Header: " << mHdr);
        // Remove the Frame Header
        LoraFrameHeader fHdr;
        //int deserialized = packetCopy->RemoveHeader(fHdr);
        fHdr.SetAsDownlink();

        packetCopy->RemoveHeader(fHdr);
        LoraDeviceAddress address; 
        address = fHdr.GetAddress();
        NwkAddr m_nwkAddr = address.GetNwkAddr();     //!< The network Id of this address
        NwkID m_nwkId;     //!< The network Id of this address

        uint32_t nwkAddr = uint32_t(m_nwkAddr.Get());
        uint32_t nwkID = uint32_t(m_nwkId.Get());

        addr_info[0]=nwkID;
        addr_info[1]=nwkAddr;
        //NS_LOG_INFO("Number of gateways: " << m_gateways);  
}

void
LoraPacketTracker::setNGateways(int ngateway){
    m_gateways = ngateway;
    //NS_LOG_INFO("Number of gateways: " << m_gateways);  

}


bool
LoraPacketTracker::IsUplink(Ptr<const Packet> packet)
{
    //NS_LOG_FUNCTION(this);

    LorawanMacHeader mHdr;
    Ptr<Packet> copy = packet->Copy();
    copy->RemoveHeader(mHdr);
    return mHdr.IsUplink();
}

////////////////////////
// Counting Functions //
////////////////////////

std::vector<int>
LoraPacketTracker::CountPhyPacketsPerGw(Time startTime, Time stopTime, int gwId)
{
    // Vector packetCounts will contain - for the interval given in the input of
    // the function, the following fields: totPacketsSent receivedPackets
    // interferedPackets noMoreGwPackets underSensitivityPackets lostBecauseTxPackets
    std::vector<int> packetCounts(6, 0); //I changed from this


    for (auto itPhy = m_packetTracker.begin(); itPhy != m_packetTracker.end(); ++itPhy)
    {
        if ((*itPhy).second.sendTime >= startTime && (*itPhy).second.sendTime <= stopTime)
        {
            packetCounts.at(0)++;

            //NS_LOG_DEBUG("Dealing with packet " << (*itPhy).second.packet);
            //NS_LOG_DEBUG("This packet was received by " << (*itPhy).second.outcomes.size()<< " gateways");

            if ((*itPhy).second.outcomes.count(gwId) > 0)
            {
                switch ((*itPhy).second.outcomes.at(gwId))
                {
                case RECEIVED: {
                    packetCounts.at(1)++;
                    break;
                }
                case INTERFERED: {
                    packetCounts.at(2)++;
                    break;
                }
                case NO_MORE_RECEIVERS: {
                    packetCounts.at(3)++;
                    break;
                }
                case UNDER_SENSITIVITY: {
                    packetCounts.at(4)++;
                    break;
                }
                case LOST_BECAUSE_TX: {
                    packetCounts.at(5)++;
                    break;
                }
                case DUTY_CYCLE: {
                    break;

                }
                case UNSET: {
                    break;
                }
                }
            }
        }
    }

    return packetCounts;
}

std::string
LoraPacketTracker::PrintPhyPacketsPerGw(Time startTime, Time stopTime, int gwId)
{
    // Vector packetCounts will contain - for the interval given in the input of
    // the function, the following fields: totPacketsSent receivedPackets
    // interferedPackets noMoreGwPackets underSensitivityPackets lostBecauseTxPackets

    std::vector<int> packetCounts(CountPhyPacketsPerGw(startTime, stopTime, gwId));

    std::string output("");
    //for (int i = 0; i < 6; ++i) //Changed this by carlos
    for (int i = 0; i < 7; ++i)
    {
        output += std::to_string(packetCounts.at(i)) + " ";
    }

    return output;
}

void
LoraPacketTracker::CountPhyPacketsAllGws(Time startTime, Time stopTime, GwsPhyPktCount& output)
{

    output.clear();
    for (const auto& ppd : m_packetTracker)
    {
        if (ppd.second.sendTime >= startTime && ppd.second.sendTime <= stopTime)
        {
            //NS_LOG_DEBUG("Dealing with packet " << ppd.second.packet);
            //NS_LOG_DEBUG("This packet was received by " << ppd.second.outcomes.size() << " gateways");
            for (const auto& out : ppd.second.outcomes)
            {
                output[out.first].v[0]++;
                switch (out.second)
                {
                case RECEIVED: {
                    output[out.first].v[1]++;
                    break;
                }
                case INTERFERED: {
                    output[out.first].v[2]++;
                    break;
                }
                case NO_MORE_RECEIVERS: {
                    output[out.first].v[3]++;
                    break;
                }
                case LOST_BECAUSE_TX: {
                    output[out.first].v[4]++;
                    break;
                }
                case UNDER_SENSITIVITY: {
                    output[out.first].v[5]++;
                    break;
                }
                case DUTY_CYCLE: {
                    break;
                }
                
                case UNSET: {
                    break;
                }
                }
            }
        }
    }
}

void
LoraPacketTracker::PrintPhyPacketsAllGws(Time startTime, Time stopTime, GwsPhyPktPrint& output)
{
    output.clear();
    GwsPhyPktCount count;
    CountPhyPacketsAllGws(startTime, stopTime, count);
    for (const auto& gw : count)
    {
        std::string out("");
        for (int i = 0; i < 5; ++i)
        {
            out += std::to_string(gw.second.v[i]) + ",";
        }
        out += std::to_string(gw.second.v[5]);
        output[gw.first].s = out;
    }
}

std::string
LoraPacketTracker::PrintPhyPacketsGlobally(Time startTime, Time stopTime)
{
    NS_LOG_FUNCTION(this << startTime << stopTime);

    std::vector<int> count(6, 0);

    for (const auto& ppd : m_packetTracker)
    {
        if (ppd.second.sendTime >= startTime && ppd.second.sendTime <= stopTime)
        {
            count[0]++;
            bool received = false;
            bool interfered = false;
            bool noPaths = false;
            bool busyGw = false;
            for (const auto& out : ppd.second.outcomes)
            {
                if (out.second == RECEIVED)
                {
                    received = true;
                    break;
                }
                else if (!interfered and out.second == INTERFERED)
                {
                    interfered = true;
                }
                else if (!noPaths and out.second == NO_MORE_RECEIVERS)
                {
                    noPaths = true;
                }
                else if (!busyGw and out.second == LOST_BECAUSE_TX)
                {
                    busyGw = true;
                }
            }
            if (received)
            {
                count[1]++;
            }
            else if (interfered)
            {
                count[2]++;
            }
            else if (noPaths)
            {
                count[3]++;
            }
            else if (busyGw)
            {
                count[4]++;
            }
            else
            {
                count[5]++;
            }
        }
    }

    std::string output("");
    for (int i = 0; i < 5; ++i)
    {
        output += std::to_string(count[i]) + ",";
    }
    output += std::to_string(count[5]);
    return output;
}


/////////////////////////// Added by me
std::string
LoraPacketTracker::CountDLPackets(Time startTime, Time stopTime)
{
    NS_LOG_FUNCTION(this << startTime << stopTime);

    int sent = 0;
    int received = 0;
    for (auto it = m_macPacketTracker_DL.begin(); it != m_macPacketTracker_DL.end(); ++it)
    {
        if ((*it).second.sendTime >= startTime && (*it).second.sendTime <= stopTime)
        {
            

            if (!(*it).second.receptionTimes.empty())
            {
                received++;
            }
        }
    }

    return "Sent DOWNLINK: " + std::to_string(sent) + " - " + "Received DOWNLINK: " + std::to_string(received);
}
///////////////////////////
std::string
LoraPacketTracker::CountMacPacketsGlobally(Time startTime, Time stopTime)
{
    NS_LOG_FUNCTION(this << startTime << stopTime);

    int sent = 0;
    int received = 0;
    for (auto it = m_macPacketTracker.begin(); it != m_macPacketTracker.end(); ++it)
    {
        if ((*it).second.sendTime >= startTime && (*it).second.sendTime <= stopTime)
        {
            sent++;
            if (!(*it).second.receptionTimes.empty())
            {
                received++;
            }
        }
    }
    return "Sent UPLINKS: " + std::to_string(sent) + " - " + "Received UPLINKS: " + std::to_string(received);

}

std::string
LoraPacketTracker::CountMacPacketsGloballyCpsr(Time startTime, Time stopTime)
{
    NS_LOG_FUNCTION(this << startTime << stopTime);

    int sent = 0;
    int received = 0;
    for (auto it = m_reTransmissionTracker.begin(); it != m_reTransmissionTracker.end(); ++it)
    {
        if ((*it).second.firstAttempt >= startTime && (*it).second.firstAttempt <= stopTime)
        {
            sent++;
            //NS_LOG_DEBUG("Found a packet");
            //NS_LOG_DEBUG("Number of attempts: " << unsigned(it->second.reTxAttempts) << ", successful: " << it->second.successful);
            if (it->second.successful)
            {
                received++;
            }
        }
    }

    return "Confirm UPLINKS: " + std::to_string(sent) + " - " + "Received ACK: " + std::to_string(received);
}

std::string
LoraPacketTracker::PrintDevicePackets(Time startTime, Time stopTime, uint32_t devId)
{
    NS_LOG_FUNCTION(this << startTime << stopTime << devId);

    int sent = 0;
    int received = 0;
    for (auto it = m_macPacketTracker.begin(); it != m_macPacketTracker.end(); ++it)
    {
        if ((*it).second.sendTime >= startTime && (*it).second.sendTime <= stopTime &&
            (*it).second.senderId == devId)
        {
            sent++;
            if (!(*it).second.receptionTimes.empty())
            {
                received++;
            }
        }
    }

    return std::to_string(sent) + " " + std::to_string(received);
}

void
LoraPacketTracker::CountAllDevicesPackets_DL(Time startTime, Time stopTime, DevPktCount& output)
{
    NS_LOG_FUNCTION(this << startTime << stopTime);

    output.clear();
    auto it = m_macPacketTracker_DL.begin();
    for (const auto& mpd : m_packetTracker_DL)
    {
        if (mpd.second.sendTime >= startTime && mpd.second.sendTime <= stopTime)
        {
            for (const auto& out : mpd.second.outcomes){

                it = m_macPacketTracker_DL.find(mpd.first);

                output[out.first].sent++;


                if((*it).second.Rxwin==1){
                    output[out.first].Rx1++;
                    NS_LOG_DEBUG("Counting Reception MAC downlink Rx1:" << mpd.second.Rxwin <<" count "<< output[out.first].Rx1);

                }else if((*it).second.Rxwin==2){
                    NS_LOG_DEBUG("Counting Reception MAC downlink Rx2:" << mpd.second.Rxwin <<" count "<< output[out.first].Rx2);
                    output[out.first].Rx2++;
                }
                
                if((*it).second.Rxwin==11){
                    output[out.first].Rx1++;
                    output[out.first].Rx1_DC++;
                    NS_LOG_DEBUG("Counting Reception MAC downlink Rx11:" << mpd.second.Rxwin <<" count "<< output[out.first].Rx1_DC);
                }

                if((*it).second.Rxwin==12){
                    output[out.first].Rx2++;
                    output[out.first].Rx2_DC++;
                    NS_LOG_DEBUG("Counting Reception MAC downlink Rx12:" << mpd.second.Rxwin <<" count "<<output[out.first].Rx2_DC);
                }

                if(out.second == RECEIVED){
                    output[out.first].received++;
                    //NS_LOG_DEBUG("Counting Reception MAC downlink" << out.first <<"Rxwin"<< mpd.second.Rxwin);
                    if((*it).second.Rxwin==1){
                        output[out.first].Rx1_s++;
                        NS_LOG_DEBUG("Counting Reception Success MAC downlink Rx1:" << mpd.second.Rxwin<<" count "<<output[out.first].Rx1_s);
                    }else if((*it).second.Rxwin==2){
                        NS_LOG_DEBUG("Counting Reception Success MAC downlink Rx2:" << mpd.second.Rxwin<<" count "<<output[out.first].Rx2_s);
                        output[out.first].Rx2_s++;
                    }
                }
            }
        }
    }
}


void
LoraPacketTracker::LogUplinks(Time startTime, Time stopTime,NodeContainer gateways,NodeContainer endDevices, std::string filename){

    const char* c = filename.c_str();
    std::ofstream outputFile;
    if (Simulator::Now() == Seconds(0))
    {
        // Delete contents of the file as it is opened
        outputFile.open(c, std::ofstream::out | std::ofstream::trunc);
    }
    else
    {
        // Only append to the file
        outputFile.open(c, std::ofstream::out | std::ofstream::app);
    }


    GwsPhyPktPrint strings;
    PrintPhyPacketsAllGws(startTime, stopTime, strings);
    outputFile << "gatewayID,Received,Interfered,No Receivers, Under, GW busy, unset"<< std::endl;

    for (auto it = gateways.Begin(); it != gateways.End(); ++it)
    {
        int systemId = (*it)->GetId();
        outputFile  << std::to_string(systemId) << ","
                   << strings[systemId].s << std::endl;
    }



    outputFile << "EndDeviceID,X,Y,Z,SF"<< std::endl;
    for (NodeContainer::Iterator j = endDevices.Begin(); j != endDevices.End(); ++j)
    {
        auto node = *j;
        auto position = node->GetObject<MobilityModel>();
        auto loraNetDevice = DynamicCast<LoraNetDevice>(node->GetDevice(0));
        auto mac = DynamicCast<BaseEndDeviceLorawanMac>(loraNetDevice->GetMac());
        int DataRate_out =  int(mac->GetDataRate());
        



        Vector pos = position->GetPosition();
        outputFile 
        << node->GetId() <<","
         << pos.x << "," 
         << pos.y << "," 
         << pos.z << ","
         <<12 - DataRate_out << "," 
         <<std::endl; //        << maxot << "," << ot <<


    }
    outputFile << "senderId,receiverID, sendTime, receivedTime,SF,SNR,Ftype, fcnt,received, code"<< std::endl;



    for (const auto& ppd : m_ULpacketTracker)
    {

        if (ppd.second.sendTime >= startTime.GetNanoSeconds() && ppd.second.sendTime <= stopTime.GetNanoSeconds())
        {
            //NS_LOG_DEBUG("Dealing with packet " << ppd.second.packet);
            //NS_LOG_DEBUG("This packet was received by " << ppd.second.outcomes.size() << " gateways");
            //int index = 0;
/*             auto it2 = m_macPacketTracker.find(ppd.first);
            auto it3 = (*it2).second.receptionTimes.begin();
 */

            LoraFrameHeader fHdr;
 
                // Work on a copy of the packet
            Ptr<Packet> packetCopy = ppd.first->Copy();
            // Remove MIC (currently we do not check it)
            packetCopy->RemoveAtEnd(4);
            packetCopy->RemoveHeader(fHdr);
            for (const auto& out : ppd.second.pktInfoCom){

                int64_t r_time = out.second.ReceiveTime;
                uint32_t ID_r = out.first;
                outputFile << ppd.second.senderId << ','
                << ID_r << ','
                << ppd.second.sendTime << ','
                << r_time << ','
                << out.second.SF << ','
                << out.second.snr << ','
                << ppd.second.type<< ','
                <<int(fHdr.GetFCnt()) <<','
                << out.second.received <<','
                << out.second.code<< ','
                <<std::endl;
                        
            }

            
        }
    }

    outputFile << "senderId,receiverID, sendTime, receivedTime,SF,SNR,Ftype, fcnt,RX"<< std::endl;


    for (const auto& ppd : m_DLpacketTracker)
    {

        if (ppd.second.sendTime >= startTime.GetNanoSeconds() && ppd.second.sendTime <= stopTime.GetNanoSeconds())
        {
            //NS_LOG_DEBUG("Dealing with packet " << ppd.second.packet);
            //NS_LOG_DEBUG("This packet was received by " << ppd.second.outcomes.size() << " gateways");
            //int index = 0;
/*             auto it2 = m_macPacketTracker.find(ppd.first);
            auto it3 = (*it2).second.receptionTimes.begin();
 */ 

            LoraFrameHeader fHdr;
            auto it = m_macPacketTracker_DL.find(ppd.first);

                // Work on a copy of the packet
            Ptr<Packet> packetCopy = ppd.first->Copy();
            // Remove MIC (currently we do not check it)
            packetCopy->RemoveAtEnd(4);
            packetCopy->RemoveHeader(fHdr);
            for (const auto& out : ppd.second.pktInfoCom){

                int64_t r_time = out.second.ReceiveTime;
                uint32_t ID_r = out.first;
                outputFile << ppd.second.senderId << ','
                << ID_r << ','
                << ppd.second.sendTime << ','
                << r_time << ','
                << out.second.SF << ','
                << out.second.snr << ','
                << ppd.second.type<< ','<<
                u_int16_t(fHdr.GetFCnt()) <<',' <<
                (*it).second.Rxwin<<std::endl;
                        
            }

            
        }
    }

    outputFile.close();

}


void
LoraPacketTracker::CountAllDevicesPackets(Time startTime, Time stopTime, DevPktCount& out)
{
    NS_LOG_FUNCTION(this << startTime << stopTime);

    out.clear();
    auto it = m_packetTracker.begin();
    bool busyGw = false;
    bool interfered = false;
    bool noPaths = false;
    for (const auto& mpd : m_macPacketTracker)
    {
        it = m_packetTracker.find(mpd.first);

        if (mpd.second.sendTime >= startTime && mpd.second.sendTime <= stopTime)
        {
            out[mpd.second.senderId].sent++;
            if((*it).second.type == 4) {
                out[mpd.second.senderId].Csent++;
            }
            else{
                out[mpd.second.senderId].UCsent++;
            }

            if (mpd.second.receptionTimes.size()){
                out[mpd.second.senderId].received++;
                if((*it).second.type == 4) {
                    out[mpd.second.senderId].Creceived++;
                }
                else{
                    out[mpd.second.senderId].UCreceived++;
                }



            }else{

                busyGw = false;
                interfered = false;
                noPaths = false;
                for (const auto& outt : (*it).second.outcomes){

                    if (outt.second == RECEIVED)
                    {
                        break;
                    }

                    if (!interfered and outt.second == INTERFERED)
                    {
                        interfered = true;
                        break;

                    }
                    else if (!noPaths and outt.second == NO_MORE_RECEIVERS)
                    {
                        noPaths = true;
                        break;

                    }
                    else if (!busyGw and outt.second == LOST_BECAUSE_TX){
                        busyGw = true;
                    }

                }
                if(!noPaths && !interfered && busyGw){
                        if((*it).second.type == 4) {
                            out[mpd.second.senderId].HDLossCUL++;
                            }
                        else{
                            out[mpd.second.senderId].HDLossUUL++;
                            }
                }

                





            }
                


        }
    }
}

std::string
LoraPacketTracker::PrintSimulationStatistics(Time startTime)
{
    NS_ASSERT(startTime < Simulator::Now());

    uint32_t total = 0;
    double totReceived = 0;
    double totInterfered = 0;
    double totNoMorePaths = 0;
    double totBusyGw = 0;
    double totUnderSens = 0;
    double unconfirmedBusyGw = 0;
    double ConfirmedBusyGW = 0 ;
    double unconfirmedUnder = 0;
    double ConfirmedBusyUnder = 0 ;

    std::vector<double> sentSF(6, 0);
    std::vector<double> receivedSF(6, 0);

    double totBytesReceived = 0;
    double totBytesSent = 0;

    double totOffTraff = 0.0;

    for (const auto& pd : m_packetTracker)
    {
        if (pd.second.sendTime < startTime - Seconds(5))
        {
            continue;
        }

        bool received = false;
        bool interfered = false;
        bool noPaths = false;
        bool busyGw = false;

        LoraPhyTxParameters params;
        LoraTag tag;
        pd.first->Copy()->RemovePacketTag(tag);
        
        LorawanMacHeader mHdr;
        Ptr<Packet> copy = pd.first->Copy();
        copy->RemoveHeader(mHdr);
        uint8_t typeFrame = mHdr.GetFType();


        params.sf = tag.GetTxParameters().sf;
        params.lowDataRateOptimizationEnabled = LoraPhy::GetTSym(params) > MilliSeconds(16);
        totOffTraff += LoraPhy::GetTimeOnAir(pd.first->Copy(), params).GetSeconds();

        total++;
        totBytesSent += pd.first->GetSize();
        sentSF[tag.GetDataRate()]++;
        for (const auto& out : pd.second.outcomes)
        {
            if (out.second == RECEIVED)
            {
                received = true;
                receivedSF[tag.GetDataRate()]++;
                totBytesReceived += pd.first->GetSize();
                break;
            }
            else if (!interfered and out.second == INTERFERED)
            {
                interfered = true;
            }
            else if (!noPaths and out.second == NO_MORE_RECEIVERS)
            {
                noPaths = true;
            }
            else if (!busyGw and out.second == LOST_BECAUSE_TX)
            {
                busyGw = true;
            }

        }
        if (received)
        {
            totReceived++;
        }
        else if (interfered)
        {
            totInterfered++;
        }
        else if (noPaths)
        {
            totNoMorePaths++;
        }
        else if (busyGw)
        {
            
            totBusyGw++;
            if(typeFrame == 4||typeFrame == 5){
                ConfirmedBusyGW++;

            }else{
                unconfirmedBusyGw++;
            }
        }
        else
        {
            totUnderSens++;
            if(typeFrame == 4||typeFrame == 5){
                ConfirmedBusyUnder++;

            }else{
                unconfirmedUnder++;
            }

        }
    }

    std::stringstream ss;
    ss << "\nPackets outcomes distribution (" << total << " sent, " << totReceived << " received):"
       << "\n  RECEIVED: " << totReceived / total * 100
       << "%\n  INTERFERED: " << totInterfered / total * 100
       << "%\n  NO_MORE_RECEIVERS: " << totNoMorePaths / total * 100
       << "%\n  BUSY_GATEWAY: " << totBusyGw / total * 100
       << "%\n  UNDER_SENSITIVITY: " << totUnderSens / total * 100       
       << "%\n  CONF BUSY_GATEWAY: " << ConfirmedBusyGW / total * 100
       << "%\n UNCONF BUSY_GATEWAY: " << unconfirmedBusyGw / total * 100
       << "%\n CONF UNDER_SENSITIVITY: " << ConfirmedBusyUnder  / total * 100
       << "%\n  UNCONF UNDER_SENSITIVITY: " << unconfirmedUnder / total * 100 << "%\n"
       << "----------------------------------------------------------------"
       << "\n  RECEIVED: " << totReceived
       << "\n  INTERFERED: " << totInterfered 
       << "\n  NO_MORE_RECEIVERS: " << totNoMorePaths 
       << "\n  BUSY_GATEWAY: " << totBusyGw
       << "\n  UNDER_SENSITIVITY: " << totUnderSens
       << "\n  CONF BUSY_GATEWAY: " << ConfirmedBusyGW 
       << "\n UNCONF BUSY_GATEWAY: " << unconfirmedBusyGw
        << "\n  CONF UNDER_SENSITIVITY: " << ConfirmedBusyUnder 
       << "\n UNCONF UNDER_SENSITIVITY: " << unconfirmedUnder   << "\n";

    ss << "\nPDR: ";
    for (int dr = 5; dr >= 0; --dr)
    {
        ss << "SF" << 12 - dr << " " << receivedSF[dr] / sentSF[dr] * 100 << "%, ";
    }
    ss << "\n";

    double totTime = (Simulator::Now() - startTime).GetSeconds();
    ss << "\nInput Traffic: " << totBytesSent * 8 / totTime
       << " b/s\nNetwork Throughput: " << totBytesReceived * 8 / totTime << " b/s\n";

    totOffTraff /= totTime;
    ss << "\nTotal (empirical) offered traffic: " << totOffTraff << " E\n";

    return ss.str();
}

/// @brief //
/// @param oldValue 
/// @param newValue 
void
LoraPacketTracker::IntTrace_correct(int32_t oldValue, int32_t newValue)
{
   //std::cout << "correctSchedule Traced " << oldValue << " to " << newValue << std::endl;
   correctSchedule++;
}
void
LoraPacketTracker::IntTrace_Tx_loss(int32_t oldValue, int32_t newValue)
{
    //std::cout << "TxLoss Traced " << oldValue << " to " << newValue << std::endl;
    TxLoss++;
}
void 
LoraPacketTracker::IntTrace_correct_Tx(int32_t oldValue, int32_t newValue){
    //std::cout << "TxLoss Traced " << oldValue << " to " << newValue << std::endl;
    correctTx++;


}

void LoraPacketTracker::printTraces()const
{
      std::cout << "#Pkt Scheduled: " << correctSchedule<< "\n" << "#Pkt drop late: " << correctSchedule-correctTx-TxLoss<< "\n"  << "# Pkt Loss due Tx: " << TxLoss<< "\n"<< "# Pkt Tx: " <<correctTx << std::endl;


}

////


std::string
LoraPacketTracker::PrintSimulationStatistics_DL(Time startTime, std::string filename)
{
    NS_ASSERT(startTime < Simulator::Now());

    uint32_t total = 0;
    double totReceived = 0;
    double totInterfered = 0;
    double totNoMorePaths = 0;
    double totBusyGw = 0;
    double totUnderSens = 0;
    double totdutycycle = 0;

    std::vector<double> sentSF(6, 0);
    std::vector<double> receivedSF(6, 0);

    double totBytesReceived = 0;
    double totBytesSent = 0;

    double totOffTraff = 0.0;

    for (const auto& pd : m_packetTracker_DL)
    {
        if (pd.second.sendTime < startTime - Seconds(5))
        {
            continue;
        }

        bool received = false;
        bool interfered = false;
        bool noPaths = false;
        bool busyGw = false;
        bool dutyCycle = false;


        LoraPhyTxParameters params;
        LoraTag tag;
        pd.first->Copy()->RemovePacketTag(tag);
        params.sf = tag.GetTxParameters().sf;
        params.lowDataRateOptimizationEnabled = LoraPhy::GetTSym(params) > MilliSeconds(16);
        totOffTraff += LoraPhy::GetTimeOnAir(pd.first->Copy(), params).GetSeconds();

        total++;
        totBytesSent += pd.first->GetSize();
        sentSF[tag.GetDataRate()]++;
        for (const auto& out : pd.second.outcomes)
        {
            if (out.second == RECEIVED)
            {
                received = true;
                receivedSF[tag.GetDataRate()]++;
                totBytesReceived += pd.first->GetSize();
                break;
            }
            else if (!interfered and out.second == INTERFERED)
            {
                interfered = true;
            }
            else if (!noPaths and out.second == NO_MORE_RECEIVERS)
            {
                noPaths = true;
            }
            else if (!busyGw and out.second == LOST_BECAUSE_TX)
            {
                busyGw = true;
            }
            else if (!dutyCycle and out.second == DUTY_CYCLE)
            {
                dutyCycle = true;
            }
        }
        if (received)
        {
            totReceived++;
        }
        else if (interfered)
        {
            totInterfered++;
        }
        else if (noPaths)
        {
            totNoMorePaths++;
        }
        else if (busyGw)
        {
            totBusyGw++;
        }
        else if (dutyCycle)
        {
            totdutycycle++;
        }
        else
        {
            totUnderSens++;
        }
    }
    std::ofstream outputFile;

    outputFile.open(filename, std::ofstream::out | std::ofstream::trunc);
    // Write a header row
    outputFile << "Time,DUTY_CYCLE(%),RECEIVED(%),INTERFERED(%),NO_MORE_RECEIVERS(%),BUSY_GATEWAY(%),"
               << "UNDER_SENSITIVITY(%),SENT,DUTY_CYCLE,RECEIVED,INTERFERED,NO_MORE_RECEIVERS,"
               << "BUSY_GATEWAY,UNDER_SENSITIVITY\n";
    // Extract and format CSV row from `ss`
    outputFile << Simulator::Now().GetSeconds() << ","  // Current simulation time
            << totdutycycle / total * 100 << ","     // Duty Cycle (%)
            << totReceived / total * 100 << ","      // Received (%)
            << totInterfered / total * 100 << ","    // Interfered (%)
            << totNoMorePaths / total * 100 << ","   // No More Receivers (%)
            << totBusyGw / total * 100 << ","        // Busy Gateway (%)
            << totUnderSens / total * 100 << ","     // Under Sensitivity (%)
            << total << ","                          // Sent
            << totdutycycle << ","                   // Duty Cycle
            << totReceived << ","                    // Received
            << totInterfered << ","                  // Interfered
            << totNoMorePaths << ","                 // No More Receivers
            << totBusyGw << ","                      // Busy Gateway
            << totUnderSens                          // Under Sensitivity
            << "\n";
    // Close the file
    outputFile.close();













    std::stringstream ss;
    ss << "\n   Packets outcomes distribution (" << total << " sent, " << totReceived << " received):"
       << "%\n  DUTY CYCLE: " << totdutycycle / total * 100 
       << "\n   RECEIVED: " << totReceived / total * 100
       << "%\n  INTERFERED: " << totInterfered / total * 100
       << "%\n  NO_MORE_RECEIVERS: " << totNoMorePaths / total * 100
       << "%\n  BUSY_GATEWAY: " << totBusyGw / total * 100
       << "%\n  UNDER_SENSITIVITY: " << totUnderSens / total * 100 << "%\n"
       << "----------------------------------------------------------------"
        << "%\n SCHEDULLED: " << total
       << "%\n  DUTY CYCLE: " << totdutycycle
       << "\n  RECEIVED: " << totReceived
       << "\n  INTERFERED: " << totInterfered 
       << "\n  NO_MORE_RECEIVERS: " << totNoMorePaths 
       << "\n  BUSY_GATEWAY: " << totBusyGw
       << "\n  UNDER_SENSITIVITY: " << totUnderSens << "\n";

    ss << "\nPDR: ";
    for (int dr = 5; dr >= 0; --dr)
    {
        ss << "SF" << 12 - dr << " " << receivedSF[dr] / sentSF[dr] * 100 << "%, ";
    }
    ss << "\n";

    double totTime = (Simulator::Now() - startTime).GetSeconds();
    ss << "\nInput Traffic: " << totBytesSent * 8 / totTime
       << " b/s\nNetwork Throughput: " << totBytesReceived * 8 / totTime << " b/s\n";

    totOffTraff /= totTime;
    ss << "\nTotal (empirical) offered traffic: " << totOffTraff << " E\n";

    return ss.str();
}






















































void
LoraPacketTracker::EnableOldPacketsCleanup(Time oldPacketThreshold)
{
    NS_ASSERT_MSG(
        oldPacketThreshold > Minutes(30),
        "Threshold to consider packets old should be > 30 min to avoid risk of partial entries");
    m_oldPacketThreshold = oldPacketThreshold;
}

void
LoraPacketTracker::CleanupOldPackets()
{
    if (m_oldPacketThreshold.IsZero())
    {
        return;
    }
    if (Simulator::Now() < m_lastPacketCleanup + m_oldPacketThreshold)
    {
        return;
    }

    for (auto it = m_packetTracker.cbegin(); it != m_packetTracker.cend();)
    {
        if ((*it).second.sendTime < Simulator::Now() - m_oldPacketThreshold)
        {
            it = m_packetTracker.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = m_macPacketTracker.cbegin(); it != m_macPacketTracker.cend();)
    {
        if ((*it).second.sendTime < Simulator::Now() - m_oldPacketThreshold)
        {
            it = m_macPacketTracker.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = m_reTransmissionTracker.cbegin(); it != m_reTransmissionTracker.cend();)
    {
        if ((*it).second.firstAttempt < Simulator::Now() - m_oldPacketThreshold)
        {
            it = m_reTransmissionTracker.erase(it);
        }
        else
        {
            ++it;
        }
    }

    m_lastPacketCleanup = Simulator::Now();
}

} // namespace lorawan
} // namespace ns3
