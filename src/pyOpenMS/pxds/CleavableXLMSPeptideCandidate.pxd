from Types cimport *
from OPXLDataStructs cimport *
#from AASequence cimport *
from AASeqWithMass cimport *

cdef extern from "<OpenMS/ANALYSIS/XLMS/OPXLDataStructs.h>" namespace "OpenMS::OPXLDataStructs":

    cdef cppclass CleavableXLMSPeptideCandidate "OpenMS::OPXLDataStructs::CleavableXLMSPeptideCandidate":

        CleavableXLMSPeptideCandidate() nogil except + # compiler
        CleavableXLMSPeptideCandidate(CleavableXLMSPeptideCandidate &) nogil except + # compiler # compiler

        const AASeqWithMass *peptide;
        int first_peak_index;
        int second_peak_index;
        double first_peak_pep_error;
        double second_peak_pep_error;