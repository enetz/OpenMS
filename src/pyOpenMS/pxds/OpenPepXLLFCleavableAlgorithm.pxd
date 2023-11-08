from Types cimport *
from libcpp.vector cimport vector as libcpp_vector
from libcpp.pair cimport pair as libcpp_pair
from DefaultParamHandler cimport *
from ProgressLogger cimport *
from FASTAFile cimport *
from PreprocessedPairSpectra cimport *
from CrossLinkSpectrumMatch cimport *
from MSExperiment cimport *

cdef extern from "<OpenMS/ANALYSIS/XLMS/OpenPepXLLFCleavableAlgorithm.h>" namespace "OpenMS":

    cdef cppclass OpenPepXLLFCleavableAlgorithm(DefaultParamHandler) :
        # wrap-inherits:
        #  DefaultParamHandler
        # wrap-doc:
        #  Search for cross-linked peptide pairs in tandem MS spectra
        OpenPepXLLFCleavableAlgorithm() nogil except +
        OpenPepXLLFCleavableAlgorithm(OpenPepXLLFCleavableAlgorithm &) nogil except + # compiler

        OpenPepXLLFCleavableAlgorithm_ExitCodes run(MSExperiment& unprocessed_spectra,
                                           libcpp_vector[ FASTAEntry ]& fasta_db,
                                           libcpp_vector[ ProteinIdentification ]& protein_ids,
                                           libcpp_vector[ PeptideIdentification ]& peptide_ids,
                                           libcpp_vector[ libcpp_vector[ CrossLinkSpectrumMatch ] ]& all_top_csms,
                                           MSExperiment& spectra) nogil except +
            # wrap-doc:
                #  Performs the main function of this class, the search for cross-linked peptides, with CID-cleavable cross-linkers
                #  
                #  
                #  :param unprocessed_spectra: The input PeakMap of experimental spectra
                #  :param fasta_db: The protein database containing targets and decoys
                #  :param protein_ids: A result vector containing search settings. Should contain one PeptideIdentification
                #  :param peptide_ids: A result vector containing cross-link spectrum matches as PeptideIdentifications and PeptideHits. Should be empty
                #  :param all_top_csms: A result vector containing cross-link spectrum matches as CrossLinkSpectrumMatches. Should be empty. This is only necessary for writing out xQuest type spectrum files
                #  :param spectra: A result vector containing the input spectra after preprocessing and filtering. Should be empty. This is only necessary for writing out xQuest type spectrum files

cdef extern from "<OpenMS/ANALYSIS/XLMS/OpenPepXLLFCleavableAlgorithm.h>" namespace "OpenMS::OpenPepXLLFCleavableAlgorithm":
    cdef enum OpenPepXLLFCleavableAlgorithm_ExitCodes "OpenMS::OpenPepXLLFCleavableAlgorithm::ExitCodes":
        #wrap-attach:
        #   OpenPepXLLFCleavableAlgorithm
        EXECUTION_OK
        ILLEGAL_PARAMETERS
        UNEXPECTED_RESULT
        INCOMPATIBLE_INPUT_DATA
