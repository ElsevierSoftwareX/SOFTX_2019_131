// --------------------------------------------------------------------------
//
// This file is part of the NetOnZeroDXC software package.
//
// Version 1.0 - April 2019
//
//
// The NetOnZeroDXC package is free software; you can use it, redistribute it,
// and/or modify it under the terms of the GNU General Public License
// version 3 as published by the Free Software Foundation. The full text
// of the license can be found in the file LICENSE.txt at the top level of
// the package distribution.
//
// Authors:
//		Alessio Perinelli and Leonardo Ricci
//		Department of Physics, University of Trento
//		I-38123 Trento, Italy
//		alessio.perinelli@unitn.it
//		leonardo.ricci@unitn.it
//		https://github.com/LeonardoRicci/netOnZeroDXC
//
//
// If you use the NetOnZeroDXC package for your analyses, please cite:
//
//	A. Perinelli, D. E. Chiari and L. Ricci,
//	"Correlation in brain networks at different time scale resolution".
//	Chaos 28 (6):063127, 2018
//
// --------------------------------------------------------------------------

#include <cstdlib>
#include <cmath>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "omp.h"

#ifndef INCLUDED_ALGORITHM
	#include "netOnZeroDXC_algorithm.hpp"
	#define INCLUDED_ALGORITHM
#endif
#ifndef INCLUDED_IOFUNCTIONS
	#include "netOnZeroDXC_io.hpp"
	#define INCLUDED_IOFUNCTIONS
#endif

void netOnZeroDXC_xc_help (char *);
int netOnZeroDXC_xc_parse_options (int, char **, bool &, bool &, bool &, bool &, bool &, int &, int &, int &, int &, int &, int &, std::string &, std::string &, char &);
int netOnZeroDXC_xc_check_sequences (const std::vector < std::vector <double> > &, int, int, int, int &, int);

int main(int argc, char *argv[]) {

	bool	read_from_file = false;
	bool	write_to_file = false;
	bool	print_corr_diagram = false;
	bool	compute_pvalue_diagram = false;
	bool	enable_parallel_computing = false;
	int	index_a = -1, index_b = -1;
	int	apply_tau = -1;
	int	nr_window_widths = -1, window_basewidth = -1, nr_surrogates = 100;
	char	separator_char = 't';
	std::string	selected_input_filename;
	std::string	selected_output_filename;

	int error;
	error = netOnZeroDXC_xc_parse_options (argc, argv, read_from_file, write_to_file, print_corr_diagram, compute_pvalue_diagram, enable_parallel_computing, index_a, index_b,
					apply_tau, nr_window_widths, window_basewidth, nr_surrogates, selected_input_filename, selected_output_filename, separator_char);
	if (error)
		exit(1);

	std::vector < std::vector <double> > 	loaded_sequences;
	std::vector <std::string>		dummy_node_labels;

	if (read_from_file) {
		error = netOnZeroDXC_load_single_file(loaded_sequences, dummy_node_labels, selected_input_filename, separator_char);
		if (error == 2) {
			std::cerr << "ERROR: cannot read the selected file '" << selected_input_filename << "'.\n";
			exit(1);
		}
	} else {
		std::vector < std::vector <double> > 	temp_data_table;
		std::vector <double>			temp_sequence;
		netOnZeroDXC_load_stdin(temp_data_table, separator_char);
		int	i, j;
		for (i = 0; i < temp_data_table[0].size(); i++) {
			temp_sequence.clear();
			for (j = 0; j < temp_data_table.size(); j++) {
				temp_sequence.push_back(temp_data_table[j][i]);
			}
			loaded_sequences.push_back(temp_sequence);
		}
		error = netOnZeroDXC_check_linear_sizes(loaded_sequences);
	}
	if ((error == 3) || (error == 5)) {
		std::cerr << "ERROR: inconsistent sequences sizes found, or only one sequence detected.\n";
		exit(1);
	}
        error = netOnZeroDXC_xc_check_sequences(loaded_sequences, index_a, index_b, nr_window_widths, window_basewidth, apply_tau);
	if (error)
		exit(1);
	index_a--;
	index_b--;

	std::vector <double>	dummy_vector;
	int	k;
	if (apply_tau > 0) {
		for (k = nr_window_widths*window_basewidth / 2 - 1; k < loaded_sequences[index_a].size() - nr_window_widths*window_basewidth / 2 - apply_tau; k = k + window_basewidth)
			dummy_vector.push_back(0);
	} else {
		for (k = nr_window_widths*window_basewidth / 2 - 1; k < loaded_sequences[index_a].size() - nr_window_widths*window_basewidth / 2; k = k + window_basewidth)
			dummy_vector.push_back(0);
	}
	std::vector < std::vector <double> >	correlation_diagram_data(nr_window_widths, dummy_vector);
	std::vector < std::vector <double> >	p_value_diagram(nr_window_widths, dummy_vector);

	netOnZeroDXC_compute_cdiagram(correlation_diagram_data, loaded_sequences, index_a, index_b, window_basewidth, nr_window_widths, (apply_tau > 0)? true : false, apply_tau);

	if (print_corr_diagram) {
		if (write_to_file) {
			error = netOnZeroDXC_save_single_file(correlation_diagram_data, selected_output_filename, separator_char);
		} else {
			int	l;
			for (l = 0; l < nr_window_widths; l++) {
				std::cout << correlation_diagram_data[l][0];
				for (k = 1; k < correlation_diagram_data[l].size(); k++) {
					std::cout << separator_char << correlation_diagram_data[l][k];
				}
				std::cout << "\n";
			}
		}
		if (error) {
			std::cerr << "ERROR: i/o error when writing data on file '" << selected_output_filename << "'. Please check permissions.\n";
			exit(1);
		}
		exit(0);
	}

	std::vector <double>		values_distribution_a, values_distribution_b;
	std::vector <double>		fft_amplitudes_a, fft_amplitudes_b;

	netOnZeroDXC_initialize_surrogate_generation(values_distribution_a, fft_amplitudes_a, loaded_sequences, index_a);
	netOnZeroDXC_initialize_surrogate_generation(values_distribution_b, fft_amplitudes_b, loaded_sequences, index_b);

	unsigned int	seed = (unsigned int) clock();
	if (enable_parallel_computing) {
		#pragma omp parallel for schedule(dynamic)
		for (int i = 0; i < nr_surrogates; i++) {
			std::vector < std::vector <double> >	correlation_diagram_surrogates(nr_window_widths, dummy_vector);
			std::vector <double>		temp_surrogate_sequence_a;
			std::vector <double>		temp_surrogate_sequence_b;
			std::vector < std::vector <double> >	surrogates_container;
			surrogates_container.clear();
			temp_surrogate_sequence_a.clear();
			temp_surrogate_sequence_b.clear();
			seed = seed + 2*i;
			netOnZeroDXC_generate_surrogate_sequence(temp_surrogate_sequence_a, loaded_sequences, index_a, values_distribution_a, fft_amplitudes_a, TOLERANCE_SURROGATES, seed);
			seed++;
			netOnZeroDXC_generate_surrogate_sequence(temp_surrogate_sequence_b, loaded_sequences, index_b, values_distribution_b, fft_amplitudes_b, TOLERANCE_SURROGATES, seed);
			surrogates_container.push_back(temp_surrogate_sequence_a);
			surrogates_container.push_back(temp_surrogate_sequence_b);
			netOnZeroDXC_compute_cdiagram(correlation_diagram_surrogates, surrogates_container, 0, 1, window_basewidth, nr_window_widths, (apply_tau > 0)? true : false, apply_tau);
			#pragma omp critical
			{
				netOnZeroDXC_update_pdiagram (p_value_diagram, correlation_diagram_data, correlation_diagram_surrogates, nr_window_widths, nr_surrogates);
			}
		}
	} else {
		int	i;
		for (i = 0; i < nr_surrogates; i++) {
			std::vector < std::vector <double> >	correlation_diagram_surrogates(nr_window_widths, dummy_vector);
			std::vector <double>		temp_surrogate_sequence_a;
			std::vector <double>		temp_surrogate_sequence_b;
			std::vector < std::vector <double> >	surrogates_container;
			surrogates_container.clear();
			temp_surrogate_sequence_a.clear();
			temp_surrogate_sequence_b.clear();
			netOnZeroDXC_generate_surrogate_sequence(temp_surrogate_sequence_a, loaded_sequences, index_a, values_distribution_a, fft_amplitudes_a, TOLERANCE_SURROGATES, seed);
			seed++;
			netOnZeroDXC_generate_surrogate_sequence(temp_surrogate_sequence_b, loaded_sequences, index_b, values_distribution_b, fft_amplitudes_b, TOLERANCE_SURROGATES, seed);
			seed++;
			surrogates_container.push_back(temp_surrogate_sequence_a);
			surrogates_container.push_back(temp_surrogate_sequence_b);
			netOnZeroDXC_compute_cdiagram(correlation_diagram_surrogates, surrogates_container, 0, 1, window_basewidth, nr_window_widths, (apply_tau > 0)? true : false, apply_tau);
			netOnZeroDXC_update_pdiagram (p_value_diagram, correlation_diagram_data, correlation_diagram_surrogates, nr_window_widths, nr_surrogates);
		}
	}

	if (write_to_file) {
		error = netOnZeroDXC_save_single_file(p_value_diagram, selected_output_filename, separator_char);
	} else {
		int	l;
		for (l = 0; l < nr_window_widths; l++) {
			std::cout << p_value_diagram[l][0];
			for (k = 1; k < p_value_diagram[l].size(); k++) {
				std::cout << separator_char << p_value_diagram[l][k];
			}
			std::cout << "\n";
		}
	}
	if (error) {
		std::cerr << "ERROR: i/o error when writing data on file '" << selected_output_filename << "'. Please check permissions.\n";
		exit(1);
	}

	return 0;
}

void netOnZeroDXC_xc_help (char *program_name)
{
	std::cerr << "Usage:\n";
	std::cerr << "\t" << program_name << " -n <#> <#> -W <#> -L <#> (<Options>)\t<\t<vector stream>\n";
	std::cerr << "\nMandatory assignment:\n";
	std::cerr << "\t-n <#> <#>\tset column numbers of the two sequences to be analyzed;\n";
	std::cerr << "\t-W <#>\t\tset the number of window widths (rows of a correlation diagram);\n";
	std::cerr << "\t-L <#>\t\tset the base window width (in number of samples; if odd, will be reduced by 1).\n";

	std::cerr << "\nOptions:\n";
	std::cerr << "\t-C\t\tonly compute correlation diagram;\n";
	std::cerr << "\t-p\t\tcompute p value diagram by surrogate generation (default);\n";
	std::cerr << "\t-M <#>\t\tset the number of surrogates to be generated (default = 100);\n";
	std::cerr << "\t-tau <#>\tapply the delay of +/-tau points to assess zero-delay cross-correlation as the average of two delayed cross-correlations;\n";
	std::cerr << "\t-parallel\tenable parallel computing.\n";

	std::cerr << "\nInput/output:\n";
	std::cerr << "\t-i <fname>\tread from file 'fname' instead of standard input;\n";
	std::cerr << "\t-o <fname>\twrite to file 'fname' instead of standard output;\n";
	std::cerr << "\t-s <@>\t\tset column separator, default t (TAB); other options are s (space) or c (comma ',').\n";

	std::cerr << "\n\t-h or --help\tshow this help.\n";
}

int netOnZeroDXC_xc_parse_options (int argc, char *argv[], bool & read_from_file, bool & write_to_file, bool & print_corr_diagram, bool & compute_pvalue_diagram,
				bool & enable_parallel_computing, int & index_a, int & index_b, int & tau, int & W, int & L, int & M, std::string & input_filename,
				std::string & output_filename, char & separator_char)
{
	int	n = 1;
	while (n < argc) {
		if (strcmp(argv[n], "-n") == 0) {
			n++;
			index_a = atoi(argv[n]);
			n++;
			index_b = atoi(argv[n]);

		} else if (strcmp(argv[n], "-i") == 0) {
			read_from_file = true;
			n++;
			input_filename = argv[n];
		} else if (strcmp(argv[n], "-o") == 0) {
			write_to_file = true;
			n++;
			output_filename = argv[n];
		} else if (strcmp(argv[n], "-s") == 0) {
			n++;
			separator_char = argv[n][0];

		} else if (strcmp(argv[n], "-parallel") == 0) {
			enable_parallel_computing = true;

		} else if ((strcmp(argv[n], "-C") == 0) || (strcmp(argv[n], "-c") == 0)) {
			print_corr_diagram = true;
		} else if (strcmp( argv[n], "-p") == 0) {
			compute_pvalue_diagram = true;

		} else if( strcmp( argv[n], "-W" ) == 0 ) {
			n++;
			W = atoi(argv[n]);
		} else if( strcmp( argv[n], "-L" ) == 0 ) {
			n++;
			L = atoi(argv[n]);
		} else if( strcmp( argv[n], "-M" ) == 0 ) {
			n++;
			M = atoi(argv[n]);
		} else if( strcmp( argv[n], "-tau" ) == 0 ) {
			n++;
			tau = atoi(argv[n]);

		} else if ((strcmp("-h", argv[n]) == 0) || (strcmp("--help", argv[n]) == 0))  {
			netOnZeroDXC_xc_help(argv[0]);
			exit(0);
		}
		n++;
	}

	if (!compute_pvalue_diagram && !print_corr_diagram)
		compute_pvalue_diagram = true;
	else if (compute_pvalue_diagram && print_corr_diagram)
		print_corr_diagram = false;

	if ((index_a <= 0) || (index_b <= 0)) {
		std::cerr << "ERROR: column numbers were not correctly set. Use " << argv[0] << " -h for a list of options.\n";
		return 1;
	}
	if (W <= 0) {
		std::cerr << "ERROR: number of window widths was not correctly set. Use " << argv[0] << " -h for a list of options.\n";
		return 1;
	}
	if (L <= 0) {
		std::cerr << "ERROR: base width was not correctly set. Use " << argv[0] << " -h for a list of options.\n";
		return 1;
	}
	if (separator_char == 's') {
		separator_char = ' ';
	} else if (separator_char == 'c') {
		separator_char = ',';
	} else {
		separator_char = '\t';
	}

	return 0;
}

int netOnZeroDXC_xc_check_sequences (const std::vector < std::vector <double> > & sequences, int na, int nb, int W, int & L, int tau)
{
	if ((na > sequences.size()) || (nb > sequences.size())) {
		std::cerr << "ERROR: requested column numbers are larger than the number of loaded sequences.\n";
		return 1;
	}

	if ((floor((sequences[na].size() - L * W ) / L) - ((tau > 0)? tau : 0)) < 1) {
		std::cerr << "ERROR: windowing settings are invalid: negative diagram size expected.\n";
		return 1;
	}

	if (L % 2 != 0) {
		L = L - 1;
		std::cerr << "WARNING: window base width was an odd number; it is now reduced to " << L << ".\n";
	}

	return 0;
}
