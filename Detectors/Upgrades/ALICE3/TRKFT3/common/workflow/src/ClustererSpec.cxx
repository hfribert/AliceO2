// Copyright 2019-2026 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "TRKWorkflow/ClustererSpec.h"
#include "DetectorsBase/GeometryManager.h"
#include "DataFormatsTRKFT3/Cluster.h"
#include "DataFormatsTRKFT3/ROFRecord.h"
#include "Framework/ConfigParamRegistry.h"
#include "Framework/Logger.h"
#include "Headers/DataHeader.h"
#include "SimulationDataFormat/ConstMCTruthContainer.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace o2::trk
{

template <int DetID>
void ClustererDPL<DetID>::init(o2::framework::InitContext& ic)
{
  mNThreads = std::max(1, ic.options().get<int>("nthreads"));
#ifdef O2_WITH_ACTS
  if constexpr (DetID == o2::detectors::DetID::TRK) {
    mUseACTS = ic.options().get<bool>("useACTS");
  }
#endif
}

template <int DetID>
void ClustererDPL<DetID>::run(o2::framework::ProcessingContext& pc)
{
  static constexpr o2::header::DataOrigin Origin{DetID == o2::detectors::DetID::TRK ? o2::header::gDataOriginTRK : o2::header::gDataOriginFT3};
  const auto detName = Origin.as<std::string>();

  o2::base::GeometryManager::loadGeometry(); // default prefix -> o2sim_geometry[-aligned].root

  uint64_t totalClusters = 0;
  for (int iLayer = 0; iLayer < mLayers; ++iLayer) {
    auto digits = pc.inputs().get<gsl::span<o2::trkft3::Digit>>(std::format("digits_{}", iLayer));
    auto rofs = pc.inputs().get<gsl::span<o2::trkft3::ROFRecord>>(std::format("ROframes_{}", iLayer));

    gsl::span<const char> labelbuffer;
    if (mUseMC) {
      labelbuffer = pc.inputs().get<gsl::span<char>>(std::format("labels_{}", iLayer));
    }
    o2::dataformats::ConstMCTruthContainerView<o2::MCCompLabel> labels(labelbuffer);

    std::vector<o2::trkft3::Cluster<DetID>> clusters;
    std::vector<unsigned char> patterns;
    std::vector<uint32_t> pixCharge; ///< per-pixel charge, cluster i owns the next clusters[i].size entries
    std::vector<o2::trkft3::ROFRecord> clusterROFs;
    std::unique_ptr<o2::dataformats::MCTruthContainer<o2::MCCompLabel>> clusterLabels;
    if (mUseMC) {
      clusterLabels = std::make_unique<o2::dataformats::MCTruthContainer<o2::MCCompLabel>>();
    }

#ifdef O2_WITH_ACTS
    if constexpr (DetID == o2::detectors::DetID::TRK) {
      if (mUseACTS) {
        LOG(info) << detName << "Clusterer running with ACTS on layer " << iLayer;
        mClustererACTS.process(digits,
                               rofs,
                               clusters,
                               patterns,
                               pixCharge,
                               clusterROFs,
                               mUseMC ? &labels : nullptr,
                               clusterLabels.get());
      } else {
        LOG(info) << detName << "Clusterer running on layer " << iLayer;
        mClusterer.process(digits,
                           rofs,
                           clusters,
                           patterns,
                           pixCharge,
                           clusterROFs,
                           mUseMC ? &labels : nullptr,
                           clusterLabels.get());
      }
    } else
#endif
    {
      LOG(info) << detName << "Clusterer running on layer " << iLayer;
      mClusterer.process(digits,
                         rofs,
                         clusters,
                         patterns,
                         pixCharge,
                         clusterROFs,
                         mUseMC ? &labels : nullptr,
                         clusterLabels.get());
    }

    const auto subspec = static_cast<o2::framework::DataAllocator::SubSpecificationType>(iLayer);
    pc.outputs().snapshot(o2::framework::Output{Origin, "COMPCLUSTERS", subspec}, clusters);
    pc.outputs().snapshot(o2::framework::Output{Origin, "PATTERNS", subspec}, patterns);
    pc.outputs().snapshot(o2::framework::Output{Origin, "PIXCHARGE", subspec}, pixCharge);
    pc.outputs().snapshot(o2::framework::Output{Origin, "CLUSTERSROF", subspec}, clusterROFs);
    if (mUseMC) {
      pc.outputs().snapshot(o2::framework::Output{Origin, "CLUSTERSMCTR", subspec}, *clusterLabels);
    }
    totalClusters += clusters.size();
    LOGP(info, "{}Clusterer layer {} pushed {} clusters in {} ROFs", detName, iLayer, clusters.size(), clusterROFs.size());
  }

  LOGP(info, "{}Clusterer produced {} clusters", detName, totalClusters);
}

template <int DetID>
o2::framework::DataProcessorSpec getClustererSpecT(bool useMC)
{
  static_assert(DetID == o2::detectors::DetID::TRK || DetID == o2::detectors::DetID::FT3, "only TRK and FT3 clusterers are supported");
  static constexpr o2::header::DataOrigin Origin{DetID == o2::detectors::DetID::TRK ? o2::header::gDataOriginTRK : o2::header::gDataOriginFT3};
  static constexpr int nLayers = DetID == o2::detectors::DetID::TRK ? o2::trk::AlmiraParam::kNLayers : o2::trk::constants::MLOTDisks::nLayers;
  const auto detName = Origin.as<std::string>();
  auto detNameLC = detName;
  std::transform(detNameLC.begin(), detNameLC.end(), detNameLC.begin(), [](unsigned char c) { return std::tolower(c); });

  std::vector<o2::framework::InputSpec> inputs;
  for (int iLayer = 0; iLayer < nLayers; ++iLayer) {
    inputs.emplace_back(std::format("digits_{}", iLayer), Origin, "DIGITS", iLayer, o2::framework::Lifetime::Timeframe);
    inputs.emplace_back(std::format("ROframes_{}", iLayer), Origin, "DIGITSROF", iLayer, o2::framework::Lifetime::Timeframe);
    if (useMC) {
      inputs.emplace_back(std::format("labels_{}", iLayer), Origin, "DIGITSMCTR", iLayer, o2::framework::Lifetime::Timeframe);
    }
  }

  std::vector<o2::framework::OutputSpec> outputs;
  for (int iLayer = 0; iLayer < nLayers; ++iLayer) {
    outputs.emplace_back(Origin, "COMPCLUSTERS", iLayer, o2::framework::Lifetime::Timeframe);
    outputs.emplace_back(Origin, "PATTERNS", iLayer, o2::framework::Lifetime::Timeframe);
    outputs.emplace_back(Origin, "PIXCHARGE", iLayer, o2::framework::Lifetime::Timeframe);
    outputs.emplace_back(Origin, "CLUSTERSROF", iLayer, o2::framework::Lifetime::Timeframe);
    if (useMC) {
      outputs.emplace_back(Origin, "CLUSTERSMCTR", iLayer, o2::framework::Lifetime::Timeframe);
    }
  }

  return o2::framework::DataProcessorSpec{
    std::format("{}-clusterer", detNameLC),
    inputs,
    outputs,
    o2::framework::AlgorithmSpec{o2::framework::adaptFromTask<o2::trk::ClustererDPL<DetID>>(useMC)},
    o2::framework::Options{{"nthreads", o2::framework::VariantType::Int, 1, {"Number of clustering threads"}}
#ifdef O2_WITH_ACTS
                           ,
                           {"useACTS", o2::framework::VariantType::Bool, false, {"Use ACTS for clustering (TRK only)"}}
#endif
    }};
}

o2::framework::DataProcessorSpec getTRKClustererSpec(bool useMC)
{
  return getClustererSpecT<o2::detectors::DetID::TRK>(useMC);
}

o2::framework::DataProcessorSpec getFT3ClustererSpec(bool useMC)
{
  return getClustererSpecT<o2::detectors::DetID::FT3>(useMC);
}

o2::framework::DataProcessorSpec getClustererSpec(bool useMC)
{
  return getTRKClustererSpec(useMC);
}

template class ClustererDPL<o2::detectors::DetID::TRK>;
template class ClustererDPL<o2::detectors::DetID::FT3>;

} // namespace o2::trk
