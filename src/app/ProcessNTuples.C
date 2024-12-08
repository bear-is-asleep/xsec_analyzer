// Post-processing program for the MicroBooNE xsec_analyzer framework. This is
// currently designed for use with the PeLEE group"s "searchingfornues" ntuples
//
// Updated 24 September 2024
// Steven Gardiner <gardiner@fnal.gov>
// Daniel Barrow <daniel.barrow@physics.ox.ac.uk>

// Standard library includes
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// ROOT includes
#include "TChain.h"
#include "TFile.h"
#include "TBranch.h"
#include "TParameter.h"
#include "TTree.h"
#include "TVector3.h"

// XSecAnalyzer includes
#include "XSecAnalyzer/Event/AnalysisEventFactory.hh"
#include "XSecAnalyzer/Constants.hh"
#include "XSecAnalyzer/Functions.hh"

#include "XSecAnalyzer/Selections/SelectionBase.hh"
#include "XSecAnalyzer/Selections/SelectionFactory.hh"

void analyze( const std::vector< std::string >& in_file_names,
  const std::vector< std::string >& selection_names,
  const std::string& output_filename,
  const std::string& experiment)
{
  std::cout << "\nRunning ProcessNTuples with options:\n";
  std::cout << "\toutput_filename: " << output_filename << '\n';
  std::cout << "\tinput_file_names:\n";
  for ( size_t i = 0u; i < in_file_names.size(); ++i ) {
    std::cout << "\t\t- " << in_file_names[i] << '\n';
  }
  std::cout << "\n\nselection names:\n";
  for ( const auto& sel_name : selection_names ) {
    std::cout << "\t\t- " << sel_name << '\n';
  }
  std::cout << "\nExperiment: " << experiment << '\n';

  // Get the TTrees containing the event ntuples and subrun POT information
  // Use TChain objects for simplicity in manipulating multiple files

  //Initialize variables to be available outside of the if statements
  TChain events_ch;
  TChain subruns_ch;
  TChain syst_ch;
  TTree* out_tree;
  TFile* out_file = new TFile( output_filename.c_str(), "recreate" );
  float pot;
  float summed_pot = 0.;
  if (experiment == "uboone"){
    std::cout<< "Running for MicroBooNE" << std::endl;
    events_ch.SetName( "nuselection/NeutrinoSelectionFilter" );
    subruns_ch.SetName( "nuselection/SubRun" );
    
    for ( const auto& f_name : in_file_names ) {
      events_ch.Add( f_name.c_str() );
      subruns_ch.Add( f_name.c_str() );
    }

    // OUTPUT TTREE
    // Make an output TTree for plotting (one entry per event)
    std::cout<< "Creating output TTree" << std::endl;
    out_file->cd();
    std::cout<< "Creating output TTree2" << std::endl;
    out_tree = new TTree( "stv_tree", "STV analysis tree" );
    std::cout<< "Creating output TTree3" << std::endl;

    // Get the total POT from the subruns TTree. Save it in the output
    // TFile as a TParameter<float>. Real data doesn"t have this TTree,
    // so check that it exists first.
    bool has_pot_branch = ( subruns_ch.GetBranch("pot") != nullptr );
    if ( has_pot_branch ) {
      subruns_ch.SetBranchAddress( "pot", &pot );
      for ( int se = 0; se < subruns_ch.GetEntries(); ++se ) {
        subruns_ch.GetEntry( se );
        summed_pot += pot;
      }
    }
  }

  if (experiment == "sbnd"){
    events_ch.SetName( "recTree" );
    syst_ch.SetName( "globalTree" );

    for ( const auto& f_name : in_file_names ) {
      events_ch.Add( f_name.c_str() );
      syst_ch.Add( f_name.c_str() );
    }

    // OUTPUT TTREE
    // Make an output TTree for plotting (one entry per event)
    out_file->cd();
    out_tree = new TTree( "stv_tree", "STV analysis tree" );

    // Add syst_ch as a friend of events_ch
    syst_ch.AddFriend(&events_ch, "events_ch");

    // Get the total POT from the subruns TTree. Save it in the output
    // TFile as a TParameter<float>. Real data doesn"t have this TTree,
    // so check that it exists first.
    bool has_pot_branch = ( events_ch.GetBranch("TotalPOT") != nullptr );
    if ( has_pot_branch ) {
      events_ch.SetBranchAddress( "pot", &pot );
      for ( int se = 0; se < events_ch.GetEntries(); ++se ) {
        events_ch.GetEntry( se );
        summed_pot += pot;
      }
    }
  }


  TParameter<float>* summed_pot_param = new TParameter<float>( "summed_pot",
    summed_pot );

  summed_pot_param->Write();

  std::vector< std::unique_ptr<SelectionBase> > selections;

  SelectionFactory sf;
  for ( const auto& sel_name : selection_names ) {
    selections.emplace_back().reset( sf.CreateSelection(sel_name) );
  }

  std::cout<< "Setting up selections" << std::endl;
  out_file->cd();
  std::cout<< "Setting up selections2" << std::endl;
  for ( auto& sel : selections ) {
    sel->Setup( out_tree );
  }
  std::cout<< "Finished setting up selections" << std::endl;

  // EVENT LOOP
  // TChains can potentially be really big (and spread out over multiple
  // files). When that's the case, calling TChain::GetEntries() can be very
  // slow. I get around this by using a while loop instead of a for loop.
  bool created_output_branches = false;
  long events_entry = 0;

  while ( true ) {
    // Get entry number of systematics tree
    if (experiment == "sbnd"){
      syst_ch.GetEntry( events_entry );
    }
    //if ( events_entry > 1000) break;

    if ( events_entry % 1000 == 0 ) {
      std::cout << "Processing event #" << events_entry << '\n';
    }
    std::cout << "Processing event #" << events_entry << '\n';

    // Create a new AnalysisEvent object. This will reset all analysis
    // variables for the current event.
    std::unique_ptr<AnalysisEvent> cur_event = create_event(experiment);

    // Set branch addresses for the member variables that will be read
    // directly from the Event TTree.
    cur_event->set_event_branch_addresses( events_ch);

    // TChain::LoadTree() returns the entry number that should be used with
    // the current TTree object, which (together with the TBranch objects
    // that it owns) doesn"t know about the other TTrees in the TChain.
    // If the return value is negative, there was an I/O error, or we've
    // attempted to read past the end of the TChain.
    int local_entry = events_ch.LoadTree( events_entry );
    // If we've reached the end of the TChain (or encountered an I/O error),
    // then terminate the event loop
    if ( local_entry < 0 ){
      if (events_entry == 0) {
        std::cerr << "No entries found in input TTree. Exiting.\n";
        return;
      }
      break;
    }

    // Load all of the branches for which we've called
    // TChain::SetBranchAddress() above
    events_ch.GetEntry( events_entry );

    // Set the output TTree branch addresses, creating the branches if needed
    // (during the first event loop iteration)
    bool create_them = false;
    if ( !created_output_branches ) {
      create_them = true;
      created_output_branches = true;
    }
    std::cout<< "Setting output branch addresses" << std::endl;
    cur_event->set_event_output_branch_addresses(*out_tree, create_them );
    std::cout<< "Finished setting output branch addresses" << std::endl;

    for ( auto& sel : selections ) {
      sel->ApplySelection( &*cur_event );
    }
    std::cout<< "Finished applying selections" << std::endl;

    // We"re done. Save the results and move on to the next event.
    out_tree->Fill();
    ++events_entry;
  }

  for ( auto& sel : selections ) {
    sel->Summary();
  }
  std::cout << "Wrote output to:" << output_filename << std::endl;

  for ( auto& sel : selections ) {
    sel->FinalTasks();
  }

  std::cout << "Finished ProcessNTuples\n";

  out_tree->Write();
  out_file->Close();
  delete out_file;
  std::cout << "Closed output file\n";
}

void analyzer( const std::string& in_file_name,
 const std::vector< std::string > selection_names,
 const std::string& output_filename,
 const std::string& experiment )
{
  std::vector< std::string > in_files = { in_file_name };
  analyze( in_files, selection_names, output_filename, experiment );
}

int main( int argc, char* argv[] ) {

  if ( argc != 5 ) {
    std::cout << "Usage: " << argv[0]
      << " INPUT_PELEE_NTUPLE_FILE SELECTION_NAMES EXPERIMENT_NAME OUTPUT_FILE\n";
    return 1;
  }

  std::string input_file_name( argv[1] );
  std::string output_file_name( argv[4] );

  std::vector< std::string > selection_names;

  std::stringstream sel_ss( argv[2] );
  std::string sel_name;
  while ( std::getline(sel_ss, sel_name, ',') ) {
    selection_names.push_back( sel_name );
  }

  std::string experiment = argv[3];
  if ( experiment != "uboone" && experiment != "sbnd" ) {
    std::cerr << "Invalid experiment name: " << experiment << '\n';
    return 1;
  }

  analyzer( input_file_name, selection_names, output_file_name, experiment );
  std::cout << "Finished running analyzer\n";
  return 0;
}
