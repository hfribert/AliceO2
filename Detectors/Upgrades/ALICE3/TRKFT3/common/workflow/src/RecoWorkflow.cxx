// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "TRKWorkflow/RecoWorkflow.h"
#include "TRKWorkflow/ClustererSpec.h"
#include "TRKWorkflow/ClusterWriterSpec.h"
#include "TRKWorkflow/DigitReaderSpec.h"
#include "Framework/CCDBParamSpec.h"

#include <string>

namespace o2::trk::reco_workflow
{

framework::WorkflowSpec getWorkflow(bool useMC,
                                    bool upstreamDigits,
                                    bool upstreamClusters,
                                    bool disableRootOutput,
                                    bool enableFT3)
{
  framework::WorkflowSpec specs;

  if (!(upstreamDigits || upstreamClusters)) {
    specs.emplace_back(o2::trk::getTRKDigitReaderSpec(useMC, false, "trkdigits.root"));
    if (enableFT3) {
      specs.emplace_back(o2::trk::getFT3DigitReaderSpec(useMC, false, "ft3digits.root"));
    }
  }
  if (!upstreamClusters) {
    specs.emplace_back(o2::trk::getTRKClustererSpec(useMC));
    if (enableFT3) {
      specs.emplace_back(o2::trk::getFT3ClustererSpec(useMC));
    }
  }

  if (!disableRootOutput) {
    specs.emplace_back(o2::trk::getTRKClusterWriterSpec(useMC));
    if (enableFT3) {
      specs.emplace_back(o2::trk::getFT3ClusterWriterSpec(useMC));
    }
  }

  return specs;
}

} // namespace o2::trk::reco_workflow
