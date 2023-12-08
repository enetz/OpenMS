from Types cimport *
from OPXLDataStructs cimport *
from CleavableXLMSPeptideCandidate cimport CleavableXLMSPeptideCandidate

cdef extern from "<OpenMS/ANALYSIS/XLMS/OPXLDataStructs.h>" namespace "OpenMS::OPXLDataStructs":

    cdef cppclass XLCPrecursor "OpenMS::OPXLDataStructs::XLCPrecursor":

        XLCPrecursor(XLCPrecursor &) nogil except + # compiler
        XLCPrecursor(double set_mass,
                    const CleavableXLMSPeptideCandidate *set_alpha,
                    const CleavableXLMSPeptideCandidate *set_beta) nogil except + # compiler

        double precursor_mass;
        const CleavableXLMSPeptideCandidate *alpha;
        const CleavableXLMSPeptideCandidate *beta;

