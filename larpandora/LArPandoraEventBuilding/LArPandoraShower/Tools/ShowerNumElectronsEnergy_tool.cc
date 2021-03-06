//############################################################################
//### Name:        ShowerNumElectronsEnergy                                ###
//### Author:      Tom Ham                                                 ###
//### Date:        01/04/2020                                              ###
//### Description: Tool for finding the Energy of the shower by going      ###
//###              from number of hits -> number of electrons -> energy.   ###
//###              Derived from the linear energy algorithm, written for   ###
//###              the EMShower_module.cc                                  ###
//############################################################################

//Framework Includes
#include "art/Utilities/ToolMacros.h"

//LArSoft Includes
#include "larpandora/LArPandoraEventBuilding/LArPandoraShower/Tools/IShowerTool.h"
#include "lardataobj/RecoBase/Cluster.h"
#include "larcoreobj/SimpleTypesAndConstants/PhysicalConstants.h"
#include "larreco/Calorimetry/CalorimetryAlg.h"

//C++ Includes
#include <tuple>

namespace ShowerRecoTools {

  class ShowerNumElectronsEnergy:IShowerTool {

    public:

      ShowerNumElectronsEnergy(const fhicl::ParameterSet& pset);

      //Physics Function. Calculate the shower Energy.
      int CalculateElement(const art::Ptr<recob::PFParticle>& pfparticle,
          art::Event& Event,
          reco::shower::ShowerElementHolder& ShowerElementHolder
          ) override;

    private:

      double CalculateEnergy(const detinfo::DetectorClocksData& clockData,
          const detinfo::DetectorPropertiesData& detProp,
          std::vector<art::Ptr<recob::Hit> >& hits,
          geo::View_t& view);

      art::InputTag fPFParticleLabel;
      int fVerbose;

      //Services
      art::ServiceHandle<geo::Geometry> fGeom;
      calo::CalorimetryAlg              fCalorimetryAlg;

      // Declare stuff
      double Energy = 0;
      double fRecombinationFactor;

      // vec to store subrun #, event #, shower #, # of hits and energy
      std::vector<std::tuple<int, int, int, int, double>> n_hit_energy; // more useful when making plots

      int showernum = 0;

  };

  ShowerNumElectronsEnergy::ShowerNumElectronsEnergy(const fhicl::ParameterSet& pset) :
    IShowerTool(pset.get<fhicl::ParameterSet>("BaseTools")),
    fPFParticleLabel(pset.get<art::InputTag>("PFParticleLabel")),
    fVerbose(pset.get<int>("Verbose")),
    fCalorimetryAlg(pset.get<fhicl::ParameterSet>("CalorimetryAlg")),
    fRecombinationFactor(pset.get<double>("RecombinationFactor"))
  {
  }

  int ShowerNumElectronsEnergy::CalculateElement(const art::Ptr<recob::PFParticle>& pfparticle,
      art::Event& Event,
      reco::shower::ShowerElementHolder& ShowerEleHolder
      ){

    if (fVerbose)
      std::cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Shower Reco Energy Tool ~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << std::endl;

    // get shower number per event
    showernum = ShowerEleHolder.GetShowerNumber();

    // get subrun number
    art::SubRunNumber_t subRunN = Event.subRun();

    // get event number
    art::EventNumber_t EventN = Event.id().event();

    //ShowerEleHolder.PrintElements();

    //Holder for the final product
    std::vector<double> ShowerNumElectronsEnergy;

    // Get the number of planes
    unsigned int numPlanes = fGeom->Nplanes();

    // Get the assocated pfParicle vertex PFParticles
    auto const pfpHandle = Event.getValidHandle<std::vector<recob::PFParticle> >(fPFParticleLabel);

    std::map<geo::View_t, std::vector<art::Ptr<recob::Hit> > > view_hits;

    //Get the clusters
    auto const clusHandle = Event.getValidHandle<std::vector<recob::Cluster> >(fPFParticleLabel);

    const art::FindManyP<recob::Cluster>& fmc = ShowerEleHolder.GetFindManyP<recob::Cluster>(
        pfpHandle, Event, fPFParticleLabel);
    // art::FindManyP<recob::Cluster> fmc(pfpHandle, Event, fPFParticleLabel);
    std::vector<art::Ptr<recob::Cluster> > clusters = fmc.at(pfparticle.key());

    //Get the hit association
    const art::FindManyP<recob::Hit>& fmhc = ShowerEleHolder.GetFindManyP<recob::Hit>(
        clusHandle, Event, fPFParticleLabel);
    // art::FindManyP<recob::Hit> fmhc(clusHandle, Event, fPFParticleLabel);

    std::vector<std::vector<art::Ptr<recob::Hit> > > trackHits;
    trackHits.resize(numPlanes);


    //Loop over the clusters in the plane and get the hits
    for(auto const& cluster: clusters){

      //Get the hits
      std::vector<art::Ptr<recob::Hit> > hits = fmhc.at(cluster.key());
      if(hits.empty()){
        if (fVerbose)
          mf::LogWarning("ShowerNumElectronsEnergy") << "No hit for the cluster. This suggest the find many is wrong."<< std::endl;
        continue;
      }

      //Get the view. geo::View_t view = cluster->View();
      geo::View_t view = cluster->View();
      //std::cout << "view = " << view << " hits = " << hits.size() << std::endl;

      view_hits[view].insert(view_hits[view].end(),hits.begin(),hits.end());

    }

    std::map<unsigned int, double > view_energies;
    std::vector<art::Ptr<recob::Hit>> hits;

    auto const clockData = art::ServiceHandle<detinfo::DetectorClocksService const>()->DataFor(Event);
    auto const detProp   = art::ServiceHandle<detinfo::DetectorPropertiesService const>()->DataFor(Event, clockData);

    //Accounting for events crossing the cathode.
    for(auto const& view_hit_iter: view_hits){

      hits = view_hit_iter.second;
      geo::View_t view = view_hit_iter.first;

      //Calculate the Energy
      Energy = CalculateEnergy(clockData, detProp, hits,view);
      //std::cout << "hits = " << hits.size() << std::endl;

      // Print out the energy for each plane
      if (fVerbose)
        std::cout<<"View: "<< view <<  " and energy: "<<Energy<<std::endl;;

      unsigned int viewNum = view;
      view_energies[viewNum] = Energy;

    }

    //TODO think of a better way of doing this
    for (unsigned int plane=0; plane<numPlanes; ++plane) {

      try{
        Energy = view_energies.at(plane);
        if (Energy<0){
          if (fVerbose)
            mf::LogWarning("ShowerNumElectronsEnergy") << "Negative shower energy: "<<Energy;
          Energy=-999;
        }
        if(plane == 2){
          n_hit_energy.push_back(std::make_tuple(subRunN ,EventN, showernum, hits.size(), Energy)); //save info for collection plane
        }
      }

      catch(...){
        if (fVerbose)
          mf::LogWarning("ShowerNumElectronsEnergy") <<"No energy calculation for plane "<<plane<<std::endl;
        // if there's no calculation, set the energy to -999.
        Energy = -999;
        if(plane == 2){
          n_hit_energy.push_back(std::make_tuple(subRunN, EventN, showernum, hits.size(), Energy)); //save info for collection plane
        }
      }
      ShowerNumElectronsEnergy.push_back(Energy);
    }

    if(ShowerNumElectronsEnergy.empty()){
      throw cet::exception("ShowerNumElectronsEnergy") << "Energy Vector is empty";
    }

    std::vector<double> EnergyError = {-999,-999,-999};

    ShowerEleHolder.SetElement(ShowerNumElectronsEnergy,EnergyError,"ShowerEnergy");


    bool write_to_file = false;
    // Make a .txt file with the subrun, event number, showernum, hits and energies from the plane
    // Useful info when making plots
    if(write_to_file){
      std::ofstream outfile;
      outfile.open("reco_hit_energy_vec.txt");
      for (auto i = n_hit_energy.begin(); i != n_hit_energy.end(); ++i){
        outfile << std::get<0>(*i) << "   " << std::get<1>(*i) << "   " << std::get<2>(*i) << "   " << std::get<3>(*i) << "   " << std::get<4>(*i) << std::endl;
      }
    }
    return 0;
  }

  // function to calculate the reco energy
  double ShowerNumElectronsEnergy::CalculateEnergy(const detinfo::DetectorClocksData& clockData,
          const detinfo::DetectorPropertiesData& detProp,
          std::vector<art::Ptr<recob::Hit> >& hits,
          geo::View_t& view){

    double totalCharge = 0;
    double totalEnergy = 0;
    double correctedtotalCharge = 0;
    double nElectrons = 0;

    for (auto const& hit :hits){
      totalCharge += hit->Integral() * fCalorimetryAlg.LifetimeCorrection(clockData, detProp, hit->PeakTime()); // obtain charge and correct for lifetime
    }

    // correct charge due to recombination
    correctedtotalCharge = totalCharge / fRecombinationFactor;
    // calculate # of electrons and the corresponding energy
    nElectrons = fCalorimetryAlg.ElectronsFromADCArea(correctedtotalCharge, view);
    totalEnergy = (nElectrons / util::kGeVToElectrons) * 1000; // energy in MeV
    return totalEnergy;
  }
}

DEFINE_ART_CLASS_TOOL(ShowerRecoTools::ShowerNumElectronsEnergy)
