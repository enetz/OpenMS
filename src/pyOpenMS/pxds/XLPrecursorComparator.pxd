from Types cimport *
from libcpp cimport bool
from OPXLDataStructs cimport *
from XLPrecursor cimport *

cdef extern from "<OpenMS/ANALYSIS/XLMS/OPXLDataStructs.h>" namespace "OpenMS::OPXLDataStructs":
    
    cdef cppclass XLPrecursorComparator "OpenMS::OPXLDataStructs::XLPrecursorComparator":
        
        bool get "operator()"(XLPrecursor & a, XLPrecursor & b) nogil except +
        bool get "operator()"(XLPrecursor & a, double & b) nogil except +
        bool get "operator()"(double & a, XLPrecursor & b) nogil except +
 
