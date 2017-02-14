// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2016.
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
#include <OpenMS/CHEMISTRY/ModificationsDB.h>
#include <OpenMS/CHEMISTRY/EnzymaticDigestion.h>
#include <OpenMS/ANALYSIS/RNPXL/ModifiedPeptideGenerator.h>
#include <OpenMS/FORMAT/FASTAFile.h>
#include <OpenMS/FORMAT/Base64.h>
#include <boost/math/special_functions/binomial.hpp>
#include <fstream>

// preprocessing and filtering
#include <OpenMS/FILTERING/TRANSFORMERS/ThresholdMower.h>
//#include <OpenMS/FILTERING/TRANSFORMERS/NLargest.h>
//#include <OpenMS/FILTERING/TRANSFORMERS/WindowMower.h>
#include <OpenMS/FILTERING/TRANSFORMERS/Normalizer.h>

using namespace std;

namespace OpenMS
{
  // fast pre-Score for cross-links
  // required: numbers of peaks for each chain, and how many of them were matched
  float OpenProXLUtils::preScore(Size matchedAlpha, Size ionsAlpha, Size matchedBeta, Size ionsBeta)
  {
    if ( (ionsAlpha > 0) && (ionsBeta > 0) )
    {
      float result = sqrt((static_cast<float>(matchedAlpha) / static_cast<float>(ionsAlpha)) * (static_cast<float>(matchedBeta) / static_cast<float>(ionsBeta)));
      return result;
    } else
    {
      return 0.0;
    }
  }

  // fast pre-Score for mono-links and loop-links
  float OpenProXLUtils::preScore(Size matchedAlpha, Size ionsAlpha)
  {
    if (ionsAlpha > 0)
    {
      float result = static_cast<float>(matchedAlpha) / static_cast<float>(ionsAlpha);
      return result;
    } else
    {
      return 0.0;
    }
  }

  // Statistics/Combinatorics functions for match-odds score
  // Standard cumulative binomial distribution
  double OpenProXLUtils::cumulativeBinomial(Size n, Size k, double p)
  {
    double p_cumul = 0.0;
    if (p < 1e-99) return static_cast<double>(k == 0); //  (not true/false, but 1/0 as probability)
    if (1 - p < 1e-99) return static_cast<double>(k != n); //
    if (k > n)  return 1.0;

    //cout << "TEST cumulBinom, passed if's, p = " << p << endl;

    for (Size j = 0; j < k; j++)
    {
      double coeff = boost::math::binomial_coefficient<double>(static_cast<unsigned int>(n), static_cast<unsigned int>(j));
      p_cumul += coeff * pow(p,  j) * pow((1-p), (n-j));
      //cout << "TEST coeff: " << coeff << " | first pow: " << pow(p,  j) << " | second pow: " <<  pow((1-p), (n-j)) << " | just added: " << coeff * pow(p,  j) * pow((1-p), (n-j)) << " | new p_cumul: " << p_cumul << endl;
    }

    // match-odds score becomes INFINITY for p_cumul >= 1, p_cumul might reach 1 because of insufficient precision, solved by using largest value smaller than 1
    if (p_cumul >= 1.0)
    {
      p_cumul = nexttoward(1.0, 0.0);
    }

    return p_cumul;
  }

  // match odds score, spectra must be sorted by position
  double OpenProXLUtils::match_odds_score(const PeakSpectrum& theoretical_spec,  const std::vector< std::pair< Size, Size > >& matched_spec, double fragment_mass_tolerance, bool fragment_mass_tolerance_unit_ppm, bool is_xlink_spectrum, Size n_charges)
  {
    // TODO Theoretical spectra for cross-links contain 1. and 2. isotopic peak, is mostly one of them matched making theo_size = 2 * matched_size in the best case?
    // how does that skew the statistics?
    // should we use theo_size / 2 for cross-links?
    // or n_charges * 2?
    Size matched_size = matched_spec.size();
    Size theo_size = theoretical_spec.size();

    double range = theoretical_spec[theo_size-1].getMZ() -  theoretical_spec[0].getMZ();

    // Compute fragment tolerance for the middle of the range / mean of MZ values, if ppm
    // TODO mean should be used, so sum over MZs and devide by number, if ppm
    double mean = 0.0;
    for (Size i = 0; i < theo_size; ++i)
    {
      mean += theoretical_spec[i].getMZ();
    }
    mean = mean / theo_size;
    double tolerance_Th = fragment_mass_tolerance_unit_ppm ? mean * 1e-6 * fragment_mass_tolerance : fragment_mass_tolerance;

    // A priori probability of a random match given info about the theoretical spectrum
    //    double a_priori_p = a_priori_probability(tolerance_Th, theo_size, range, 3);
    double a_priori_p = 0;

    if (is_xlink_spectrum)
    {
      a_priori_p = (1 - ( pow( (1 - 2 * tolerance_Th / (0.5 * range)),  (theo_size / n_charges))));
    }
    else
    {
      a_priori_p = (1 - ( pow( (1 - 2 * tolerance_Th / (0.5 * range)),  (theo_size))));
    }

    double match_odds = 0;
    match_odds = -log(1 - cumulativeBinomial(theo_size, matched_size, a_priori_p) + 1e-5);

//     cout << "TEST a_priori_prob: " << a_priori_p << " | tolerance: " << tolerance_Th << " | theo_size: " << theo_size << " | matched_size: " << matched_size << " | cumul_binom: " << cumulativeBinomial(theo_size, matched_size, a_priori_p)
//              << " | match_odds: " << match_odds << endl;

    // score lower than 0 does not make sense, but can happen if cumBinom = 0, -log( 1 + 1e5 ) < 0
    if (match_odds >= 0.0)
    {
      return match_odds;
    }
    else
    {
      return 0;
    }
  }

//   // Cross-correlation, with shifting the second spectrum from -maxshift to +maxshift of tolerance bins (Tolerance in Da, a constant binsize)
//  template <typename SpectrumType1, typename SpectrumType2>
//  std::vector< double > OpenProXLUtils::xCorrelation(const SpectrumType1 & spec1, const SpectrumType2 & spec2, Int maxshift, double tolerance)
//  {
//    // generate vector of results, filled with zeroes
//    std::vector< double > results(maxshift * 2 + 1, 0);

//    // return 0 = no correlation, either positive nor negative, when one of the spectra is empty (e.g. when no common ions or xlink ions could be matched between light and heavy spectra)
//    if (spec1.size() == 0 || spec2.size() == 0) {
//      return results;
//    }

//    double maxionsize = max(spec1[spec1.size()-1].getMZ(), spec2[spec2.size()-1].getMZ());
//    Int table_size = ceil(maxionsize / tolerance)+1;
//    std::vector< double > ion_table1(table_size, 0);
//    std::vector< double > ion_table2(table_size, 0);

//    // Build tables of the same size, each bin has the size of the tolerance
//    for (Size i = 0; i < spec1.size(); ++i)
//    {
//      Size pos = static_cast<Size>(ceil(spec1[i].getMZ() / tolerance));
//      // TODO this line leads to using real intensities
////      ion_table1[pos] = spec1[i].getIntensity();
//      // TODO this line leads to using intensities normalized to 10
//      ion_table1[pos] = 10.0;
//    }
//    for (Size i = 0; i < spec2.size(); ++i)
//    {
//      Size pos =static_cast<Size>(ceil(spec2[i].getMZ() / tolerance));
//      // TODO this line leads to using real intensities
////      ion_table2[pos] = spec2[i].getIntensity();
//      // TODO this line leads to using intensities normalized to 10
//      ion_table2[pos] = 10.0;
//    }

//    // Compute means for real intensities
//    double mean1 = (std::accumulate(ion_table1.begin(), ion_table1.end(), 0.0)) / table_size;
//    double mean2 = (std::accumulate(ion_table2.begin(), ion_table2.end(), 0.0)) / table_size;

//    // Compute denominator
//    double s1 = 0;
//    double s2 = 0;
//    for (Int i = 0; i < table_size; ++i)
//    {
//      s1 += pow((ion_table1[i] - mean1), 2);
//      s2 += pow((ion_table2[i] - mean2), 2);
//    }
//    double denom = sqrt(s1 * s2);

//    // Calculate correlation for each shift
//    for (Int shift = -maxshift; shift <= maxshift; ++shift)
//    {
//      double s = 0;
//      for (Int i = 0; i < table_size; ++i)
//      {
//        Int j = i + shift;
//        if ( (j >= 0) && (j < table_size))
//        {
//          s += (ion_table1[i] - mean1) * (ion_table2[j] - mean2);
//        }
//      }
//      if (denom > 0)
//      {
//        results[shift + maxshift] = s / denom;
//      }
//    }
//    return results;
//  }

  // weigthed TIC score, using standard max- and mindigestlength, TODO remove digestlength from equation after benchmarking scores against xQuest?
  double OpenProXLUtils::weighted_TIC_score(Size alpha_size, Size beta_size, double intsum_alpha, double intsum_beta, double intsum, double total_current, bool type_is_cross_link)
  {
    // TODO from xquest.def, but not used in this program aside from this calculation
    double maxdigestlength = 50;
    double mindigestlength = 5;
    if (!type_is_cross_link)
    {
      beta_size = ( maxdigestlength + mindigestlength ) - alpha_size;
      // this should already be the case
      intsum_beta = 0;
      intsum_alpha = intsum;
    }

    double aatotal = alpha_size + beta_size;

    // TODO maybe use this alternative version, based only on the lengths of the sequences?
    //double invMax = 1 / (min(alpha_size, beta_size) / aatotal);
    double invMax = 1 / (mindigestlength / (mindigestlength + maxdigestlength));
    double invFrac_alpha = 1 / (alpha_size / aatotal);
    double invFrac_beta = 1 / (beta_size / aatotal);
    double TIC_weight_alpha = invFrac_alpha / invMax;
    double TIC_weight_beta = invFrac_beta / invMax;

    double wTIC = TIC_weight_alpha * (intsum_alpha / total_current ) + TIC_weight_beta * (intsum_beta / total_current);
    return wTIC;
  }

//  // Sum of matched ion intesity, for Intsum score and %TIC score
//  double OpenProXLUtils::matched_current_chain(const std::vector< std::pair< Size, Size > >& matched_spec_common, const std::vector< std::pair< Size, Size > >& matched_spec_xlinks, const PeakSpectrum& spectrum_common_peaks, const PeakSpectrum& spectrum_xlink_peaks)
//  {
//    double intsum = 0;
//    for (SignedSize j = 0; j < static_cast<SignedSize>(matched_spec_common.size()); ++j)
//    {
//      intsum += spectrum_common_peaks[matched_spec_common[j].second].getIntensity();
//    }
//    for (SignedSize j = 0; j < static_cast<SignedSize>(matched_spec_xlinks.size()); ++j)
//    {
//      intsum += spectrum_xlink_peaks[matched_spec_xlinks[j].second].getIntensity();
//    }
//    return intsum;
//  }

//  double OpenProXLUtils::total_matched_current(const std::vector< std::pair< Size, Size > >& matched_spec_common_alpha, const std::vector< std::pair< Size, Size > >& matched_spec_common_beta, const std::vector< std::pair< Size, Size > >& matched_spec_xlinks_alpha, const std::vector< std::pair< Size, Size > >& matched_spec_xlinks_beta, const PeakSpectrum& spectrum_common_peaks, const PeakSpectrum& spectrum_xlink_peaks)
//  {
//    // make vectors of matched peak indices
//    double intsum = 0;
//    std::vector< Size > indices_common;
//    std::vector< Size > indices_xlinks;
//    for (Size j = 0; j < matched_spec_common_alpha.size(); ++j)
//    {
//      indices_common.push_back(matched_spec_common_alpha[j].second);
//    }
//    for (Size j = 0; j < matched_spec_common_beta.size(); ++j)
//    {
//      indices_common.push_back(matched_spec_common_beta[j].second);
//    }
//    for (Size j = 0; j < matched_spec_xlinks_alpha.size(); ++j)
//    {
//      indices_xlinks.push_back(matched_spec_xlinks_alpha[j].second);
//    }
//    for (Size j = 0; j < matched_spec_xlinks_beta.size(); ++j)
//    {
//      indices_xlinks.push_back(matched_spec_xlinks_beta[j].second);
//    }

//    // make the indices in the vectors unique
//    sort(indices_common.begin(), indices_common.end());
//    sort(indices_xlinks.begin(), indices_xlinks.end());
//    std::vector< Size >::iterator last_unique_common = unique(indices_common.begin(), indices_common.end());
//    std::vector< Size >::iterator last_unique_xlinks = unique(indices_xlinks.begin(), indices_xlinks.end());
//    indices_common.erase(last_unique_common, indices_common.end());
//    indices_xlinks.erase(last_unique_xlinks, indices_xlinks.end());

//    // sum over intensities under the unique indices
//    for (Size j = 0; j < indices_common.size(); ++j)
//    {
//      intsum += spectrum_common_peaks[indices_common[j]].getIntensity();
//    }
//    for (Size j = 0; j < indices_xlinks.size(); ++j)
//    {
//      intsum += spectrum_xlink_peaks[indices_xlinks[j]].getIntensity();
//    }

//    return intsum;
//  }


  // check whether the candidate pair is within the given tolerance to at least one precursor mass in the spectra data
  void filter_and_add_candidate (vector<OpenProXLUtils::XLPrecursor>& mass_to_candidates, vector< double >& spectrum_precursors, bool precursor_mass_tolerance_unit_ppm, double precursor_mass_tolerance, OpenProXLUtils::XLPrecursor precursor)
  {
    bool found_matching_precursors = false;
    // loop over all considered ion charges;
    // TODO: maybe precompute uncharged masses from precursor m/z values instead? don't forget to filter by charge then

    // use candidate mass and current charge to compute m/z
    //double cross_link_mz = (precursor.precursor_mass + (static_cast<double>(charge) * Constants::PROTON_MASS_U)) / static_cast<double>(charge);

    vector< double >::const_iterator low_it;
    vector< double >::const_iterator up_it;

    // compute absolute tolerance from relative, if necessary
    double allowed_error = 0;
    if (precursor_mass_tolerance_unit_ppm) // ppm
    {
      allowed_error = precursor.precursor_mass * precursor_mass_tolerance * 1e-6;
    }
    else // Dalton
    {
      allowed_error = precursor_mass_tolerance;
    }

    // find precursor with m/z >= low end of range
    low_it = lower_bound(spectrum_precursors.begin(), spectrum_precursors.end(), precursor.precursor_mass - allowed_error);
    // find precursor with m/z > (not equal to) high end of range
    up_it =  upper_bound(spectrum_precursors.begin(), spectrum_precursors.end(), precursor.precursor_mass + allowed_error);
    // if these two are equal, there is no precursor within the range

    if (low_it != up_it) // if they are not equal, there are matching precursors in the data
    {
      found_matching_precursors = true;
    }


    // if precursors were found in the above for-loop, add candidate to results vector
    if (found_matching_precursors)
    {
// don't access this vector from two processing threads at the same time
#ifdef _OPENMP
#pragma omp critical
#endif
      mass_to_candidates.push_back(precursor);
    }
  }


  // Enumerate all pairs of peptides from the searched database and calculate their masses (inlcuding mono-links and loop-links)
  vector<OpenProXLUtils::XLPrecursor> OpenProXLUtils::enumerateCrossLinksAndMasses_(const vector<OpenProXLUtils::PeptideMass>&  peptides, double cross_link_mass, const DoubleList& cross_link_mass_mono_link, const StringList& cross_link_residue1, const StringList& cross_link_residue2, vector< double >& spectrum_precursors, double precursor_mass_tolerance, bool precursor_mass_tolerance_unit_ppm)
  {
    // initialize empty vector for the results
    vector<OpenProXLUtils::XLPrecursor> mass_to_candidates;
    // initialize progress counter
    Size countA = 0;

    double min_precursor = spectrum_precursors[0];
    double max_precursor = spectrum_precursors[spectrum_precursors.size()-1];

// Multithreading options: schedule: static, dynamic, guided
// use OpenMP to run this for-loop on multiple CPU cores
#ifdef _OPENMP
#pragma omp parallel for schedule(guided)
#endif
    for (SignedSize p1 = 0; p1 < static_cast<SignedSize>(peptides.size()); ++p1)
    {
      // get the amino acid sequence of this peptide as a character string
      String seq_first = peptides[p1].peptide_seq.toUnmodifiedString();

      // every 500 peptides print current progress to console
      countA += 1;
      if (countA % 500 == 0)
      {
        cout << "Enumerating pairs with sequence " << countA << " of " << peptides.size() << ";\t Current pair count: " << mass_to_candidates.size() << endl;
      }

      // generate mono-links: one cross-linker with one peptide attached to one side
      for (Size i = 0; i < cross_link_mass_mono_link.size(); i++)
      {
        // Monoisotopic weight of the peptide + cross-linker
        double cross_linked_pair_mass = peptides[p1].peptide_mass + cross_link_mass_mono_link[i];

        // Make sure it is clear only one peptide is considered here. Use NULL value for the second peptide.
        // to check: if(precursor.beta_index) returns "false" for NULL, "true" for any other value
        XLPrecursor precursor;
        precursor.precursor_mass = cross_linked_pair_mass;
        precursor.alpha_index = p1;
        precursor.beta_index = NULL;

        // call function to compare with spectrum precursor masses
        // will only add this candidate, if the mass is within the given tolerance to any precursor in the spectra data
        filter_and_add_candidate(mass_to_candidates, spectrum_precursors, precursor_mass_tolerance_unit_ppm, precursor_mass_tolerance, precursor);
      }


       // test if this peptide could have loop-links: one cross-link with both sides attached to the same peptide
       // TODO check for distance between the two linked residues
      bool first_res = false; // is there a residue the first side of the linker can attach to?
      bool second_res = false; // is there a residue the second side of the linker can attach to?
      for (Size k = 0; k < seq_first.size()-1; ++k)
      {
        for (Size i = 0; i < cross_link_residue1.size(); ++i)
        {
          if (seq_first.substr(k, 1) == cross_link_residue1[i])
          {
            first_res = true;
          }
        }
        for (Size i = 0; i < cross_link_residue2.size(); ++i)
        {
          if (seq_first.substr(k, 1) == cross_link_residue2[i])
          {
            second_res = true;
          }
        }
      }

      // If both sides of a cross-linker can link to this peptide, generate the loop-link
      if (first_res && second_res)
      {
        // Monoisotopic weight of the peptide + cross-linker
        double cross_linked_pair_mass = peptides[p1].peptide_mass + cross_link_mass;

        // also only one peptide
        XLPrecursor precursor;
        precursor.precursor_mass = cross_linked_pair_mass;
        precursor.alpha_index = p1;
        precursor.beta_index = NULL;

        // call function to compare with spectrum precursor masses
        filter_and_add_candidate(mass_to_candidates, spectrum_precursors, precursor_mass_tolerance_unit_ppm, precursor_mass_tolerance, precursor);
      }

      // check for minimal mass of second peptide, jump farther than current peptide if possible
      double allowed_error = 0;
      if (precursor_mass_tolerance_unit_ppm) // ppm
      {
        allowed_error = min_precursor * precursor_mass_tolerance * 1e-6;
      }
      else // Dalton
      {
        allowed_error = precursor_mass_tolerance;
      }
      double min_second_peptide_mass = min_precursor - cross_link_mass - peptides[p1].peptide_mass - allowed_error;

      if (precursor_mass_tolerance_unit_ppm) // ppm
      {
        allowed_error = max_precursor * precursor_mass_tolerance * 1e-6;
      }
      double max_second_peptide_mass = max_precursor - cross_link_mass - peptides[p1].peptide_mass + allowed_error;

      // Generate cross-links: one cross-linker linking two separate peptides, the most important case
      // Loop over all p2 peptide candidates, that come after p1 in the list
      for (Size p2 = p1; p2 < peptides.size(); ++p2)
      {
        // skip peptides, that are too small in any case
        if (peptides[p2].peptide_mass < min_second_peptide_mass)
        {
          continue;
        } else if (peptides[p2].peptide_mass > max_second_peptide_mass)
        {
          break;
        }

        // Monoisotopic weight of the first peptide + the second peptide + cross-linker
        double cross_linked_pair_mass = peptides[p1].peptide_mass + peptides[p2].peptide_mass + cross_link_mass;

        // this time both peptides have valid indices
        XLPrecursor precursor;
        precursor.precursor_mass = cross_linked_pair_mass;
        precursor.alpha_index = p1;
        precursor.beta_index = p2;

        // call function to compare with spectrum precursor masses
        filter_and_add_candidate(mass_to_candidates, spectrum_precursors, precursor_mass_tolerance_unit_ppm, precursor_mass_tolerance, precursor);
      }
    }
    return mass_to_candidates;
  }

//  // Enumerates all possible combinations containing a cross-link, without specific cross-link positions. (There are cases where multiple positions are possible, but they have the same precursor mass)
//  // At this point the only difference between mono-links and loop-links is the added cross-link mass
//  multimap<double, pair<const AASequence*, const AASequence*> > OpenProXLUtils::enumerateCrossLinksAndMasses_(const multimap<StringView, AASequence>&  peptides, double cross_link_mass, const DoubleList& cross_link_mass_mono_link, const StringList& cross_link_residue1, const StringList& cross_link_residue2)
//  {
//    multimap<double, pair<const AASequence*, const AASequence*> > mass_to_candidates;
//    Size countA = 0;

//    vector<const StringView*> peptide_SVs;
//    vector<const AASequence*> peptide_AASeqs;
//    // preparing vectors compatible with openmp multi-threading, TODO this should only be a temporary fix (too much overhead?)
//    for (map<StringView, AASequence>::const_iterator a = peptides.begin(); a != peptides.end(); ++a)
//    {
//      peptide_SVs.push_back(&(a->first));
//      peptide_AASeqs.push_back(&(a->second));
//    }

//    //for (map<StringView, AASequence>::const_iterator a = peptides.begin(); a != peptides.end(); ++a) // old loop version

//// Multithreading options: schedule: static, dynamic, guided with chunk size
//#ifdef _OPENMP
//#pragma omp parallel for schedule(guided)
//#endif
//    for (Size p1 = 0; p1 < peptide_AASeqs.size(); ++p1)
//    {
//      String seq_first = peptide_AASeqs[p1]->toUnmodifiedString();

//      countA += 1;
//      if (countA % 500 == 0)
//      {
//        //LOG_DEBUG << "Enumerating pairs with sequence " << countA << " of " << peptides.size() << ";\t Current pair count: " << mass_to_candidates.size() << endl;
//        cout << "Enumerating pairs with sequence " << countA << " of " << peptides.size() << ";\t Current pair count: " << mass_to_candidates.size() << endl;

//      }

//      // generate mono-links
//      for (Size i = 0; i < cross_link_mass_mono_link.size(); i++)
//      {
//        double cross_linked_pair_mass = peptide_AASeqs[p1]->getMonoWeight() + cross_link_mass_mono_link[i];
//        // Make sure it is clear this is a monolink, (is a NULL pointer a good idea?)
//#ifdef _OPENMP
//#pragma omp critical
//#endif
//        mass_to_candidates.insert(make_pair(cross_linked_pair_mass, make_pair<const AASequence*, const AASequence*>(peptide_AASeqs[p1], NULL)));
//      }

//      // generate loop-links
//      bool first_res = false;
//      bool second_res = false;
//      for (Size k = 0; k < seq_first.size()-1; ++k)
//      {
//        for (Size i = 0; i < cross_link_residue1.size(); ++i)
//        {
//          if (seq_first.substr(k, 1) == cross_link_residue1[i])
//          {
//            first_res = true;
//          }
//        }
//        for (Size i = 0; i < cross_link_residue2.size(); ++i)
//        {
//          if (seq_first.substr(k, 1) == cross_link_residue2[i])
//          {
//            second_res = true;
//          }
//        }
//      }
//      // If both sides of a homo- or heterobifunctional cross-linker can link to this peptide, generate the loop-link
//      if (first_res && second_res)
//      {
//        double cross_linked_pair_mass = peptide_AASeqs[p1]->getMonoWeight() + cross_link_mass;
//#ifdef _OPENMP
//#pragma omp critical
//#endif
//        mass_to_candidates.insert(make_pair(cross_linked_pair_mass, make_pair<const AASequence*, const AASequence*>(peptide_AASeqs[p1], NULL)));
//      }

//      // Generate cross-link between two peptides
//      //for (map<StringView, AASequence>::const_iterator b = a; b != peptides.end(); ++b)
//      for (Size p2 = p1; p2 < peptide_AASeqs.size(); ++p2)
//      {
//        // mass peptide1 + mass peptide2 + cross linker mass - cross link loss
//        double cross_linked_pair_mass = peptide_AASeqs[p1]->getMonoWeight() + peptide_AASeqs[p2]->getMonoWeight() + cross_link_mass;
//#ifdef _OPENMP
//#pragma omp critical
//#endif
//        mass_to_candidates.insert(make_pair(cross_linked_pair_mass, make_pair<const AASequence*, const AASequence*>(peptide_AASeqs[p1], peptide_AASeqs[p2])));
//      }
//    }

//    return mass_to_candidates;
//  }

 // Write xQuest.xml output
  void  OpenProXLUtils::writeXQuestXML(String out_file, String base_name, const std::vector< PeptideIdentification >& peptide_ids, const std::vector< std::vector< CrossLinkSpectrumMatch > >& all_top_csms, const PeakMap& spectra,
                                                String precursor_mass_tolerance_unit, String fragment_mass_tolerance_unit, double precursor_mass_tolerance, double fragment_mass_tolerance, double fragment_mass_tolerance_xlinks, String cross_link_name,
                                                double cross_link_mass_light, DoubleList cross_link_mass_mono_link, String in_fasta, String in_decoy_fasta, StringList cross_link_residue1, StringList cross_link_residue2, double cross_link_mass_iso_shift, String enzyme_name, Size missed_cleavages)
  {
    String spec_xml_name = base_name + "_matched";

    cout << "Writing xquest.xml to " << out_file << endl;
    ofstream xml_file;
    xml_file.open(out_file.c_str(), ios::trunc);
    // XML Header
    xml_file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl;
    xml_file << "<?xml-stylesheet type=\"text/xsl\" href=\"\"?>" << endl;

    // TODO!!! write actual experiment data
    // original date/time format: Fri Dec 18 12:28:23 2015
    DateTime time= DateTime::now();
    String timestring = time.getDate() + " " + time.getTime();

//    String precursor_mass_tolerance_unit = getStringOption_("precursor:mass_tolerance_unit");
//    String fragment_mass_tolerance_unit = getStringOption_("fragment:mass_tolerance_unit");
//    double precursor_mass_tolerance = getDoubleOption_("precursor:mass_tolerance");
//    double fragment_mass_tolerance = getDoubleOption_("fragment:mass_tolerance");
//    double fragment_mass_tolerance_xlinks = getDoubleOption_("fragment:mass_tolerance_xlinks");

//    String cross_link_name = getStringOption_("cross_linker:name");
//    double cross_link_mass_light = getDoubleOption_("cross_linker:mass_light");
//    DoubleList cross_link_mass_mono_link = getDoubleList_("cross_linker:mass_mono_link");
    String mono_masses;
    for (Size k = 0; k < cross_link_mass_mono_link.size()-1; ++k)
    {
      mono_masses += String(cross_link_mass_mono_link[k]) + ", ";
    }
    mono_masses += cross_link_mass_mono_link[cross_link_mass_mono_link.size()-1];

//    const string in_fasta(getStringOption_("database"));
//    const string in_decoy_fasta(getStringOption_("decoy_database"));
//    StringList cross_link_residue1 = getStringList_("cross_linker:residue1");
//    StringList cross_link_residue2 = getStringList_("cross_linker:residue2");
    String aarequired1, aarequired2;
    for (Size k= 0; k < cross_link_residue1.size()-1; ++k)
    {
      aarequired1 += cross_link_residue1[k] + ",";
    }
    aarequired1 += cross_link_residue1[cross_link_residue1.size()-1];
    for (Size k= 0; k < cross_link_residue2.size()-1; ++k)
    {
      aarequired2 += cross_link_residue2[k] + ",";
    }
    aarequired2 += cross_link_residue2[cross_link_residue2.size()-1];

//    double cross_link_mass_iso_shift = 0;
//    // This parameter is only available for the algorithm for labeled linkers
//    try
//    {
//      cross_link_mass_iso_shift = getDoubleOption_("cross_linker:mass_iso_shift");
//    }
//    catch (...)
//    {
//    }


//    String enzyme_name = getStringOption_("peptide:enzyme");
//    Size missed_cleavages = getIntOption_("peptide:missed_cleavages");

    xml_file << "<xquest_results xquest_version=\"OpenProXL 1.0\" date=\"" << timestring <<
             "\" author=\"Eugen Netz, Timo Sachsenberg\" tolerancemeasure_ms1=\"" << precursor_mass_tolerance_unit  <<
             "\" tolerancemeasure_ms2=\"" << fragment_mass_tolerance_unit << "\" ms1tolerance=\"" << precursor_mass_tolerance <<
             "\" ms2tolerance=\"" << fragment_mass_tolerance << "\" xlink_ms2tolerance=\"" << fragment_mass_tolerance_xlinks <<
             "\" crosslinkername=\"" << cross_link_name << "\" xlinkermw=\"" << cross_link_mass_light <<
             "\" monolinkmw=\"" << mono_masses << "\" database=\"" << in_fasta << "\" database_dc=\"" << in_decoy_fasta <<
             "\" xlinktypes=\"1111\" AArequired1=\"" << aarequired1 << "\" AArequired2=\"" << aarequired2 <<  "\" cp_isotopediff=\"" << cross_link_mass_iso_shift <<
             "\" enzyme_name=\"" << enzyme_name << "\" outputpath=\"" << spec_xml_name <<
             "\" Iontag_charges_for_index=\"1\" missed_cleavages=\"" << missed_cleavages <<
             "\" ntermxlinkable=\"0\" CID_match2ndisotope=\"1" <<
             "\" variable_mod=\"TODO\" nocutatxlink=\"1\" xcorrdelay=\"5\" >" << endl;



    for (vector< vector< CrossLinkSpectrumMatch > >::const_iterator top_csms_spectrum = all_top_csms.begin(); top_csms_spectrum != all_top_csms.end(); ++top_csms_spectrum)
    {
      vector< CrossLinkSpectrumMatch > top_vector = (*top_csms_spectrum);

      if (!top_vector.empty())
      {
        // Spectrum Data, for each spectrum
        Size scan_index_light = top_vector[0].scan_index_light;
        Size scan_index_heavy = scan_index_light;
        if (cross_link_mass_iso_shift > 0)
        {
          scan_index_heavy = top_vector[0].scan_index_heavy;
        }
        const PeakSpectrum& spectrum_light = spectra[scan_index_light];
        double precursor_charge = spectrum_light.getPrecursors()[0].getCharge();

        double precursor_mz = spectrum_light.getPrecursors()[0].getMZ();
        double precursor_rt = spectrum_light.getRT();
        double precursor_mass = precursor_mz * static_cast<double>(precursor_charge) - static_cast<double>(precursor_charge) * Constants::PROTON_MASS_U;

        double precursor_mz_heavy = spectra[scan_index_heavy].getPrecursors()[0].getMZ();
        double precursor_rt_heavy = spectra[scan_index_heavy].getRT();

        // print information about new peak to file (starts with <spectrum_search..., ends with </spectrum_search>
        String spectrum_light_name = base_name + ".light." + scan_index_light;
        String spectrum_heavy_name = base_name + ".heavy." + scan_index_heavy;

        String spectrum_name = spectrum_light_name + String("_") + spectrum_heavy_name;
        String rt_scans = String(precursor_rt) + ":" + String(precursor_rt_heavy);
        String mz_scans = String(precursor_mz) + ":" + String(precursor_mz_heavy);

        // Mean ion intensity (light spectrum, TODO add heavy spectrum?)
        double mean_intensity= 0;
        if (cross_link_mass_iso_shift > 0)
        {
          for (SignedSize j = 0; j < static_cast<SignedSize>(spectrum_light.size()); ++j) mean_intensity += spectrum_light[j].getIntensity();
          for (SignedSize j = 0; j < static_cast<SignedSize>(spectra[scan_index_heavy].size()); ++j) mean_intensity += spectra[scan_index_heavy][j].getIntensity();
          mean_intensity = mean_intensity / (spectrum_light.size() + spectra[scan_index_heavy].size());
        }
        else
        {
          for (SignedSize j = 0; j < static_cast<SignedSize>(spectrum_light.size()); ++j) mean_intensity += spectrum_light[j].getIntensity();
          mean_intensity = mean_intensity / spectrum_light.size();
        }

        xml_file << "<spectrum_search spectrum=\"" << spectrum_name << "\" mean_ionintensity=\"" << mean_intensity << "\" ionintensity_stdev=\"" << "TODO" << "\" addedMass=\"" << "TODO" << "\" iontag_ncandidates=\"" << "TODO"
            << "\"  apriori_pmatch_common=\"" << "TODO" << "\" apriori_pmatch_xlink=\"" << "TODO" << "\" ncommonions=\"" << "TODO" << "\" nxlinkions=\"" << "TODO" << "\" mz_precursor=\"" << precursor_mz
            << "\" scantype=\"" << "light_heavy" << "\" charge_precursor=\"" << precursor_charge << "\" Mr_precursor=\"" << precursor_mass <<  "\" rtsecscans=\"" << rt_scans << "\" mzscans=\"" << mz_scans << "\" >" << endl;


        for (vector< CrossLinkSpectrumMatch>::const_iterator top_csm = top_csms_spectrum->begin(); top_csm != top_csms_spectrum->end(); ++top_csm)
        {
          String xltype = "monolink";
          String structure = top_csm->cross_link.alpha.toUnmodifiedString();
          String letter_first = structure.substr(top_csm->cross_link.cross_link_position.first, 1);


           // TODO track or otherwise find out, which kind of mono-link it was (if there are several possibilities for the weigths)
          double weight = top_csm->cross_link.alpha.getMonoWeight() + top_csm->cross_link.cross_linker_mass;
//            bool is_monolink = (top_csm->cross_link.cross_link_position.second == -1);
          int alpha_pos = top_csm->cross_link.cross_link_position.first + 1;
          int beta_pos = top_csm->cross_link.cross_link_position.second + 1;

          String topology = String("a") + alpha_pos;
          String id = structure + String("-") + letter_first + alpha_pos + String("-") + static_cast<int>(top_csm->cross_link.cross_linker_mass);

          if (top_csm->cross_link.getType() == ProteinProteinCrossLink::CROSS)
          {
            xltype = "xlink";
            structure += "-" + top_csm->cross_link.beta.toUnmodifiedString();
            topology += String("-b") + beta_pos;
            weight += top_csm->cross_link.beta.getMonoWeight();
            id = structure + "-" + topology;
          }
          else if (top_csm->cross_link.getType() == ProteinProteinCrossLink::LOOP)
          {
            xltype = "intralink";
            topology += String("-b") + beta_pos;
            String letter_second = structure.substr(top_csm->cross_link.cross_link_position.second, 1);
            id = structure + String("-") + letter_first + alpha_pos + String("-") + letter_second + beta_pos;
          }

           // Error calculation
          double cl_mz = (weight + (static_cast<double>(precursor_charge) * Constants::PROTON_MASS_U)) / static_cast<double>(precursor_charge);
          double error = precursor_mz - cl_mz;
          double rel_error = (error / cl_mz) / 1e-6;

          PeptideIdentification pep_id = peptide_ids[top_csm->peptide_id_index];
          vector< PeptideHit > pep_hits = pep_id.getHits();

          String prot_alpha = pep_hits[0].getPeptideEvidences()[0].getProteinAccession();
          if (pep_hits[0].getPeptideEvidences().size() > 1)
          {
            for (Size i = 1; i < pep_hits[0].getPeptideEvidences().size(); ++i)
            {
              prot_alpha = prot_alpha + "," + pep_hits[0].getPeptideEvidences()[i].getProteinAccession();
            }
          }

          String prot_beta = "";

          if (pep_hits.size() > 1)
          {
            prot_beta= pep_hits[1].getPeptideEvidences()[0].getProteinAccession();
            if (pep_hits[1].getPeptideEvidences().size() > 1)
            {
              for (Size i = 1; i < pep_hits[1].getPeptideEvidences().size(); ++i)
              {
                prot_alpha = prot_alpha + "," + pep_hits[1].getPeptideEvidences()[i].getProteinAccession();
              }
            }
          }
          // Hit Data, for each cross-link to Spectrum Hit (e.g. top 5 per spectrum)
          xml_file << "<search_hit search_hit_rank=\"" <<top_csm->rank << "\" id=\"" << id << "\" type=\"" << xltype << "\" structure=\"" << structure << "\" seq1=\"" << top_csm->cross_link.alpha.toUnmodifiedString() << "\" seq2=\"" << top_csm->cross_link.beta.toUnmodifiedString()
                << "\" prot1=\"" << prot_alpha << "\" prot2=\"" << prot_beta << "\" topology=\"" << topology << "\" xlinkposition=\"" << (top_csm->cross_link.cross_link_position.first+1) << "," << (top_csm->cross_link.cross_link_position.second+1)
                << "\" Mr=\"" << weight << "\" mz=\"" << cl_mz << "\" charge=\"" << precursor_charge << "\" xlinkermass=\"" << top_csm->cross_link.cross_linker_mass << "\" measured_mass=\"" << precursor_mass << "\" error=\"" << error
                << "\" error_rel=\"" << rel_error << "\" xlinkions_matched=\"" << (top_csm->matched_xlink_alpha + top_csm->matched_xlink_beta) << "\" backboneions_matched=\"" << (top_csm->matched_common_alpha + top_csm->matched_common_beta)
                << "\" weighted_matchodds_mean=\"" << "TODO" << "\" weighted_matchodds_sum=\"" << "TODO" << "\" match_error_mean=\"" << "TODO" << "\" match_error_stdev=\"" << "TODO" << "\" xcorrx=\"" << top_csm->xcorrx_max << "\" xcorrb=\"" << top_csm->xcorrc_max << "\" match_odds=\"" <<top_csm->match_odds << "\" prescore=\"" << top_csm->pre_score
                << "\" prescore_alpha=\"" << "TODO" << "\" prescore_beta=\"" << "TODO" << "\" match_odds_alphacommon=\"" << "TODO" << "\" match_odds_betacommon=\"" << "TODO" << "\" match_odds_alphaxlink=\"" << "TODO"
                << "\" match_odds_betaxlink=\"" << "TODO" << "\" num_of_matched_ions_alpha=\"" << (top_csm->matched_common_alpha + top_csm->matched_xlink_alpha) << "\" num_of_matched_ions_beta=\"" << (top_csm->matched_common_beta + top_csm->matched_xlink_beta) << "\" num_of_matched_common_ions_alpha=\"" << top_csm->matched_common_alpha
                << "\" num_of_matched_common_ions_beta=\"" << top_csm->matched_common_beta << "\" num_of_matched_xlink_ions_alpha=\"" << top_csm->matched_xlink_alpha << "\" num_of_matched_xlink_ions_beta=\"" << top_csm->matched_xlink_beta << "\" xcorrall=\"" << "TODO" << "\" TIC=\"" << top_csm->percTIC
                << "\" TIC_alpha=\"" << "TODO" << "\" TIC_beta=\"" << "TODO" << "\" wTIC=\"" << top_csm->wTIC << "\" intsum=\"" << top_csm->int_sum * 100 << "\" apriori_match_probs=\"" << "TODO" << "\" apriori_match_probs_log=\"" << "TODO"
                << "\" HyperCommon=\"" << top_csm->HyperCommon << "\" HyperXLink=\"" << top_csm->HyperXlink << "\" HyperBoth=\"" << top_csm->HyperBoth << "\" PScoreCommon=\"" << top_csm->PScoreCommon << "\" PScoreXLink=\"" << top_csm->PScoreXlink << "\" PScoreBoth=\"" << top_csm->PScoreBoth
                << "\" series_score_mean=\"" << "TODO" << "\" annotated_spec=\"" << "" << "\" score=\"" << top_csm->score << "\" >" << endl;
          xml_file << "</search_hit>" << endl;
        }
        // Closing tag for Spectrum
        xml_file << "</spectrum_search>" << endl;
      }
    }

    // Closing tag for results (end of file)
    xml_file << "</xquest_results>" << endl;
    xml_file.close();

    return;
  }

  PeakSpectrum OpenProXLUtils::mergeAnnotatedSpectra(PeakSpectrum & first_spectrum, PeakSpectrum & second_spectrum)
  {
    // merge peaks: create new spectrum, insert peaks from first and then from second spectrum
    PeakSpectrum resulting_spectrum;
    resulting_spectrum.insert(resulting_spectrum.end(), first_spectrum.begin(), first_spectrum.end());
    resulting_spectrum.insert(resulting_spectrum.end(), second_spectrum.begin(), second_spectrum.end());

    // merge DataArrays in a similar way
    for (Size i = 0; i < first_spectrum.getFloatDataArrays().size(); i++)
    {
      // TODO instead of this "if", get second array by name if available.  would not be dependent on order.
      if (second_spectrum.getFloatDataArrays().size() > i)
      {
        PeakSpectrum::FloatDataArray float_array;
        float_array.insert(float_array.end(), first_spectrum.getFloatDataArrays()[i].begin(), first_spectrum.getFloatDataArrays()[i].end());
        float_array.insert(float_array.end(), second_spectrum.getFloatDataArrays()[i].begin(), second_spectrum.getFloatDataArrays()[i].end());
        resulting_spectrum.getFloatDataArrays().push_back(float_array);
      }
    }

    for (Size i = 0; i < first_spectrum.getStringDataArrays().size(); i++)
    {
      if (second_spectrum.getStringDataArrays().size() > i)
      {
        PeakSpectrum::StringDataArray string_array;
        string_array.insert(string_array.end(), first_spectrum.getStringDataArrays()[i].begin(), first_spectrum.getStringDataArrays()[i].end());
        string_array.insert(string_array.end(), second_spectrum.getStringDataArrays()[i].begin(), second_spectrum.getStringDataArrays()[i].end());
        resulting_spectrum.getStringDataArrays().push_back(string_array);
      }
    }

    for (Size i = 0; i < first_spectrum.getIntegerDataArrays().size(); i++)
    {
      if (second_spectrum.getIntegerDataArrays().size() > i)
      {
        PeakSpectrum::IntegerDataArray integer_array;
        integer_array.insert(integer_array.end(), first_spectrum.getIntegerDataArrays()[i].begin(), first_spectrum.getIntegerDataArrays()[i].end());
        integer_array.insert(integer_array.end(), second_spectrum.getIntegerDataArrays()[i].begin(), second_spectrum.getIntegerDataArrays()[i].end());
        resulting_spectrum.getIntegerDataArrays().push_back(integer_array);
      }
    }

//    PeakSpectrum::StringDataArray names;
//    PeakSpectrum::IntegerDataArray charges;
//    names.insert(names.end(), first_spectrum.getStringDataArrays()[0].begin(), first_spectrum.getStringDataArrays()[0].end());
//    names.insert(names.end(), second_spectrum.getStringDataArrays()[0].begin(), second_spectrum.getStringDataArrays()[0].end());
//    resulting_spectrum.getStringDataArrays().push_back(names);

//    charges.insert(charges.end(), first_spectrum.getIntegerDataArrays()[0].begin(), first_spectrum.getIntegerDataArrays()[0].end());
//    charges.insert(charges.end(), second_spectrum.getIntegerDataArrays()[0].begin(), second_spectrum.getIntegerDataArrays()[0].end());
//    resulting_spectrum.getIntegerDataArrays().push_back(charges);

    // Spectra were simply concatenated, so they are not sorted by position anymore
    resulting_spectrum.sortByPosition();
    return resulting_spectrum;
  }

  void OpenProXLUtils::nLargestSpectrumFilter(PeakSpectrum spectrum, int peak_count)
  {
    if (spectrum.size() <= peak_count) return;

    // sort by reverse intensity
    spectrum.sortByIntensity(true);

    // keep the n largest peaks if more than n are present
    spectrum.resize(peak_count);

    // also resize DataArrays
    for (Size i = 0; i < spectrum.getFloatDataArrays().size(); i++)
    {
      spectrum.getFloatDataArrays()[i].resize(peak_count);
    }
    for (Size i = 0; i < spectrum.getStringDataArrays().size(); i++)
    {
      spectrum.getStringDataArrays()[i].resize(peak_count);
    }
    for (Size i = 0; i < spectrum.getIntegerDataArrays().size(); i++)
    {
      spectrum.getIntegerDataArrays()[i].resize(peak_count);
    }
  }

  void OpenProXLUtils::wrap_(const String& input, Size width, String & output)
  {
    Size start = 0;

    while (start + width < input.size())
    {
      output += input.substr(start, width) + "\n";
      start += width;
    }

    if (start < input.size())
    {
      output += input.substr(start, input.size() - start) + "\n";
    }
  }

  String OpenProXLUtils::getxQuestBase64EncodedSpectrum(const PeakSpectrum& spec, String header)
  {
    vector<String> in_strings;
    StringList sl;

    double precursor_mz = spec.getPrecursors()[0].getMZ();
    double precursor_z = spec.getPrecursors()[0].getCharge();

    // header lines
    if (!header.empty()) // common or xlinker spectrum will be reported
    {
      sl.push_back(header + "\n"); // e.g. GUA1372-S14-A-LRRK2_DSS_1A3.03873.03873.3.dta,GUA1372-S14-A-LRRK2_DSS_1A3.03863.03863.3.dta
      sl.push_back(String(precursor_mz) + "\n");
      sl.push_back(String(precursor_z) + "\n");
    }
    else // light or heavy spectrum will be reported
    {
      sl.push_back(String(precursor_mz) + "\t" + String(precursor_z) + "\n");
    }

    // write peaks
    for (Size i = 0; i != spec.size(); ++i)
    {
      String s;
      s += String(spec[i].getMZ()) + "\t";
      s += String(spec[i].getIntensity()) + "\t";

      // add fragment charge if meta value exists (must be present for 'common' and 'xlinker'.
//      if (spec[i].metaValueExists("z"))
//      {
//        s += String(spec[i].getMetaValue("z"));
//      }
      s += "0";

      s += "\n";

      sl.push_back(s);
    }

    String out;
    out.concatenate(sl.begin(), sl.end(), "");
    in_strings.push_back(out);

    String out_encoded;
    Base64().encodeStrings(in_strings, out_encoded, false, false);
    String out_wrapped;
    wrap_(out_encoded, 76, out_wrapped);
    return out_wrapped;
  }

  vector<ResidueModification> OpenProXLUtils::getModificationsFromStringList(StringList modNames)
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

  void OpenProXLUtils::preprocessSpectraLabeled(PeakMap& exp)
  {
    // filter MS2 map
    // remove 0 intensities
    ThresholdMower threshold_mower_filter;
    threshold_mower_filter.filterPeakMap(exp);
    // TODO perl code filters by dynamic range (1000), meaning everything below max_intensity / 1000 is filtered out additionally to 0 int, before scaling / normalizing

    Normalizer normalizer;
    normalizer.filterPeakMap(exp);
    // TODO perl code scales to 0-100: int / max_int * 100

    // sort by rt
    exp.sortSpectra(false);

    // filter settings
//    WindowMower window_mower_filter;
//    Param filter_param = window_mower_filter.getParameters();
//    filter_param.setValue("windowsize", 100.0, "The size of the sliding window along the m/z axis.");
//    filter_param.setValue("peakcount", 20, "The number of peaks that should be kept.");
//    filter_param.setValue("movetype", "jump", "Whether sliding window (one peak steps or jumping window window size steps) should be used.");
//    window_mower_filter.setParameters(filter_param);
//    NLargest nlargest_filter = NLargest(250);   // De-noising in xQuest: Dynamic range = 1000, 250 most intense peaks?

#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (SignedSize exp_index = 0; exp_index < static_cast<SignedSize>(exp.size()); ++exp_index)
    {
//      // sort by mz
      exp[exp_index].sortByPosition();
      PeakSpectrum::IntegerDataArray charges;
      PeakSpectrum::StringDataArray annotations;
      charges.assign(exp[exp_index].size(), 0);
      annotations.assign(exp[exp_index].size(), "");
      charges.setName("charges");
      annotations.setName("annotations");
      exp[exp_index].getIntegerDataArrays().push_back(charges);
      exp[exp_index].getStringDataArrays().push_back(annotations);
//      nlargest_filter.filterSpectrum(exp[exp_index]);
//      window_mower_filter.filterPeakSpectrum(exp[exp_index]);
//      // sort (nlargest changes order)
//      exp[exp_index].sortByPosition();
     }
  }

  PeakSpectrum OpenProXLUtils::getToleranceWindowPeaks(PeakSpectrum spec, double mz, double tolerance, bool relative_tolerance)
  {
    PeakSpectrum peaks;
    double max_dist;

    if (relative_tolerance)
    {
      max_dist = mz + (mz * tolerance * 1e-6);
    } else
    {
      max_dist = tolerance;
    }

    bool inside = true;
    while (inside)
    {
      Size index = spec.findNearest(mz);
      double peak_mz = spec[index].getMZ();
      double dist = abs(mz - peak_mz);

      if (dist <= max_dist)
      {
        peaks.push_back(spec[index]);
        spec.erase(spec.begin() + index);
      } else
      {
        inside = false;
      }
    }
    return peaks;
  }

  void OpenProXLUtils::getSpectrumAlignment(std::vector<std::pair<Size, Size> > & alignment, const PeakSpectrum & s1, const PeakSpectrum & s2, double tolerance, bool relative_tolerance, double intensity_cutoff)
  {
    if (!s1.isSorted() || !s2.isSorted())
    {
      throw Exception::IllegalArgument(__FILE__, __LINE__, __PRETTY_FUNCTION__, "Input to SpectrumAlignment is not sorted!");
    }

    // clear result
    alignment.clear();
    //double tolerance = (double)param_.getValue("tolerance");

    if (!(relative_tolerance))
    {
      std::map<Size, std::map<Size, std::pair<Size, Size> > > traceback;
      std::map<Size, std::map<Size, double> > matrix;

      // init the matrix with "gap costs" tolerance
      matrix[0][0] = 0;
      for (Size i = 1; i <= s1.size(); ++i)
      {
        matrix[i][0] = i * tolerance;
        traceback[i][0]  = std::make_pair(i - 1, 0);
      }
      for (Size j = 1; j <= s2.size(); ++j)
      {
        matrix[0][j] = j * tolerance;
        traceback[0][j] = std::make_pair(0, j - 1);
      }

      // fill in the matrix
      Size left_ptr(1);
      Size last_i(0), last_j(0);

      //Size off_band_counter(0);
      for (Size i = 1; i <= s1.size(); ++i)
      {
        double pos1(s1[i - 1].getMZ());

        for (Size j = left_ptr; j <= s2.size(); ++j)
        {
          bool off_band(false);
          // find min of the three possible directions
          double pos2(s2[j - 1].getMZ());
          double diff_align = fabs(pos1 - pos2);

          // running off the right border of the band?
          if (pos2 > pos1 && diff_align >= tolerance)
          {
            if (i < s1.size() && j < s2.size() && s1[i].getMZ() < pos2)
            {
              off_band = true;
            }
          }

          // can we tighten the left border of the band?
          if (pos1 > pos2 && diff_align >= tolerance && j > left_ptr + 1)
          {
            ++left_ptr;
          }

          double score_align = diff_align;

          if (matrix.find(i - 1) != matrix.end() && matrix[i - 1].find(j - 1) != matrix[i - 1].end())
          {
            score_align += matrix[i - 1][j - 1];
          }
          else
          {
            score_align += (i - 1 + j - 1) * tolerance;
          }

          double score_up = tolerance;
          if (matrix.find(i) != matrix.end() && matrix[i].find(j - 1) != matrix[i].end())
          {
            score_up += matrix[i][j - 1];
          }
          else
          {
            score_up += (i + j - 1) * tolerance;
          }

          double score_left = tolerance;
          if (matrix.find(i - 1) != matrix.end() && matrix[i - 1].find(j) != matrix[i - 1].end())
          {
            score_left += matrix[i - 1][j];
          }
          else
          {
            score_left += (i - 1 + j) * tolerance;
          }

          // check for similar intensity values
          double intensity1(s1[i - 1].getIntensity());
          double intensity2(s2[j - 1].getIntensity());
          bool diff_int_clear = (min(intensity1, intensity2) / max(intensity1, intensity2)) > intensity_cutoff;

          // check for same charge
          PeakSpectrum::IntegerDataArray s1_charges = s1.getIntegerDataArrays()[0];
          PeakSpectrum::IntegerDataArray s2_charges = s2.getIntegerDataArrays()[0];
          bool charge_fits = s1_charges[i - 1] == s2_charges[j - 1] || s1_charges[i - 1] == 0 || s2_charges[j - 1] == 0;
          LOG_DEBUG << "s1 charge: " << s1_charges[i - 1] << " | s2 charge: " << s2_charges[j - 1] << endl;

          // TODO SET A CHARGE FOR EXPERIMENTAL SPECTRA, then use this again
//          bool charge_fits = true;

          if (score_align <= score_up && score_align <= score_left && diff_align < tolerance && diff_int_clear)
          {
            matrix[i][j] = score_align;
            traceback[i][j] = std::make_pair(i - 1, j - 1);
            last_i = i;
            last_j = j;
          }
          else
          {
            if (score_up <= score_left)
            {
              matrix[i][j] = score_up;
              traceback[i][j] = std::make_pair(i, j - 1);
            }
            else
            {
              matrix[i][j] = score_left;
              traceback[i][j] = std::make_pair(i - 1, j);
            }
          }

          if (off_band)
          {
            break;
          }
        }
      }

      // do traceback
      Size i = last_i;
      Size j = last_j;

      while (i >= 1 && j >= 1)
      {
        if (traceback[i][j].first == i - 1 && traceback[i][j].second == j - 1)
        {
          alignment.push_back(std::make_pair(i - 1, j - 1));
        }
        Size new_i = traceback[i][j].first;
        Size new_j = traceback[i][j].second;

        i = new_i;
        j = new_j;
      }

      std::reverse(alignment.begin(), alignment.end());

    }
    else  // relative alignment (ppm tolerance)
    {
      for (Size i = 0; i != s1.size(); ++i)
      {
        const double& theo_mz = s1[i].getMZ();
        double max_dist_dalton = theo_mz * tolerance * 1e-6;

        // iterate over peaks in experimental spectrum in given fragment tolerance around theoretical peak
        Size j = s2.findNearest(theo_mz);
        double exp_mz = s2[j].getMZ();

        // check for similar intensity values
        double intensity1(s1[i - 1].getIntensity());
        double intensity2(s2[j - 1].getIntensity());
        bool diff_int_clear = (min(intensity1, intensity2) / max(intensity1, intensity2)) > intensity_cutoff;

        // found peak match
        if (std::abs(theo_mz - exp_mz) <= max_dist_dalton && diff_int_clear)
        {
          alignment.push_back(std::make_pair(i, j));
        } else if (std::abs(theo_mz - exp_mz) <= max_dist_dalton && !diff_int_clear)
        {
//          s2.erase(s2.begin() + s2.findNearest(theo_mz));
          if (i > 0)
          {
            i--;
          }
        }
      }
    }
  }



  void OpenProXLUtils::getSpectrumIntensityMatching(std::vector<std::pair<Size, Size> > & alignment, const PeakSpectrum & s1, const PeakSpectrum & s2, double tolerance, bool relative_tolerance, double intensity_cutoff)
  {
    if (!(s1.isSorted() && s2.isSorted()))
    {
      throw Exception::IllegalArgument(__FILE__, __LINE__, __PRETTY_FUNCTION__, "Input to SpectrumAlignment is not sorted!");
    }
    PeakSpectrum spec1copy = s1;
    PeakSpectrum spec2copy = s2;

    // clear result
    alignment.clear();

    spec1copy.sortByIntensity(true);

    for (Size i = 0; i != spec1copy.size(); ++i)
    {
      const double& spec1copy_mz = spec1copy[i].getMZ();

      PeakSpectrum peaks = getToleranceWindowPeaks(spec2copy, spec1copy_mz, tolerance, relative_tolerance);
      if (peaks.size() > 1)
      {
        LOG_DEBUG << "SpectrumIntensityMatch: Peaks in tolerance window: " << peaks.size() << endl;
      }

      if (peaks.size() > 0)
      {
        peaks.sortByIntensity();
        double intensity1(spec1copy[i].getIntensity());

        for (Size j = 0; j < peaks.size(); ++j)
        {
          // check for similar intensity values
          double intensity2(peaks[j].getIntensity());
          double high_cutoff = 1 / intensity_cutoff;
          //bool diff_int_clear = (min(intensity1, intensity2) / max(intensity1, intensity2)) >= intensity_cutoff;
          bool diff_int_clear = (intensity1 / intensity2) >= intensity_cutoff && (intensity1 / intensity2) <= high_cutoff;

          // check for same charge
//          PeakSpectrum::IntegerDataArray s1_charges = s1.getIntegerDataArrays()[0];
//           PeakSpectrum::IntegerDataArray s2_charges = s2.getIntegerDataArrays()[0];
//           bool charge_fits = s1_charges[i - 1] == s2_charges[j - 1] || s2_charges[j - 1] == 0;
//           LOG_DEBUG << "s1 charge: " << s1_charges[i - 1] << " | s2 charge: " << s2_charges[j - 1] << endl;

          // TODO SET A CHARGE FOR EXPERIMENTAL SPECTRA, then use this again
          bool charge_fits = true;

          // found peak match. if intensity similar enough, update alignment and remove peak, so that it will not get matched again to another peak
          if (diff_int_clear && charge_fits)
          {
            Size s2_index = s2.findNearest(peaks[j].getMZ());
            Size s1_index = s1.findNearest(spec1copy_mz);
            alignment.push_back(std::make_pair(s1_index, s2_index));
            // remove peak from temporary spectrum copy to avoid multiple mappings
            Size spec2copy_index = spec2copy.findNearest(peaks[j].getMZ());
            spec2copy.erase(spec2copy.begin() + spec2copy_index);
            break;
          } else
          {
            // erase the most intense peak, because it does not fulfill the similarity or charge constraints and try with the rest of the peaks
            peaks.erase(peaks.begin() + j);
            --j;
          }
        }
      }
    }
  }

  std::vector<OpenProXLUtils::PeptideMass> OpenProXLUtils::digestDatabase(vector<FASTAFile::FASTAEntry> fasta_db, EnzymaticDigestion digestor, Size min_peptide_length, StringList cross_link_residue1, StringList cross_link_residue2, std::vector<ResidueModification> fixed_modifications, std::vector<ResidueModification> variable_modifications, Size max_variable_mods_per_peptide, Size count_proteins, Size count_peptides, bool n_term_linker, bool c_term_linker)
  {
    multimap<StringView, AASequence> processed_peptides;
    vector<OpenProXLUtils::PeptideMass> peptide_masses;
//#ifdef _OPENMP
//#pragma omp parallel for
//#endif
    // digest and filter database
    for (SignedSize fasta_index = 0; fasta_index < static_cast<SignedSize>(fasta_db.size()); ++fasta_index)
    {
//#ifdef _OPENMP
//#pragma omp atomic
//#endif
      ++count_proteins;

      // store vector of substrings pointing in fasta database (bounded by pairs of begin, end iterators)
      vector<StringView> current_digest;
      digestor.digestUnmodifiedString(fasta_db[fasta_index].sequence, current_digest, min_peptide_length);

      for (vector<StringView>::iterator cit = current_digest.begin(); cit != current_digest.end(); ++cit)
      {
        // skip peptides with invalid AAs // TODO is this necessary?
        if (cit->getString().has('B') || cit->getString().has('O') || cit->getString().has('U') || cit->getString().has('X') || cit->getString().has('Z')) continue;

        OpenProXLUtils::PeptidePosition position = OpenProXLUtils::INTERNAL;
        if (fasta_db[fasta_index].sequence.hasPrefix(cit->getString()))
        {
          position = OpenProXLUtils::N_TERM;
        } else if (fasta_db[fasta_index].sequence.hasSuffix(cit->getString()))
        {
          position = OpenProXLUtils::C_TERM;
        }

        // skip if no cross-linked residue
        bool skip = true;
        for (Size k = 0; k < cross_link_residue1.size(); k++)
        {
          if (cit->getString().find(cross_link_residue1[k]) < cit->getString().size()-1)
          {
            skip = false;
          }
          if (n_term_linker && position == OpenProXLUtils::N_TERM)
          {
            skip = false;
          }
          if (c_term_linker && position == OpenProXLUtils::C_TERM)
          {
            skip = false;
          }
        }
        for (Size k = 0; k < cross_link_residue2.size(); k++)
        {
          if (cit->getString().find(cross_link_residue2[k]) < cit->getString().size()-1)
          {
            skip = false;
          }
          if (n_term_linker && position == OpenProXLUtils::N_TERM)
          {
            skip = false;
          }
          if (c_term_linker && position == OpenProXLUtils::C_TERM)
          {
            skip = false;
          }
        }
        if (skip) continue;

        bool already_processed = false;
//#ifdef _OPENMP
//#pragma omp critical (processed_peptides_access)
//#endif
        {
          if (processed_peptides.find(*cit) != processed_peptides.end())
          {
            // peptide (and all modified variants) already processed so skip it
            already_processed = true;
          }
        }

        if (already_processed)
        {
          continue;
        }
//        if (cit->getString().find('K') >= cit->getString().size()-1)
//        {
//          continue;
//        }



//#ifdef _OPENMP
//#pragma omp atomic
//#endif
        ++count_peptides;

        vector<AASequence> all_modified_peptides;

        // generate all modified variants of a peptide
        // Note: no critial section is needed despite ResidueDB not beeing thread sage.
        //       It is only written to on introduction of novel modified residues. These residues have been already added above (single thread context).
        {
          AASequence aas = AASequence::fromString(cit->getString());
          ModifiedPeptideGenerator::applyFixedModifications(fixed_modifications.begin(), fixed_modifications.end(), aas);
          ModifiedPeptideGenerator::applyVariableModifications(variable_modifications.begin(), variable_modifications.end(), aas, max_variable_mods_per_peptide, all_modified_peptides);
        }

        for (SignedSize mod_pep_idx = 0; mod_pep_idx < static_cast<SignedSize>(all_modified_peptides.size()); ++mod_pep_idx)
        {
          const AASequence& candidate = all_modified_peptides[mod_pep_idx];
          OpenProXLUtils::PeptideMass pep_mass;
          pep_mass.peptide_mass = candidate.getMonoWeight();
          pep_mass.peptide_seq = candidate;
          pep_mass.position = position;

//#ifdef _OPENMP
//#pragma omp critical (processed_peptides_access)
//#endif
          {
            processed_peptides.insert(pair<StringView, AASequence>(*cit, candidate));
            peptide_masses.push_back(pep_mass);
          }
        }
      }
    }
    sort(peptide_masses.begin(), peptide_masses.end());
    return peptide_masses;
  }

}
