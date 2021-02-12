/*****************************************************************************
 *                                                                           *
 *  This file is part of Verificarlo.                                        *
 *                                                                           *
 *  Copyright (c) 2015                                                       *
 *     Universite de Versailles St-Quentin-en-Yvelines                       *
 *     CMLA, Ecole Normale Superieure de Cachan                              *
 *  Copyright (c) 2019-2020                                                  *
 *     Verificarlo contributors                                              *
 *     Universite de Versailles St-Quentin-en-Yvelines                       *
 *                                                                           *
 *  Verificarlo is free software: you can redistribute it and/or modify      *
 *  it under the terms of the GNU General Public License as published by     *
 *  the Free Software Foundation, either version 3 of the License, or        *
 *  (at your option) any later version.                                      *
 *                                                                           *
 *  Verificarlo is distributed in the hope that it will be useful,           *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU General Public License for more details.                             *
 *                                                                           *
 *  You should have received a copy of the GNU General Public License        *
 *  along with Verificarlo.  If not, see <http://www.gnu.org/licenses/>.     *
 *                                                                           *
 *****************************************************************************/

// Changelog:
//
// 2021-02-12 Initial version from scratch
//

#ifndef __VPREC_UTILS_H__
#define __VPREC_UTILS_H__

#include <stdio.h>

#include "interflop.h"
#include "vfc_hashmap.h"
#include "logger.h"

// Hashmap for functions metadata
vfc_hashmap_t _vprec_func_map;

// Metadata of arguments
typedef struct _vprec_argument_data {
  // Identifier of the argument
  char arg_id[100];
  // Data type of the argument 0 is float and 1 is double
  short data_type;
  // Minimum rounded value of the argument
  int min_range;
  // Maximum rounded value of the argument
  int max_range;
  // Exponent length of the argument
  int exponent_length;
  // Mantissa length of the argument
  int mantissa_length;
} _vprec_argument_data_t;

// Metadata of function calls
typedef struct _vprec_inst_function {
  // standard function informations
  interflop_function_info_t *std_info;
  // Internal Operations Range64
  int OpsRange64;
  // Internal Operations Prec64
  int OpsPrec64;
  // Internal Operations Range32
  int OpsRange32;
  // Internal Operations Prec32
  int OpsPrec32;
  // Number of floating point input arguments
  int nb_input_args;
  // Array of data on input arguments
  _vprec_argument_data_t *input_args;
  // Number of floating point output arguments
  int nb_output_args;
  // Array of data on output arguments
  _vprec_argument_data_t *output_args;
  // Number of call for this call site
  int n_calls;
} _vprec_inst_function_t;

// Print str in vprec_lof_file with the correct offset
#define _vprec_print_log(_vprec_depth, _vprec_str, ...)                        \
  ({                                                                           \
    if (vprec_log_file != NULL) {                                              \
      for (int _vprec_d = 0; _vprec_d < _vprec_depth; _vprec_d++)              \
        fprintf(vprec_log_file, "\t");                                         \
      fprintf(vprec_log_file, _vprec_str, ##__VA_ARGS__);                      \
    }                                                                          \
  })


// Write the hashmap in the given file
void _vprec_write_hasmap(FILE *fout);


// Read and initialize the hashmap from the given file
void _vprec_read_hasmap(FILE *fin);

#endif
