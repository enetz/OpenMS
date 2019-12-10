// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2018.
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
// $Authors: Timo Sachsenberg, Eugen Netz $
// --------------------------------------------------------------------------

#include <OpenMS/CHEMISTRY/Tagger.h>
#include <OpenMS/CHEMISTRY/ResidueDB.h>
#include <OpenMS/CHEMISTRY/Residue.h>
#include <OpenMS/CHEMISTRY/ModificationsDB.h>
#include <OpenMS/MATH/MISC/MathFunctions.h>

#ifdef _OPENMP
#include <omp.h>
#define NUMBER_OF_THREADS (omp_get_num_threads())
#else
#define NUMBER_OF_THREADS (1)
#endif

namespace OpenMS
{
  char Tagger::getAAByMass_(double m) const
  {
    // fast check for border cases
    if (m < min_gap_ || m > max_gap_) return ' ';

    const double delta = Math::ppmToMass(ppm_, m);
    auto left = mass2aa.lower_bound(m - delta);
    //if (left == mass2aa.end()) return ' '; // cannot happen, since we checked boundaries above

    if (fabs(left->first - m) < delta) return left->second;
    return ' ';
  }

  void Tagger::getTag_(std::string & tag, const std::vector<double>& mzs, const size_t i, std::vector<std::string>& tags) const
  {
    const size_t N = mzs.size();
    size_t j = i + 1;
    while (j < N)
    {
      if (tag.size() == max_tag_length_) { return; } // maximum tag size reached? - continue with next parent

      const double gap = mzs[j] - mzs[i];
      if ((gap) > max_gap_) { return; } // already too far away - continue with next parent

      // consider all charges for the next residue
      for (Size charge = min_charge_; charge <= max_charge_; ++charge)
      {
        if ((gap * charge) > max_gap_) { break; } // already too far away - higher charges won't fit either
        const char aa = getAAByMass_(gap * charge);

        if (aa == ' ') { continue; } // can't extend tag
        tag += aa;
        getTag_(tag, mzs, j, tags);

        if (tag.size() >= min_tag_length_)
        {
#pragma omp critical (tags_access)
          tags.push_back(tag);
        }

        // if aa is "L", then also add "I" as an alternative residue and extend the tag again
        // this will add redundancy, (and redundant runtime) but we avoid dealing with J and ambigous matching to I and L later on
        if (aa == 'L')
        {
          tag.pop_back();
          tag.push_back('I');
          getTag_(tag, mzs, j, tags);
          if (tag.size() >= min_tag_length_)
          {
#pragma omp critical (tags_access)
            tags.push_back(tag);
          }
        }
        tag.pop_back();  // remove last string
      }
      ++j;
    }
  }

  Tagger::Tagger(size_t min_tag_length, double ppm, size_t max_tag_length, size_t min_charge, size_t max_charge, const StringList& fixed_mods, const StringList& var_mods)
  {
    ppm_ = ppm;
    min_tag_length_ = min_tag_length;
    max_tag_length_ = max_tag_length;
    min_charge_ = min_charge;
    max_charge_ = max_charge;

    const std::set<const Residue*> aas = ResidueDB::getInstance()->getResidues("Natural19WithoutI");

    for (const auto& r : aas)
    {
      const char letter = r->getOneLetterCode()[0];
      const double mass = r->getMonoWeight(Residue::Internal);
      mass2aa[mass] = letter;
    }

    // for fixed modifications, replace the unmodified residue with the modified one
    for (const auto& mod : fixed_mods)
    {
      const ResidueModification* rm = ModificationsDB::getInstance()->getModification(mod);
      Residue r = *(ResidueDB::getInstance()->getResidue(rm->getOrigin()));
      r.setModification(rm->getId());

      // remove the unmodified residue
      // this requires searching the map by value, but this is only done once when the Tagger is initialized
      for (std::map<double, char>::iterator it = mass2aa.begin(); it != mass2aa.end(); ++it)
      {
        if (it->second == rm->getOrigin())
        {
          mass2aa.erase(it);
          break;
        }
      }
      const char name = rm->getOrigin();
      const double mass = r.getMonoWeight(Residue::Internal);
      mass2aa[mass] = name;
    }

    // for variable modifications, add an additional instance of the residue with the modified mass to the list
    for (const auto& mod : var_mods)
    {
      const ResidueModification* rm = ModificationsDB::getInstance()->getModification(mod);
      Residue r = *(ResidueDB::getInstance()->getResidue(rm->getOrigin()));
      r.setModification(rm->getId());
      const char name = rm->getOrigin();
      const double mass = r.getMonoWeight(Residue::Internal);
      mass2aa[mass] = name;
    }

    min_gap_ = mass2aa.begin()->first - Math::ppmToMass(ppm, mass2aa.begin()->first);
    max_gap_ = mass2aa.rbegin()->first + Math::ppmToMass(ppm, mass2aa.rbegin()->first);
  }

  void Tagger::getTag(const std::vector<double>& mzs, std::vector<std::string>& tags) const
  {
    // start peak
    if (min_tag_length_ > mzs.size()) return; // avoid segfault

#ifdef _OPENMP
#pragma omp parallel for schedule(guided)
#endif
      for (int i = 0; i < mzs.size() - min_tag_length_; ++i)
      {
        std::string tag;
        getTag_(tag, mzs, i, tags);
      } // end of parallelized loop over starting peaks

    // make tags unique
    sort(tags.begin(), tags.end());
    auto last_unique_tag = unique(tags.begin(), tags.end());
    if (last_unique_tag != tags.end())
    {
      tags.erase(last_unique_tag, tags.end());
    }
  }

  void Tagger::getTag(const MSSpectrum& spec, std::vector<std::string>& tags) const
  {
    const size_t N = spec.size();
    if (N < min_tag_length_) { return; }
    // copy to double vector (speed)
    std::vector<double> mzs;
    mzs.reserve(N);
    for (auto const& p : spec) { mzs.push_back(p.getMZ()); }
    getTag(mzs, tags);
  }
  void Tagger::setMaxCharge(size_t max_charge)
  {
    max_charge_ = max_charge;
  }
}
