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
 * Author: Carlos
 *
 */

#ifndef COUTAUD_LOSS_H
#define COUTAUD_LOSS_H

#include "ns3/mobility-model.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/random-variable-stream.h"
#include "ns3/vector.h"

namespace ns3
{
class MobilityModel;

namespace lorawan
{

/**
 * A class implementing the TR 45.820 model for building losses
 */
class CoutadLoss : public PropagationLossModel
{
  public:
    static TypeId GetTypeId();
    CoutadLoss();
    int64_t DoAssignStreams(int64_t stream) override;
    void SetNGateways(uint32_t nGateways) ;
    ~CoutadLoss() override;

  private:
    /**
     * Perform the computation of the received power according to the current
     * model.
     */
    double DoCalcRxPower(double txPowerDbm,
                         Ptr<MobilityModel> a,
                         Ptr<MobilityModel> b) const override;


    /**
     * Generate a random p value.
     * The distribution of the returned value is as specified in TR 45.820.
     * \returns A value in the 0-3 range.
     */
    double GetNextGaussian(uint32_t value) const;





    Ptr<UniformRandomVariable> m_uniformRV; //!< An uniform RV
    Ptr<UniformRandomVariable> m_uniformRV2; //!< An uniform RV
    uint32_t nGateways;


};
} // namespace lorawan
} // namespace ns3
#endif
