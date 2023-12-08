from Types cimport *
from OPXLDataStructs cimport *
from AASequence cimport *

cdef extern from "<OpenMS/ANALYSIS/XLMS/OPXLDataStructs.h>" namespace "OpenMS::OPXLDataStructs":

    cdef cppclass AASeqWithMass "OpenMS::OPXLDataStructs::AASeqWithMass":

        AASeqWithMass() except + nogil 
        AASeqWithMass(AASeqWithMass &) except + nogil  # compiler

        double peptide_mass
        AASequence peptide_seq
        PeptidePosition position
