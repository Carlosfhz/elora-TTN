/*
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
 * 17/01/2023
 * Modified by: Alessandro Aimi <alessandro.aimi@orange.com>
 *                              <alessandro.aimi@cnam.fr>
 */

#include "coutad-loss.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include <cmath>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "ns3/node-list.h"//added by me
#include "ns3/node.h"  // Ensure this is included!
#include "ns3/lorawan-mac-helper.h"
#include "ns3/class-a-end-device-lorawan-mac.h"

#define PATH_LOSS_EXPONENT 2.75f
#define PL0 74.85f           // Path loss at reference distance (in dB)
#define D0 1.0f              // Reference distance (in meters)
#define SHADOWING_STD 11.25f   // Shadowing standard deviation


namespace ns3
{
namespace lorawan
{

NS_LOG_COMPONENT_DEFINE("CoutadLoss");

NS_OBJECT_ENSURE_REGISTERED(CoutadLoss);

TypeId
CoutadLoss::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CoutadLoss")
                            .SetParent<PropagationLossModel>()
                            .SetGroupName("Lora")
                            .AddConstructor<CoutadLoss>();
    return tid;
}

CoutadLoss::CoutadLoss()
{
    NS_LOG_FUNCTION_NOARGS();

    // Initialize the random variable
    m_uniformRV = CreateObject<UniformRandomVariable>();
    m_uniformRV2 = CreateObject<UniformRandomVariable>();

}

CoutadLoss::~CoutadLoss()
{
    NS_LOG_FUNCTION_NOARGS();
    m_uniformRV = nullptr;
    m_uniformRV2 = nullptr;

}

double
CoutadLoss::DoCalcRxPower(double txPowerDbm,
                                       Ptr<MobilityModel> a,
                                       Ptr<MobilityModel> b) const
{
    NS_LOG_DEBUG(this <<" "  << txPowerDbm << a << b);

    NS_LOG_DEBUG("random see " << m_uniformRV->GetValue());
    
    // Retrieve the nodes associated with a and b
    Ptr<Node> nodeA = a->GetObject<Node>();
    Ptr<Node> nodeB = b->GetObject<Node>();
    NS_LOG_ERROR("Retrieve NetDevices");
    
    double distance = a->GetDistanceFrom(b);
    //NS_LOG_DEBUG("Distance" << distance);

    float LSF = (float)(PL0 + 10 * PATH_LOSS_EXPONENT * std::log10(distance / D0));
    //NS_LOG_DEBUG("Total LSF " << LSF);

    float ShF;
    float SSF;
    ShF = GetNextGaussian(nodeB->GetId()) * SHADOWING_STD;
    if (nodeB->GetId()>nGateways) {
        NS_LOG_DEBUG("This is a UL");
        SSF = (float)(log(1 - double(m_uniformRV2->GetValue())) / -1.0f);

    }else{
        NS_LOG_DEBUG("This is a DL");
        SSF = (float)(log(1 - double(m_uniformRV->GetValue())) / -1.0f);

    }

    //NS_LOG_DEBUG("Total ShF " << ShF);

    double loss = (double)(LSF + ShF + SSF);


   // Ptr<LorawanMac>macA = netDeviceA->GetMac();
    //Ptr<LorawanMac> macB =netDeviceB->GetMac();
    //if (!macA || !macB) {
        //NS_LOG_ERROR("Could not retrieve MAC layers");
    //}



    return txPowerDbm - loss;
}

int64_t
CoutadLoss::DoAssignStreams(int64_t stream)
{
    m_uniformRV->SetStream(stream);
    m_uniformRV2->SetStream(stream);
    return 1;
}



double
CoutadLoss::GetNextGaussian(uint32_t value) const
{
    NS_LOG_FUNCTION_NOARGS();
    NS_LOG_DEBUG("GetNextGaussian" << value );

    double v1, v2, s;
    do {
        if (value>nGateways) {

            v1 = 2.0 * double (m_uniformRV2->GetValue()) - 1.0;
            v2 = 2.0 * double (m_uniformRV2->GetValue()) - 1.0;

        }else{

            v1 = 2.0 * double (m_uniformRV->GetValue()) - 1.0;
            v2 = 2.0 * double (m_uniformRV->GetValue()) - 1.0;   
        }

            
        s = v1 * v1 + v2 * v2;
        NS_LOG_DEBUG("GetNextGaussian" << s  );
    } while (s >= 1.0 || s == 0.0);

    s = sqrt(-2.0 * log(s) / s);
    //NS_LOG_DEBUG("random: " << (float)(v1 * s));
    NS_LOG_DEBUG("Finish" << (double)(v1 * s));

    return (double)(v1 * s);
}
void
CoutadLoss::SetNGateways(uint32_t ngw) 
{

    nGateways = ngw;

}


} // namespace lorawan
} // namespace ns3
