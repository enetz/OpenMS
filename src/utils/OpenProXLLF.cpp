
// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2017.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Eugen Netz $
// $Authors: Eugen Netz $
// --------------------------------------------------------------------------

#include <OpenMS/ANALYSIS/XLMS/OpenProXLUtils.h>
#include <OpenMS/KERNEL/StandardTypes.h>
#include <OpenMS/FORMAT/FASTAFile.h>
#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/FORMAT/ConsensusXMLFile.h>
#include <OpenMS/DATASTRUCTURES/ListUtilsIO.h>
#include <OpenMS/APPLICATIONS/TOPPBase.h>
#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/FORMAT/MzIdentMLFile.h>
#include <OpenMS/CHEMISTRY/EnzymaticDigestion.h>
#include <OpenMS/CHEMISTRY/EnzymesDB.h>
#include <OpenMS/CHEMISTRY/ModificationsDB.h>
#include <OpenMS/ANALYSIS/RNPXL/ModifiedPeptideGenerator.h>
#include <OpenMS/ANALYSIS/ID/IDMapper.h>
#include <OpenMS/ANALYSIS/ID/PeptideIndexing.h>


// TESTING SCORES
#include <OpenMS/ANALYSIS/RNPXL/HyperScore.h>
#include <OpenMS/ANALYSIS/RNPXL/PScore.h>

// preprocessing and filtering
#include <OpenMS/FILTERING/TRANSFORMERS/ThresholdMower.h>
#include <OpenMS/FILTERING/TRANSFORMERS/Normalizer.h>

#include <OpenMS/CHEMISTRY/TheoreticalSpectrumGenerator.h>
#include <OpenMS/CHEMISTRY/TheoreticalSpectrumGeneratorXLMS.h>

// results
#include <OpenMS/METADATA/ProteinIdentification.h>

#include <iostream>
#include <cmath>
#include <numeric>

using namespace std;
using namespace OpenMS;

#ifdef _OPENMP
#include <omp.h>
#define NUMBER_OF_THREADS (omp_get_num_threads())
#else
#define NUMBER_OF_THREADS (1)
#endif

/**
    @page UTILS_OpenProXLLF OpenProXLLF

    @brief Perform protein-protein cross-linking experiment search.

    <CENTER>
    <table>
        <tr>
            <td ALIGN = "center" BGCOLOR="#EBEBEB"> pot. predecessor tools </td>
            <td VALIGN="middle" ROWSPAN=2> \f$ \longrightarrow \f$ OpenProXLLF \f$ \longrightarrow \f$</td>
            <td ALIGN = "center" BGCOLOR="#EBEBEB"> pot. successor tools </td>
        </tr>
        <tr>
            <td VALIGN="middle" ALIGN = "center" ROWSPAN=1> - </td>
            <td VALIGN="middle" ALIGN = "center" ROWSPAN=1> - </td>
        </tr>
    </table>
</CENTER>

    <B>The command line parameters of this tool are:</B>
    @verbinclude UTILS_OpenProXLLF.cli
    <B>INI file documentation of this tool:</B>
    @htmlinclude UTILS_OpenProXLLF.html
*/

class TOPPOpenProXLLF :
  public TOPPBase
{
public:
  TOPPOpenProXLLF() :
    TOPPBase("OpenProXLLF", "Tool for protein-protein cross linking with label-free linkers.", false)
  {
  }

protected:
  void registerOptionsAndFlags_()
  {
    // input files
    registerInputFile_("in", "<file>", "", "Input file containing the spectra.");
    setValidFormats_("in", ListUtils::create<String>("mzML"));

    //registerInputFile_("consensus", "<file>", "", "Input file containing the linked mass peaks.");
    //setValidFormats_("consensus", ListUtils::create<String>("consensusXML"));

    registerInputFile_("database", "<file>", "", "Input file containing the protein database.");
    setValidFormats_("database", ListUtils::create<String>("fasta"));

    registerInputFile_("decoy_database", "<file>", "", "Input file containing the decoy protein database.", false);
    setValidFormats_("decoy_database", ListUtils::create<String>("fasta"));

    registerStringOption_("decoy_string", "<string>", "decoy", "String that was appended (or prefixed - see 'prefix' flag below) to the accessions in the protein database to indicate decoy proteins.", false);
    registerFlag_("decoy_prefix", "Set flag, if the decoy_string is a prefix of accessions in the protein database. Otherwise it is a suffix.");

    registerTOPPSubsection_("precursor", "Precursor (Parent Ion) Options");
    registerDoubleOption_("precursor:mass_tolerance", "<tolerance>", 10.0, "Width of precursor mass tolerance window", false);

    StringList precursor_mass_tolerance_unit_valid_strings;
    precursor_mass_tolerance_unit_valid_strings.push_back("ppm");
    precursor_mass_tolerance_unit_valid_strings.push_back("Da");

    registerStringOption_("precursor:mass_tolerance_unit", "<unit>", "ppm", "Unit of precursor mass tolerance.", false, false);
    setValidStrings_("precursor:mass_tolerance_unit", precursor_mass_tolerance_unit_valid_strings);

    registerIntOption_("precursor:min_charge", "<num>", 3, "Minimum precursor charge to be considered.", false, true);
    registerIntOption_("precursor:max_charge", "<num>", 7, "Maximum precursor charge to be considered.", false, true);

    registerTOPPSubsection_("fragment", "Fragments (Product Ion) Options");
    registerDoubleOption_("fragment:mass_tolerance", "<tolerance>", 3, "Fragment mass tolerance", false);
    registerDoubleOption_("fragment:mass_tolerance_xlinks", "<tolerance>", 3, "Fragment mass tolerance for cross-link ions", false);

    StringList fragment_mass_tolerance_unit_valid_strings;
    fragment_mass_tolerance_unit_valid_strings.push_back("ppm");
    fragment_mass_tolerance_unit_valid_strings.push_back("Da");

    registerStringOption_("fragment:mass_tolerance_unit", "<unit>", "ppm", "Unit of fragment m", false, false);
    setValidStrings_("fragment:mass_tolerance_unit", fragment_mass_tolerance_unit_valid_strings);

    registerTOPPSubsection_("modifications", "Modifications Options");
    vector<String> all_mods;
    ModificationsDB::getInstance()->getAllSearchModifications(all_mods);
    registerStringList_("modifications:fixed", "<mods>", ListUtils::create<String>(""), "Fixed modifications, specified using UniMod (www.unimod.org) terms, e.g. 'Carbamidomethyl (C)'", false);
    setValidStrings_("modifications:fixed", all_mods);
    registerStringList_("modifications:variable", "<mods>", ListUtils::create<String>(""), "Variable modifications, specified using UniMod (www.unimod.org) terms, e.g. 'Oxidation (M)'", false);
    setValidStrings_("modifications:variable", all_mods);
    registerIntOption_("modifications:variable_max_per_peptide", "<num>", 2, "Maximum number of residues carrying a variable modification per candidate peptide", false, false);

    registerTOPPSubsection_("peptide", "Peptide Options");
    registerIntOption_("peptide:min_size", "<num>", 5, "Minimum size a peptide must have after digestion to be considered in the search.", false, true);
    registerIntOption_("peptide:missed_cleavages", "<num>", 2, "Number of missed cleavages.", false, false);
    vector<String> all_enzymes;
    EnzymesDB::getInstance()->getAllNames(all_enzymes);
    registerStringOption_("peptide:enzyme", "<cleavage site>", "Trypsin", "The enzyme used for peptide digestion.", false);
    setValidStrings_("peptide:enzyme", all_enzymes);


    registerTOPPSubsection_("cross_linker", "Cross Linker Options");
    registerStringList_("cross_linker:residue1", "<one letter code>", ListUtils::create<String>("K"), "Comma separated residues, that the first side of a bifunctional cross-linker can attach to", false);
    registerStringList_("cross_linker:residue2", "<one letter code>", ListUtils::create<String>("K"), "Comma separated residues, that the second side of a bifunctional cross-linker can attach to", false);
    registerDoubleOption_("cross_linker:mass", "<mass>", 138.0680796, "Mass of the light cross-linker, linking two residues on one or two peptides", false);
    //registerDoubleOption_("cross_linker:mass_iso_shift", "<mass>", 12.075321, "Mass of the isotopic shift between the light and heavy linkers", false);
    registerDoubleList_("cross_linker:mass_mono_link", "<mass>", ListUtils::create<double>("156.07864431, 155.094628715"), "Possible masses of the linker, when attached to only one peptide", false);
    registerStringOption_("cross_linker:name", "<string>", "DSS" ,  "Name of the searched cross-link, used to resolve ambiguity of equal masses (e.g. DSS or BS3)", false);

    registerTOPPSubsection_("algorithm", "Algorithm Options");
    registerStringOption_("algorithm:candidate_search", "<param>", "enumeration", "Mode used to generate candidate peptides.", false, false);
    StringList candidate_search_modes_strings;
//    candidate_search_modes_strings.push_back("index");
    candidate_search_modes_strings.push_back("enumeration");
    setValidStrings_("algorithm:candidate_search", candidate_search_modes_strings);

    registerIntOption_("algorithm:number_top_hits", "<num>", 5, "Number of top hits reported for each spectrum pair", false, true);

    // output file
    registerOutputFile_("out_xquestxml", "<file>", "", "Results in the original xquest.xml format", false);
    setValidFormats_("out_xquestxml", ListUtils::create<String>("xml"));

    registerOutputFile_("out_idXML", "<file>", "", "Results in idXML format", false);
    setValidFormats_("out_idXML", ListUtils::create<String>("idXML"));

    registerOutputFile_("out_mzIdentML", "<file>","", "Results in mzIdentML (.mzid) format", false);
    setValidFormats_("out_mzIdentML", ListUtils::create<String>("mzid"));
  }

  vector<ResidueModification> getModifications_(StringList modNames)
  {
    vector<ResidueModification> modifications;

    // iterate over modification names and add to vector
    for (StringList::iterator mod_it = modNames.begin(); mod_it != modNames.end(); ++mod_it)
    {
      String modification(*mod_it);
      modifications.push_back(ModificationsDB::getInstance()->getModification(modification));
    }

    return modifications;
  }

protected:
  PeakMap preprocessSpectra_(PeakMap& exp)
  {
    Size peptide_min_size = getIntOption_("peptide:min_size") * 2; //  x2 because cross-links
    Int min_precursor_charge = getIntOption_("precursor:min_charge");
    Int max_precursor_charge = getIntOption_("precursor:max_charge");

    //String precursor_mass_tolerance_unit = getStringOption_("precursor:mass_tolerance_unit");
    String fragment_mass_tolerance_unit = getStringOption_("fragment:mass_tolerance_unit");
    bool fragment_mass_tolerance_ppm = fragment_mass_tolerance_unit == "ppm";
    //double fragment_mass_tolerance = getDoubleOption_("fragment:mass_tolerance");
    double fragment_mass_tolerance_xlinks = getDoubleOption_("fragment:mass_tolerance_xlinks");

    // filter MS2 map
    // remove 0 intensities
    ThresholdMower threshold_mower_filter;
    threshold_mower_filter.filterPeakMap(exp);

    Normalizer normalizer;
    normalizer.filterPeakMap(exp);

    // sort by rt
    exp.sortSpectra(false);
    LOG_DEBUG << "Deisotoping and filtering spectra." << endl;

    PeakMap deisotoped_spectra;

#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (SignedSize exp_index = 0; exp_index < static_cast<SignedSize>(exp.size()); ++exp_index)
    {
      vector<Precursor> precursor = exp[exp_index].getPrecursors();
      bool process_this_spectrum = false;
      if (precursor.size() == 1 && exp[exp_index].size() >= peptide_min_size*2)
      {
        int precursor_charge = precursor[0].getCharge();
        if (precursor_charge >= min_precursor_charge && precursor_charge <= max_precursor_charge)
        {
          process_this_spectrum = true;
        }
      }

      if (!process_this_spectrum)
      {
        continue;
      }
      exp[exp_index].sortByPosition();

      // params:                                                                                                                       (PeakSpectrum,  min_charge, max_charge,  fragment_tol, tol_unit_ppm,                              only_keep_deiso, min_iso_peaks, max_iso_peaks, make_single_charged);
      PeakSpectrum deisotoped = OpenProXLUtils::deisotopeAndSingleChargeMSSpectrum(exp[exp_index], 1,                 7,                    10,                 fragment_mass_tolerance_ppm, false,                    3,                      10,                     false);

      if (deisotoped.size() > peptide_min_size * 2)
      {
        OpenProXLUtils::nLargestSpectrumFilter(deisotoped, 500);
        deisotoped.sortByPosition();

#ifdef _OPENMP
#pragma omp critical
#endif
        deisotoped_spectra.addSpectrum(deisotoped);
      }
    }
    return deisotoped_spectra;
  }

  ExitCodes main_(int, const char**)
  {
    ProgressLogger progresslogger;
    progresslogger.setLogType(log_type_);

    const string in_mzml(getStringOption_("in"));
    const string in_fasta(getStringOption_("database"));
    const string in_decoy_fasta(getStringOption_("decoy_database"));
    //const string in_consensus(getStringOption_("consensus"));
    const string out_idXML(getStringOption_("out_idXML"));
    const string out_xquest = getStringOption_("out_xquestxml");
    const string out_mzIdentML = getStringOption_("out_mzIdentML");

    const bool decoy_prefix(getFlag_("decoy_prefix"));
    const string decoy_string(getStringOption_("decoy_string"));

    Int min_precursor_charge = getIntOption_("precursor:min_charge");
    Int max_precursor_charge = getIntOption_("precursor:max_charge");
    double precursor_mass_tolerance = getDoubleOption_("precursor:mass_tolerance");
    bool precursor_mass_tolerance_unit_ppm = (getStringOption_("precursor:mass_tolerance_unit") == "ppm");

    double fragment_mass_tolerance = getDoubleOption_("fragment:mass_tolerance");
    double fragment_mass_tolerance_xlinks = getDoubleOption_("fragment:mass_tolerance_xlinks");
    if (fragment_mass_tolerance_xlinks < fragment_mass_tolerance)
    {
      fragment_mass_tolerance_xlinks = fragment_mass_tolerance;
    }
    bool fragment_mass_tolerance_unit_ppm = (getStringOption_("fragment:mass_tolerance_unit") == "ppm");

    StringList cross_link_residue1 = getStringList_("cross_linker:residue1");
    StringList cross_link_residue2 = getStringList_("cross_linker:residue2");
    double cross_link_mass = getDoubleOption_("cross_linker:mass");
    //double cross_link_mass_iso_shift = getDoubleOption_("cross_linker:mass_iso_shift");
    DoubleList cross_link_mass_mono_link = getDoubleList_("cross_linker:mass_mono_link");
    String cross_link_name = getStringOption_("cross_linker:name");

    StringList fixedModNames = getStringList_("modifications:fixed");
    set<String> fixed_unique(fixedModNames.begin(), fixedModNames.end());

    Size peptide_min_size = getIntOption_("peptide:min_size");

    bool ion_index_mode = (getStringOption_("algorithm:candidate_search") == "index");
    Int number_top_hits = getIntOption_("algorithm:number_top_hits");

    if (fixed_unique.size() != fixedModNames.size())
    {
      LOG_DEBUG << "duplicate fixed modification provided." << endl;
      return ILLEGAL_PARAMETERS;
    }

    StringList varModNames = getStringList_("modifications:variable");
    set<String> var_unique(varModNames.begin(), varModNames.end());
    if (var_unique.size() != varModNames.size())
    {
      LOG_DEBUG << "duplicate variable modification provided." << endl;
      return ILLEGAL_PARAMETERS;
    }
    //TODO add crosslinker

    vector<ResidueModification> fixed_modifications = getModifications_(fixedModNames);
    vector<ResidueModification> variable_modifications = getModifications_(varModNames);
    Size max_variable_mods_per_peptide = getIntOption_("modifications:variable_max_per_peptide");

    // load MS2 map
    PeakMap unprocessed_spectra;
    MzMLFile f;
    f.setLogType(log_type_);

    PeakFileOptions options;
    options.clearMSLevels();
    options.addMSLevel(2);
    f.getOptions() = options;
    f.load(in_mzml, unprocessed_spectra);
    unprocessed_spectra.sortSpectra(true);

    // preprocess spectra (filter out 0 values, sort by position)
    progresslogger.startProgress(0, 1, "Filtering spectra...");
    PeakMap spectra = preprocessSpectra_(unprocessed_spectra);
    progresslogger.endProgress();

    // load linked features
    ConsensusMap cfeatures;
    ConsensusXMLFile cf;
    //cf.load(in_consensus, cfeatures);

    // load fasta database
    progresslogger.startProgress(0, 1, "Load database from FASTA file...");
    FASTAFile fastaFile;
    vector<FASTAFile::FASTAEntry> fasta_db;
    fastaFile.load(in_fasta, fasta_db);


    if (!in_decoy_fasta.empty())
    {
      vector<FASTAFile::FASTAEntry> fasta_decoys;
      fastaFile.load(in_decoy_fasta, fasta_decoys);
      fasta_db.reserve(fasta_db.size() + fasta_decoys.size());
      fasta_db.insert(fasta_db.end(), fasta_decoys.begin(), fasta_decoys.end());
    }

    progresslogger.endProgress();

    const Size missed_cleavages = getIntOption_("peptide:missed_cleavages");
    EnzymaticDigestion digestor;
    String enzyme_name = getStringOption_("peptide:enzyme");
    digestor.setEnzyme(enzyme_name);
    digestor.setMissedCleavages(missed_cleavages);

    // set minimum size of peptide after digestion
    Size min_peptide_length = getIntOption_("peptide:min_size");

    // one identification run
    vector<ProteinIdentification> protein_ids(1);
    protein_ids[0].setDateTime(DateTime::now());
    protein_ids[0].setSearchEngine("OpenXQuest");
    protein_ids[0].setSearchEngineVersion(VersionInfo::getVersion());
    protein_ids[0].setPrimaryMSRunPath(unprocessed_spectra.getPrimaryMSRunPath());
    protein_ids[0].setMetaValue("SpectrumIdentificationProtocol", DataValue("MS:1002494")); // cross-linking search = MS:1002494

    ProteinIdentification::SearchParameters search_params;
    search_params.charges = "2,3,4,5,6";
    search_params.db = in_fasta;
    search_params.digestion_enzyme = (*EnzymesDB::getInstance()->getEnzyme(enzyme_name));
    search_params.fixed_modifications = fixedModNames;
    search_params.variable_modifications = varModNames;
    search_params.mass_type = ProteinIdentification::MONOISOTOPIC;
    search_params.missed_cleavages = missed_cleavages;
    search_params.fragment_mass_tolerance = fragment_mass_tolerance;
    search_params.fragment_mass_tolerance_ppm =  fragment_mass_tolerance_unit_ppm;
    search_params.precursor_mass_tolerance = precursor_mass_tolerance;
    search_params.precursor_mass_tolerance_ppm = precursor_mass_tolerance_unit_ppm;

    // As MetaValues
    //search_params.setMetaValue("input_consensusXML", in_consensus);
    protein_ids[0].setMetaValue("input_mzML", in_mzml);
    //protein_ids[0].setMetaValue("input_database", in_fasta);

    search_params.setMetaValue("input_decoys", in_decoy_fasta);
    search_params.setMetaValue("decoy_prefix", decoy_prefix);
    search_params.setMetaValue("decoy_string", decoy_string);

    search_params.setMetaValue("precursor:min_charge", min_precursor_charge);
    search_params.setMetaValue("precursor:max_charge", max_precursor_charge);
    //protein_ids[0].setMetaValue("precursor:mass_tolerance", precursor_mass_tolerance);
    //protein_ids[0].setMetaValue("precursor:mass_tolerance_unit", precursor_mass_tolerance_unit_ppm ? "ppm" : "Da");

    search_params.setMetaValue("fragment:mass_tolerance_xlinks", fragment_mass_tolerance_xlinks);
    //protein_ids[0].setMetaValue("fragment:mass_tolerance", fragment_mass_tolerance);
    //protein_ids[0].setMetaValue("fragment:mass_tolerance_unit", fragment_mass_tolerance_unit_ppm ? "ppm" : "Da");

    search_params.setMetaValue("peptide:min_size", peptide_min_size);
    //protein_ids[0].setMetaValue("peptide:missed_cleavages", missed_cleavages);
    //protein_ids[0].setMetaValue("peptide:enzyme", enzyme_name);

    search_params.setMetaValue("cross_link:residue1", cross_link_residue1);
    search_params.setMetaValue("cross_link:residue2", cross_link_residue2);
    search_params.setMetaValue("cross_link:mass", cross_link_mass);
    //search_params.setMetaValue("cross_link:mass_isoshift", cross_link_mass_iso_shift);
    search_params.setMetaValue("cross_link:mass_monolink", cross_link_mass_mono_link);

    //protein_ids[0].setMetaValue("modifications:fixed", fixedModNames);
    //protein_ids[0].setMetaValue("modifications:variable", varModNames);
    search_params.setMetaValue("modifications:variable_max_per_peptide", max_variable_mods_per_peptide);

    search_params.setMetaValue("algorithm:candidate_search", ion_index_mode ? "ion-tag" : "enumeration");

    protein_ids[0].setSearchParameters(search_params);

    vector<PeptideIdentification> peptide_ids;

    // Determine if N-term and C-term modifications are possible with the used linker
    bool n_term_linker = false;
    bool c_term_linker = false;
    for (Size k = 0; k < cross_link_residue1.size(); k++)
    {
      if (cross_link_residue1[k] == "K")
      {
        n_term_linker = true;
      }
      if (cross_link_residue1[k] == "D")
      {
        c_term_linker = true;
      }
    }
    for (Size k = 0; k < cross_link_residue2.size(); k++)
    {
      if (cross_link_residue2[k] == "K")
      {
        n_term_linker = true;
      }
      if (cross_link_residue2[k] == "D")
      {
        c_term_linker = true;
      }
    }

    // lookup for processed peptides. must be defined outside of omp section and synchronized
    //multimap<StringView, AASequence> processed_peptides;
    vector<OpenProXLUtils::PeptideMass> peptide_masses;

    Size count_proteins = 0;
    Size count_peptides = 0;

    progresslogger.startProgress(0, 1, "Digesting peptides...");
    peptide_masses = OpenProXLUtils::digestDatabase(fasta_db, digestor, min_peptide_length, cross_link_residue1, cross_link_residue2, fixed_modifications,  variable_modifications, max_variable_mods_per_peptide, count_proteins, count_peptides, n_term_linker, c_term_linker);
    progresslogger.endProgress();

    // create spectrum generator
    TheoreticalSpectrumGeneratorXLMS specGen;

    // Setting parameters for cross-link fragmentation
    Param specGenParams = specGen.getParameters();
    specGenParams.setValue("add_isotopes", "true", "If set to 1 isotope peaks of the product ion peaks are added");
    specGenParams.setValue("max_isotope", 2, "Defines the maximal isotopic peak which is added, add_isotopes must be set to 1");
    specGenParams.setValue("add_losses", "false", "Adds common losses to those ion expect to have them, only water and ammonia loss is considered");
    specGenParams.setValue("add_precursor_peaks", "false", "Adds peaks of the precursor to the spectrum, which happen to occur sometimes");
    specGenParams.setValue("add_abundant_immonium_ions", "false", "Add most abundant immonium ions");
    specGenParams.setValue("add_first_prefix_ion", "true", "If set to true e.g. b1 ions are added");
    specGenParams.setValue("add_y_ions", "true", "Add peaks of y-ions to the spectrum");
    specGenParams.setValue("add_b_ions", "true", "Add peaks of b-ions to the spectrum");
    specGenParams.setValue("add_a_ions", "false", "Add peaks of a-ions to the spectrum");
    specGenParams.setValue("add_c_ions", "false", "Add peaks of c-ions to the spectrum");
    specGenParams.setValue("add_x_ions", "false", "Add peaks of  x-ions to the spectrum");
    specGenParams.setValue("add_z_ions", "false", "Add peaks of z-ions to the spectrum");
    // TODO does nothing yet
    specGenParams.setValue("multiple_fragmentation_mode" , "false", "If set to true, multiple fragmentation events on the same cross-linked peptide pair are considered (HCD fragmentation)");
    specGen.setParameters(specGenParams);

    // TODO constant binsize for HashGrid computation
    double tolerance_binsize = 0.2;

    LOG_DEBUG << "Peptide candidates: " << peptide_masses.size() << endl;
    search_params = protein_ids[0].getSearchParameters();
    search_params.setMetaValue("MS:1001029", peptide_masses.size()); // number of sequences searched = MS:1001029
    protein_ids[0].setSearchParameters(search_params);

    vector<OpenProXLUtils::XLPrecursor> enumerated_cross_link_masses;

    // Collect precursor MZs for filtering enumerated peptide pairs
    vector< double > spectrum_precursors;
    for (Size i = 0; i < spectra.size(); i++)
    {
      double current_precursor_mz = spectra[i].getPrecursors()[0].getMZ();
      double current_precursor_charge = spectra[i].getPrecursors()[0].getCharge();
      double current_precursor_mass = (current_precursor_mz * current_precursor_charge) - (current_precursor_charge * Constants::PROTON_MASS_U);
      spectrum_precursors.push_back(current_precursor_mass);
    }
    sort(spectrum_precursors.begin(), spectrum_precursors.end());
    cout << "Number of precursor masses in the spectra: " << spectrum_precursors.size() << endl;

    sort(peptide_masses.begin(), peptide_masses.end());
    // The largest peptides given a fixed maximal precursor mass are possible with loop links
    // Filter peptides using maximal loop link mass first
    double max_precursor_mass = spectrum_precursors[spectrum_precursors.size()-1];

    // compute absolute tolerance from relative, if necessary
    double max_peptide_allowed_error = 0;
    if (precursor_mass_tolerance_unit_ppm) // ppm
    {
      max_peptide_allowed_error = max_precursor_mass * precursor_mass_tolerance * 1e-6;
    }
    else // Dalton
    {
      max_peptide_allowed_error= precursor_mass_tolerance;
    }

    double max_peptide_mass = max_precursor_mass - cross_link_mass + max_peptide_allowed_error;

    cout << "Filtering peptides with precursors" << endl;

    // search for the first mass greater than the maximim, cut off everything larger
    vector<OpenProXLUtils::PeptideMass>::iterator last = upper_bound(peptide_masses.begin(), peptide_masses.end(), max_peptide_mass);
    peptide_masses.assign(peptide_masses.begin(), last);

    progresslogger.startProgress(0, 1, "Enumerating cross-links...");
    enumerated_cross_link_masses = OpenProXLUtils::enumerateCrossLinksAndMasses_(peptide_masses, cross_link_mass, cross_link_mass_mono_link, cross_link_residue1, cross_link_residue2,
                                                                                                                                                  spectrum_precursors, precursor_mass_tolerance, precursor_mass_tolerance_unit_ppm);
    progresslogger.endProgress();
    cout << "Enumerated cross-links: " << enumerated_cross_link_masses.size() << endl;
    sort(enumerated_cross_link_masses.begin(), enumerated_cross_link_masses.end());
    cout << "Sorting of enumerated precursors finished" << endl;


    // for PScore, precompute ranks
    vector<vector<Size> > rankMap = PScore::calculateRankMap(spectra);

    // TODO test variables, can be removed, or set to be used in debug mode?
    double pScoreMax = 0;
    double TICMax = 0;
    double wTICMax = 0;
    double intsumMax = 0;
    double matchOddsMax = 0;
    double xcorrxMax = 0;
    double xcorrcMax = 0;
    double maxMatchCount = 0;
    double sumMatchCount = 0;

    // iterate over all spectra
    progresslogger.startProgress(0, 1, "Matching to theoretical spectra and scoring...");
    vector< vector< CrossLinkSpectrumMatch > > all_top_csms;

    Size spectrum_counter = 0;

    cout << "Spectra left after preprocessing and filtering: " << spectra.size() << " of " << unprocessed_spectra.size() << endl;

// Multithreading options: schedule: static, dynamic, guided with chunk size
#ifdef _OPENMP
#pragma omp parallel for schedule(guided)
#endif
    for (SignedSize scan_index = 0; scan_index < static_cast<SignedSize>(spectra.size()); ++scan_index)
    {
      const PeakSpectrum& spectrum = spectra[scan_index];

      // TODO probably not necessary, if this is appropriately done in preprocessing
      if ( spectrum.size() < peptide_min_size )
      {
        continue;
      }

      LOG_DEBUG << "################## NEW SPECTRUM ##############################" << endl;
      //LOG_DEBUG << "Scan index: " << scan_index << "\tSpectrum native ID: " << spectrum.getNativeID()  << endl;

#ifdef _OPENMP
#pragma omp critical
#endif
      {
        spectrum_counter++;
        cout << "Processing spectrum " << spectrum_counter << " / " << spectra.size() << "\tSpectrum ID: " << spectrum.getNativeID()  << endl;
      }

      const double precursor_charge = spectrum.getPrecursors()[0].getCharge();
      const double precursor_mz = spectrum.getPrecursors()[0].getMZ();
      const double precursor_mass = (precursor_mz * static_cast<double>(precursor_charge)) - (static_cast<double>(precursor_charge) * Constants::PROTON_MASS_U);

      // needed farther down in the scoring, but only needs to be computed once for a spectrum
      vector< double > aucorrx = OpenProXLUtils::xCorrelation(spectrum, spectrum, 5, 0.3);
      vector< double > aucorrc = OpenProXLUtils::xCorrelation(spectrum, spectrum, 5, 0.2);

      const PeakSpectrum& common_peaks = spectra[scan_index];

      vector< CrossLinkSpectrumMatch > top_csms_spectrum;


      // determine candidates
      vector< OpenProXLUtils::XLPrecursor > candidates;
      double allowed_error = 0;

      //LOG_DEBUG << "Number of common peaks, xlink peaks: " << preprocessed_pair_spectra.spectra_common_peaks[scan_index].size() << "\t" << preprocessed_pair_spectra.spectra_xlink_peaks[scan_index].size();

      // determine MS2 precursors that match to the current peptide mass
      vector< OpenProXLUtils::XLPrecursor >::const_iterator low_it;
      vector< OpenProXLUtils::XLPrecursor >::const_iterator up_it;

      if (precursor_mass_tolerance_unit_ppm) // ppm
      {
        allowed_error = precursor_mass * precursor_mass_tolerance * 1e-6;
      }
      else // Dalton
      {
        allowed_error = precursor_mass_tolerance;
      }
#ifdef _OPENMP
#pragma omp critical (enumerated_cross_link_masses_access)
#endif
      {
        low_it = lower_bound(enumerated_cross_link_masses.begin(), enumerated_cross_link_masses.end(), precursor_mass - allowed_error);
        up_it =  upper_bound(enumerated_cross_link_masses.begin(), enumerated_cross_link_masses.end(), precursor_mass + allowed_error);
      }

      if (low_it != up_it) // no matching precursor in data
      {
        for (; low_it != up_it; ++low_it)
        {
          candidates.push_back(*low_it);
        }
      }

#ifdef _OPENMP
#pragma omp critical
#endif
      cout << "Number of candidates for this spectrum: " << candidates.size() << endl;

      // Find all positions of lysine (K) in the peptides (possible scross-linking sites), create cross_link_candidates with all combinations
      vector <ProteinProteinCrossLink> cross_link_candidates = OpenProXLUtils::buildCandidates(candidates, peptide_masses, cross_link_residue1, cross_link_residue2, cross_link_mass, cross_link_mass_mono_link, precursor_mass, allowed_error, cross_link_name, n_term_linker, c_term_linker);

      // lists for one spectrum, to determine best match to the spectrum
      vector< CrossLinkSpectrumMatch > all_csms_spectrum;

      // TODO variables for benchmarking and testing purposes
#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
      {
        if (cross_link_candidates.size() > maxMatchCount) maxMatchCount = cross_link_candidates.size();
        sumMatchCount += cross_link_candidates.size();
      }

      for (Size i = 0; i != cross_link_candidates.size(); ++i)
      {
        ProteinProteinCrossLink cross_link_candidate = cross_link_candidates[i];
        double candidate_mz = (cross_link_candidate.alpha.getMonoWeight() + cross_link_candidate.beta.getMonoWeight() +  cross_link_candidate.cross_linker_mass+ (static_cast<double>(precursor_charge) * Constants::PROTON_MASS_U)) / precursor_charge;

        LOG_DEBUG << "Pair: " << cross_link_candidate.alpha.toString() << "-" << cross_link_candidate.beta.toString() << " matched to light spectrum " << scan_index << "\t and heavy spectrum " << scan_index
            << " with m/z: " << precursor_mz << "\t" << "and candidate m/z: " << candidate_mz << "\tK Positions: " << cross_link_candidate.cross_link_position.first << "\t" << cross_link_candidate.cross_link_position.second << endl;
//        LOG_DEBUG << a->second.getMonoWeight() << ", " << b->second.getMonoWeight() << " cross_link_mass: " <<  cross_link_mass <<  endl;

	CrossLinkSpectrumMatch csm;
	csm.cross_link = cross_link_candidate;

	PeakSpectrum theoretical_spec_common_alpha;
	PeakSpectrum theoretical_spec_common_beta;
	PeakSpectrum theoretical_spec_xlinks_alpha;
	PeakSpectrum theoretical_spec_xlinks_beta;

	bool type_is_cross_link = cross_link_candidate.getType() == ProteinProteinCrossLink::CROSS;
	bool type_is_loop = cross_link_candidate.getType() == ProteinProteinCrossLink::LOOP;
	Size link_pos_B = 0;
	if (type_is_loop)
	{
	  link_pos_B = cross_link_candidate.cross_link_position.second;
	}

//        specGen.getCommonIonSpectrum(theoretical_spec_common_alpha, cross_link_candidate, 2, true);
        specGen.getCommonIonSpectrum(theoretical_spec_common_alpha, cross_link_candidate.alpha, cross_link_candidate.cross_link_position.first, true, 2, link_pos_B);
        if (type_is_cross_link)
        {
//          specGen.getCommonIonSpectrum(theoretical_spec_common_beta, cross_link_candidate, 2, false);
//          specGen.getXLinkIonSpectrum(theoretical_spec_xlinks_alpha, theoretical_spec_xlinks_beta, cross_link_candidate, 2, precursor_charge);
          specGen.getCommonIonSpectrum(theoretical_spec_common_beta, cross_link_candidate.beta, cross_link_candidate.cross_link_position.second, false, 2);
          specGen.getXLinkIonSpectrum(theoretical_spec_xlinks_alpha, cross_link_candidate.alpha, cross_link_candidate.cross_link_position.first, precursor_mass, true, 1, precursor_charge);
          specGen.getXLinkIonSpectrum(theoretical_spec_xlinks_beta, cross_link_candidate.beta, cross_link_candidate.cross_link_position.second, precursor_mass, false, 1, precursor_charge);
        } else
        {
          // Function for mono-links or loop-links
//          specGen.getXLinkIonSpectrum(theoretical_spec_xlinks_alpha, cross_link_candidate, 2, precursor_charge);
          specGen.getXLinkIonSpectrum(theoretical_spec_xlinks_alpha, cross_link_candidate.alpha, cross_link_candidate.cross_link_position.first, precursor_mass, true, 2, precursor_charge, link_pos_B);
        }

        // Something like this can happen, e.g. with a loop link connecting the first and last residue of a peptide
        if ( (theoretical_spec_common_alpha.size() < 1) || (theoretical_spec_xlinks_alpha.size() < 1) )
        {
          continue;
        }

        vector< pair< Size, Size > > matched_spec_common_alpha;
        vector< pair< Size, Size > > matched_spec_common_beta;
        vector< pair< Size, Size > > matched_spec_xlinks_alpha;
        vector< pair< Size, Size > > matched_spec_xlinks_beta;

        OpenProXLUtils::getSpectrumAlignment(matched_spec_common_alpha, theoretical_spec_common_alpha, spectrum, fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm);
        OpenProXLUtils::getSpectrumAlignment(matched_spec_common_beta, theoretical_spec_common_beta, spectrum, fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm);
        OpenProXLUtils::getSpectrumAlignment(matched_spec_xlinks_alpha, theoretical_spec_xlinks_alpha, spectrum, fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm);
        OpenProXLUtils::getSpectrumAlignment(matched_spec_xlinks_beta, theoretical_spec_xlinks_beta, spectrum, fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm);

        LOG_DEBUG << "Spectrum sizes: " << spectrum.size() << " || " << theoretical_spec_common_alpha.size() <<  " | " << theoretical_spec_common_beta.size()
                              <<  " | " << theoretical_spec_xlinks_alpha.size() <<  " | " << theoretical_spec_xlinks_beta.size() << endl;
        LOG_DEBUG << "Matched peaks: " << matched_spec_common_alpha.size() << " | " << matched_spec_common_beta.size()
                              <<  " | " << matched_spec_xlinks_alpha.size() <<  " | " << matched_spec_xlinks_beta.size() << endl;

        // Pre-Score calculations
        Size matched_alpha_count = matched_spec_common_alpha.size() + matched_spec_xlinks_alpha.size();
        Size theor_alpha_count = theoretical_spec_common_alpha.size() + theoretical_spec_xlinks_alpha.size();
        Size matched_beta_count = matched_spec_common_beta.size() + matched_spec_xlinks_beta.size();
        Size theor_beta_count = theoretical_spec_common_beta.size() + theoretical_spec_xlinks_beta.size();

        if (matched_alpha_count + matched_beta_count < 1)
        {
          continue;
        }
        // Simplified pre-Score
        //float pre_score = preScore(matched_fragments_theor_spec.size(), theoretical_spec.size());
        double pre_score = 0;
        if (type_is_cross_link)
        {
          pre_score = OpenProXLUtils::preScore(matched_alpha_count, theor_alpha_count, matched_beta_count, theor_beta_count);
        }
        else
        {
          pre_score = OpenProXLUtils::preScore(matched_alpha_count, theor_alpha_count);
        }
        //LOG_DEBUG << "Number of matched peaks to theor. spectrum: " << matched_alpha_count << "\t" << matched_beta_count << endl;
        //LOG_DEBUG << "Number of theoretical ions: " << theor_alpha_count << "\t" << theor_beta_count << endl;
        //LOG_DEBUG << "Pre Score: " << pre_score << endl;
        //LOG_DEBUG << "Peptide size: " << a->second.size() << "\t" << b->second.size() << "\t" << "K Pos:" << K_pos_a[i] << "\t" << K_pos_b[i] << endl;

#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
        if (pre_score > pScoreMax) pScoreMax = pre_score;

        // compute intsum score
        double intsum = OpenProXLUtils::total_matched_current(matched_spec_common_alpha, matched_spec_common_beta, matched_spec_xlinks_alpha, matched_spec_xlinks_beta, spectrum, spectrum);

        // Total ion intensity of light spectrum
        // sum over common and xlink ion spectra instead of unfiltered
        double total_current = 0;
        for (SignedSize j = 0; j < static_cast<SignedSize>(spectrum.size()); ++j)
        {
          total_current += spectrum[j].getIntensity();
        }
        double TIC = intsum / total_current;

#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
        if (TIC > TICMax) TICMax = TIC;

        // TIC_alpha and _beta
        double intsum_alpha = OpenProXLUtils::matched_current_chain(matched_spec_common_alpha, matched_spec_xlinks_alpha, spectrum, spectrum);
        double intsum_beta = 0;
        if (type_is_cross_link)
        {
          intsum_beta = OpenProXLUtils::matched_current_chain(matched_spec_common_beta, matched_spec_xlinks_beta, spectrum, spectrum);
        }

        // normalize TIC_alpha and  _beta
        if ((intsum_alpha + intsum_beta) > 0.0)
        {
          intsum_alpha = intsum_alpha * intsum / (intsum_alpha + intsum_beta);
          intsum_beta = intsum_beta *  intsum / (intsum_alpha + intsum_beta);
        }

        // compute wTIC
        double wTIC = OpenProXLUtils::weighted_TIC_score(cross_link_candidate.alpha.size(), cross_link_candidate.beta.size(), intsum_alpha, intsum_beta, intsum, total_current, type_is_cross_link);

#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
        {
          if (wTIC > wTICMax) wTICMax = wTIC;
          if (intsum > intsumMax) intsumMax = intsum;
        }

        // maximal xlink ion charge = (Precursor charge - 1), minimal xlink ion charge: 2
        Size n_xlink_charges = (precursor_charge - 1) - 2;
        if (n_xlink_charges < 1) n_xlink_charges = 1;

        // compute match odds (unweighted), the 3 is the number of charge states in the theoretical spectra
        double match_odds_c_alpha = OpenProXLUtils::match_odds_score(theoretical_spec_common_alpha, matched_spec_common_alpha, fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, false);
        double match_odds_x_alpha = OpenProXLUtils::match_odds_score(theoretical_spec_xlinks_alpha, matched_spec_xlinks_alpha, fragment_mass_tolerance_xlinks , fragment_mass_tolerance_unit_ppm, true, n_xlink_charges);
        double match_odds = 0;
        //cout << "TEST Match_odds_c_alpha: " << match_odds_c_alpha << "\t x_alpha: " << match_odds_x_alpha << endl;
        if (type_is_cross_link)
        {
          double match_odds_c_beta = OpenProXLUtils::match_odds_score(theoretical_spec_common_beta, matched_spec_common_beta, fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, false);
          double match_odds_x_beta = OpenProXLUtils::match_odds_score(theoretical_spec_xlinks_beta, matched_spec_xlinks_beta, fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm, true, n_xlink_charges);
          match_odds = (match_odds_c_alpha + match_odds_x_alpha + match_odds_c_beta + match_odds_x_beta) / 4;
          //cout << "TEST Match_odds_c_beta: " << match_odds_c_beta << "\t x_beta: " << match_odds_x_beta<< endl;
        }
        else
        {
          match_odds = (match_odds_c_alpha + match_odds_x_alpha) / 2;
        }

#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
        if (match_odds > matchOddsMax) matchOddsMax = match_odds;

        //Cross-correlation
        PeakSpectrum theoretical_spec_common;
        PeakSpectrum theoretical_spec_xlinks;

        if (type_is_cross_link)
        {
          theoretical_spec_common = OpenProXLUtils::mergeAnnotatedSpectra(theoretical_spec_common_alpha, theoretical_spec_common_beta);
          theoretical_spec_xlinks = OpenProXLUtils::mergeAnnotatedSpectra(theoretical_spec_xlinks_alpha, theoretical_spec_xlinks_beta);
        }
        else
        {
          theoretical_spec_common = theoretical_spec_common_alpha;
          theoretical_spec_xlinks = theoretical_spec_xlinks_alpha;
        }

        PeakSpectrum theoretical_spec = OpenProXLUtils::mergeAnnotatedSpectra(theoretical_spec_common, theoretical_spec_xlinks);
        PeakSpectrum theoretical_spec_alpha = OpenProXLUtils::mergeAnnotatedSpectra(theoretical_spec_common_alpha, theoretical_spec_xlinks_alpha);
        PeakSpectrum theoretical_spec_beta;
        if (type_is_cross_link)
        {
          theoretical_spec_beta = OpenProXLUtils::mergeAnnotatedSpectra(theoretical_spec_common_beta, theoretical_spec_xlinks_beta);
        }
        vector< double > xcorrx = OpenProXLUtils::xCorrelation(spectrum, theoretical_spec_xlinks, 5, 0.3);
        vector< double > xcorrc = OpenProXLUtils::xCorrelation(spectrum, theoretical_spec_common, 5, 0.2);

          // TODO save time: only needs to be done once per light spectrum, here it is repeated for cross-link each candidate
//        vector< double > aucorrx = OpenProXLUtils::xCorrelation(spectrum, spectrum, 5, 0.3);
//        vector< double > aucorrc = OpenProXLUtils::xCorrelation(spectrum, spectrum, 5, 0.2);
        double aucorr_sumx = accumulate(aucorrx.begin(), aucorrx.end(), 0.0);
        double aucorr_sumc = accumulate(aucorrc.begin(), aucorrc.end(), 0.0);
        double xcorrx_max = accumulate(xcorrx.begin(), xcorrx.end(), 0.0) / aucorr_sumx;
        double xcorrc_max = accumulate(xcorrc.begin(), xcorrc.end(), 0.0) / aucorr_sumc;
//          LOG_DEBUG << "xCorrelation X: " << xcorrx << endl;
//          LOG_DEBUG << "xCorrelation C: " << xcorrc << endl;
//          LOG_DEBUG << "Autocorr: " << aucorr << "\t Autocorr_sum: " << aucorr_sum << "\t xcorrx_max: " << xcorrx_max << "\t xcorrc_max: " << xcorrc_max << endl;

//########################### TESTING SCORES ##############################################

        map<Size, PeakSpectrum> peak_level_spectra = PScore::calculatePeakLevelSpectra(spectrum, rankMap[scan_index]);
        csm.PScoreCommon = PScore::computePScore(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, peak_level_spectra, theoretical_spec_common);
        csm.PScoreXlink = PScore::computePScore(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, peak_level_spectra, theoretical_spec_xlinks);
        csm.PScoreBoth = PScore::computePScore(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, peak_level_spectra, theoretical_spec);
        csm.PScoreAlpha = PScore::computePScore(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, peak_level_spectra, theoretical_spec_alpha);
        csm.PScoreBeta = PScore::computePScore(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, peak_level_spectra, theoretical_spec_beta);

        csm.HyperCommon = HyperScore::compute(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, spectrum, theoretical_spec_common);
        csm.HyperAlpha = HyperScore::compute(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, spectrum, theoretical_spec_alpha);
        csm.HyperBeta = HyperScore::compute(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, spectrum, theoretical_spec_beta);
        csm.HyperXlink = HyperScore::compute(fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm, spectrum, theoretical_spec_xlinks);
        csm.HyperBoth = HyperScore::compute(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, spectrum, theoretical_spec);




//########################### END TESTING SCORES ###########################################



#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
        {
          if (xcorrx_max > xcorrxMax) xcorrxMax = xcorrx_max;
          if (xcorrc_max > xcorrcMax) xcorrcMax = xcorrc_max;
        }

        // Compute score from the 4 scores and 4 weights
        double xcorrx_weight = 2.488;
        double xcorrc_weight = 21.279;
        //double match_odds_weight = 1.973;
        // TODO match-.odds score does not work for high res data, very low tolerances lead to low probabilities for random matches and the maximal score is reached very often
        double match_odds_weight = 0.1;
        double wTIC_weight = 12.829;
        double intsum_weight = 1.8;

        double score = xcorrx_weight * xcorrx_max + xcorrc_weight * xcorrc_max + match_odds_weight * match_odds + wTIC_weight * wTIC + intsum_weight * intsum;

        csm.score = score;
        csm.pre_score = pre_score;
        csm.percTIC = TIC;
        csm.wTIC = wTIC;
        csm.int_sum = intsum;
        csm.match_odds = match_odds;
//        csm.xcorrx = xcorrx;
        csm.xcorrx_max = xcorrx_max;
//        csm.xcorrc = xcorrc;
        csm.xcorrc_max = xcorrc_max;
        csm.matched_common_alpha = matched_spec_common_alpha.size();
        csm.matched_common_beta = matched_spec_common_beta.size();
        csm.matched_xlink_alpha = matched_spec_xlinks_alpha.size();
        csm.matched_xlink_beta = matched_spec_xlinks_beta.size();
        csm.scan_index_light = scan_index;
        csm.scan_index_heavy = -1;


        // write fragment annotations
        LOG_DEBUG << "Start writing annotations" << endl;
        vector<PeptideHit::FragmentAnnotation> frag_annotations;

        OpenProXLUtils::buildFragmentAnnotations(frag_annotations, matched_spec_common_alpha, theoretical_spec_common_alpha, spectrum);
        OpenProXLUtils::buildFragmentAnnotations(frag_annotations, matched_spec_common_beta, theoretical_spec_common_beta, spectrum);
        OpenProXLUtils::buildFragmentAnnotations(frag_annotations, matched_spec_xlinks_alpha, theoretical_spec_xlinks_alpha, spectrum);
        OpenProXLUtils::buildFragmentAnnotations(frag_annotations, matched_spec_xlinks_beta, theoretical_spec_xlinks_beta, spectrum);
        LOG_DEBUG << "End writing annotations, size: " << frag_annotations.size() << endl;

        // make annotations unique
        sort(frag_annotations.begin(), frag_annotations.end());
        vector<PeptideHit::FragmentAnnotation>::iterator last_unique_anno = unique(frag_annotations.begin(), frag_annotations.end());
        if (last_unique_anno != frag_annotations.end())
        {
//          LOG_DEBUG << "uniqiifying: " << endl;
//          for (vector<PeptideHit::FragmentAnnotation>::iterator double_frag = last_unique_anno; double_frag != frag_annotations.end(); ++double_frag)
//          {
//            LOG_DEBUG << "anno: " << double_frag->annotation << "\tcharge: " << double_frag->charge << "\tmz: " << double_frag->mz << "\tint: " << double_frag->intensity << endl;
//          }
          frag_annotations.erase(last_unique_anno, frag_annotations.end());
//          LOG_DEBUG << "Fragment annotations were uniquified, new size: " << frag_annotations.size() << endl;
        }

        csm.frag_annotations = frag_annotations;

        all_csms_spectrum.push_back(csm);
      } // candidates for peak finished, determine best matching candidate

      Int top = 0;

      // collect top n matches to spectrum
      while(!all_csms_spectrum.empty() && top < number_top_hits)
      {
        top++;
        //double max_score = *max_element(candidate_score.begin(), candidate_score.end());
        Int max_position = distance(all_csms_spectrum.begin(), max_element(all_csms_spectrum.begin(), all_csms_spectrum.end()));
        all_csms_spectrum[max_position].rank = top;
        top_csms_spectrum.push_back(all_csms_spectrum[max_position]);
        all_csms_spectrum.erase(all_csms_spectrum.begin() + max_position);

        LOG_DEBUG << "Score: " << all_csms_spectrum[max_position].score << "\t wTIC: " << all_csms_spectrum[max_position].wTIC << "\t xcorrx: " << all_csms_spectrum[max_position].xcorrx_max
                << "\t xcorrc: " << all_csms_spectrum[max_position].xcorrc_max << "\t match-odds: " << all_csms_spectrum[max_position].match_odds << "\t Intsum: " << all_csms_spectrum[max_position].int_sum << endl;

        if (all_csms_spectrum[max_position].cross_link.getType() == ProteinProteinCrossLink::CROSS)
        {
          LOG_DEBUG << "Matched ions calpha , cbeta , xalpha , xbeta" << "\t" << all_csms_spectrum[max_position].matched_common_alpha << "\t" << all_csms_spectrum[max_position].matched_common_beta
                  << "\t" << all_csms_spectrum[max_position].matched_xlink_alpha <<  "\t" << all_csms_spectrum[max_position].matched_xlink_beta << endl;
        }
        else
        {
          LOG_DEBUG << "Matched ions common, cross-links " << all_csms_spectrum[max_position].matched_common_alpha << "\t" << all_csms_spectrum[max_position].matched_xlink_alpha << endl;
        }
      }

      Size all_top_csms_current_index = 0;
#ifdef _OPENMP
#pragma omp critical (all_top_csms_access)
#endif
      {
        all_top_csms.push_back(top_csms_spectrum);
        all_top_csms_current_index = all_top_csms.size()-1;
      }

      // Write PeptideIdentifications and PeptideHits for n top hits
      OpenProXLUtils::buildPeptideIDs(peptide_ids, top_csms_spectrum, all_top_csms, all_top_csms_current_index, spectra, scan_index, scan_index);

      LOG_DEBUG << "Next Spectrum ################################## \n";
    }

    // end of matching / scoring
    progresslogger.endProgress();

    LOG_DEBUG << "Pre Score maximum: " << pScoreMax << "\t TIC maximum: " << TICMax << "\t wTIC maximum: " << wTICMax << "\t Match-Odds maximum: " << matchOddsMax << endl;
    LOG_DEBUG << "XLink Cross-correlation maximum: " << xcorrxMax << "\t Common Cross-correlation maximum: " << xcorrcMax << "\t Intsum maximum: " << intsumMax << endl;
    LOG_DEBUG << "Total number of matched candidates: " << sumMatchCount << "\t Maximum number of matched candidates to one spectrum pair: " << maxMatchCount << "\t Average: " << sumMatchCount / spectra.size() << endl;

    // Add protein identifications
    PeptideIndexing pep_indexing;
    Param indexing_param = pep_indexing.getParameters();

    // TODO update additional parameters of PeptideIndexing (enzyme etc.)
    String d_prefix = decoy_prefix ? "true" : "false";
    indexing_param.setValue("prefix", d_prefix, "If set, protein accessions in the database contain 'decoy_string' as prefix.");
    indexing_param.setValue("decoy_string", decoy_string, "String that was appended (or prefixed - see 'prefix' flag below) to the accessions in the protein database to indicate decoy proteins.");
    indexing_param.setValue("missing_decoy_action", "warn");
    indexing_param.setValue("enzyme:name", enzyme_name);
    pep_indexing.setParameters(indexing_param);

    pep_indexing.run(fasta_db, protein_ids, peptide_ids);

    // write output
    progresslogger.startProgress(0, 1, "Writing output...");
    if (out_idXML.size() > 0)
    {
      IdXMLFile().store(out_idXML, protein_ids, peptide_ids);
    }
    if (out_mzIdentML.size() > 0)
    {
      MzIdentMLFile().store(out_mzIdentML, protein_ids, peptide_ids);
    }
    if (out_xquest.size() > 0)
    {
      vector<String> input_split_dir;
      vector<String> input_split;
      getStringOption_("in").split(String("/"), input_split_dir);
      input_split_dir[input_split_dir.size()-1].split(String("."), input_split);
      String base_name = input_split[0];

      vector<String> output_split_dir;
      vector<String> output_split;
      Size found;
      found = out_xquest.find_last_of("/\\");
      // TODO "/" is Unix specific
      String matched_spec_xml_name;
      if (found == out_xquest.size())
      {
        matched_spec_xml_name = out_xquest.substr(0, found) + "/" + base_name + "_matched.spec.xml";
      }
      else
      {
        matched_spec_xml_name = base_name + "_matched.spec.xml";
      }

      // TODO old version here
//      writeXQuestXML(out_xquest, base_name, peptide_ids, all_top_csms, spectra);

      // TODO use this after RichPeaks are no longer used
      String precursor_mass_tolerance_unit_string = precursor_mass_tolerance_unit_ppm ? "ppm" : "Da";
      String fragment_mass_tolerance_unit_string = fragment_mass_tolerance_unit_ppm ? "ppm" : "Da";
      double cross_link_mass_iso_shift = 0;
      double cross_link_mass_light = cross_link_mass;
      OpenProXLUtils::writeXQuestXML(out_xquest, base_name, peptide_ids, all_top_csms, spectra,
                                                            precursor_mass_tolerance_unit_string, fragment_mass_tolerance_unit_string, precursor_mass_tolerance, fragment_mass_tolerance, fragment_mass_tolerance_xlinks, cross_link_name,
                                                            cross_link_mass_light, cross_link_mass_mono_link, in_fasta, in_decoy_fasta, cross_link_residue1, cross_link_residue2, cross_link_mass_iso_shift, enzyme_name, missed_cleavages);

      OpenProXLUtils::writeXQuestXMLSpec(matched_spec_xml_name, base_name, all_top_csms, spectra);
    }
    progresslogger.endProgress();

    return EXECUTION_OK;
  }

};

int main(int argc, const char** argv)
{

  TOPPOpenProXLLF tool;

  return tool.main(argc, argv);
}

