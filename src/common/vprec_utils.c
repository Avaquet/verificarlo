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

#include "vprec_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void _vprec_write_hasmap(FILE *fout) {
  fprintf(fout, "<vprec_execution_profile>\n");
  for (int ii = 0; ii < _vprec_func_map->capacity; ii++) {
    if (get_value_at(_vprec_func_map->items, ii) != 0 &&
        get_value_at(_vprec_func_map->items, ii) != 0) {
      _vprec_inst_function_t *function =
          (_vprec_inst_function_t *)get_value_at(_vprec_func_map->items, ii);
      fprintf(fout, "\t<function>\n");

      fprintf(fout, "\t\t<id>%s</id>\n", function->std_info->id);
      fprintf(fout, "\t\t<is_lib>%hd</is_lib>\n", function->std_info->isLibraryFunction);
      fprintf(fout, "\t\t<is_intr>%hd</is_intr>\n", function->std_info->isIntrinsicFunction);
      fprintf(fout, "\t\t<use_float>%hd</use_float>\n", function->std_info->useFloat);
      fprintf(fout, "\t\t<use_double>%hd</use_double>\n", function->std_info->useDouble); 
      fprintf(fout, "\t\t<prec64>%d</prec64>\n", function->OpsPrec64);
      fprintf(fout, "\t\t<range64>%d</range64>\n", function->OpsRange64);
      fprintf(fout, "\t\t<prec32>%d</prec32>\n", function->OpsPrec32);
      fprintf(fout, "\t\t<range32>%d</range32>\n", function->OpsRange32);
      fprintf(fout, "\t\t<nb_input_args>%d</nb_input_args>\n", function->nb_input_args);
      fprintf(fout, "\t\t<nb_output_args>%d</nb_output_args>\n", function->nb_output_args);
      fprintf(fout, "\t\t<nb_calls>%d</nb_calls>\n", function->n_calls);

      /*
      fprintf(fout, "%s\t%hd\t%hd\t%hd\t%hd\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
              function->std_info->id, function->std_info->isLibraryFunction,
              function->std_info->isIntrinsicFunction, function->std_info->useFloat,
              function->std_info->useDouble, function->OpsPrec64, function->OpsRange64,
              function->OpsPrec32, function->OpsRange32,
              function->nb_input_args, function->nb_output_args,
              function->n_calls);
      */
      
      for (int i = 0; i < function->nb_input_args; i++) {
        fprintf(fout, "\t\t<input>\n");  

        fprintf(fout, "\t\t\t<arg_id>%s</arg_id>\n", function->input_args[i].arg_id);
        fprintf(fout, "\t\t\t<data_type>%hd</data_type>\n", function->input_args[i].data_type);
        fprintf(fout, "\t\t\t<mantissa_length>%d</mantissa_length>\n", function->input_args[i].mantissa_length);
        fprintf(fout, "\t\t\t<exponent_length>%d</exponent_length>\n", function->input_args[i].exponent_length);
        fprintf(fout, "\t\t\t<min_range>%d</min_range>\n", function->input_args[i].min_range);
        fprintf(fout, "\t\t\t<max_range>%d</max_range>\n", function->input_args[i].max_range);
         
       /* 
        fprintf(fout, "input:\t%s\t%hd\t%d\t%d\t%d\t%d\n",
                function->input_args[i].arg_id,
                function->input_args[i].data_type,
                function->input_args[i].mantissa_length,
                function->input_args[i].exponent_length,
                function->input_args[i].min_range,
                function->input_args[i].max_range); 
        */

        fprintf(fout, "\t\t</input>\n");  
      }
      for (int i = 0; i < function->nb_output_args; i++) {
        fprintf(fout, "\t\t<output>\n");  
       
        fprintf(fout, "\t\t\t<arg_id>%s</arg_id>\n", function->output_args[i].arg_id);
        fprintf(fout, "\t\t\t<data_type>%hd</data_type>\n", function->output_args[i].data_type);
        fprintf(fout, "\t\t\t<mantissa_length>%d</mantissa_length>\n", function->output_args[i].mantissa_length);
        fprintf(fout, "\t\t\t<exponent_length>%d</exponent_length>\n", function->output_args[i].exponent_length);
        fprintf(fout, "\t\t\t<min_range>%d</min_range>\n", function->output_args[i].min_range);
        fprintf(fout, "\t\t\t<max_range>%d</max_range>\n", function->output_args[i].max_range);
         
        /*
        fprintf(fout, "output:\t%s\t%hd\t%d\t%d\t%d\t%d\n",
                function->output_args[i].arg_id,
                function->output_args[i].data_type,
                function->output_args[i].mantissa_length,
                function->output_args[i].exponent_length,
                function->output_args[i].min_range,
                function->output_args[i].max_range);
        */
        
        fprintf(fout, "\t\t</output>\n");  
      }
      fprintf(fout, "\t</function>\n");
    }
  } 
  fprintf(fout, "</vprec_execution_profile>\n");
}

// Read and initialize the hashmap from the given file
void _vprec_read_hasmap(FILE *fin) {
  _vprec_inst_function_t function;
  function.std_info = malloc(sizeof(interflop_function_info_t));
  
  int binary64_precision, binary64_range, binary32_precision, binary32_range,
      type;
  fscanf(fin, "<vprec_execution_profile>\n");
  
  while(1){
    fscanf(fin, "\t<function>\n");
    function.std_info->id = malloc(sizeof(char)*256);
    if(fscanf(fin, "<id>%s</id>\n", function.std_info->id)){
      fprintf(stderr, "id = %s\n", function.std_info->id);
    }else{
      break;
    }
    fscanf(fin, "\t\t<is_lib>%hd</is_lib>\n", &function.std_info->isLibraryFunction);
    fscanf(fin, "\t\t<is_intr>%hd</is_intr>\n", &function.std_info->isIntrinsicFunction);
    fscanf(fin, "\t\t<use_float>%hd</use_float>\n", &function.std_info->useFloat);
    fscanf(fin, "\t\t<use_double>%hd</use_double>\n", &function.std_info->useDouble); 
    fscanf(fin, "\t\t<prec64>%d</prec64>\n", &function.OpsPrec64);
    fscanf(fin, "\t\t<range64>%d</range64>\n", &function.OpsRange64);
    fscanf(fin, "\t\t<prec32>%d</prec32>\n", &function.OpsPrec32);
    fscanf(fin, "\t\t<range32>%d</range32>\n", &function.OpsRange32);
    fscanf(fin, "\t\t<nb_input_args>%d</nb_input_args>\n", &function.nb_input_args);
    fscanf(fin, "\t\t<nb_output_args>%d</nb_output_args>\n", &function.nb_output_args);
    fscanf(fin, "\t\t<nb_calls>%d</nb_calls>\n", &function.n_calls);

    // allocate space for input arguments
    function.input_args =
        malloc(function.nb_input_args * sizeof(_vprec_argument_data_t));
    // allocate space for output arguments
    function.output_args =
        malloc(function.nb_output_args * sizeof(_vprec_argument_data_t));
  
    // get input arguments precision
    for (int i = 0; i < function.nb_input_args; i++) { 
      fscanf(fin, "\t\t<input>\n");  

      fscanf(fin, "\t\t\t<arg_id>%s</arg_id>\n", function.input_args[i].arg_id);
      fscanf(fin, "\t\t\t<data_type>%hd</data_type>\n", &function.input_args[i].data_type);
      fscanf(fin, "\t\t\t<mantissa_length>%d</mantissa_length>\n", &function.input_args[i].mantissa_length);
      fscanf(fin, "\t\t\t<exponent_length>%d</exponent_length>\n", &function.input_args[i].exponent_length);
      fscanf(fin, "\t\t\t<min_range>%d</min_range>\n", &function.input_args[i].min_range);
      fscanf(fin, "\t\t\t<max_range>%d</max_range>\n", &function.input_args[i].max_range);
       
      fscanf(fin, "\t\t</input>\n");  
    }

    // get output arguments precision
    for (int i = 0; i < function.nb_output_args; i++) {
      fscanf(fin, "\t\t<output>\n");  
      
      fscanf(fin, "\t\t\t<arg_id>%s</arg_id>\n", function.output_args[i].arg_id);
      fscanf(fin, "\t\t\t<data_type>%hd</data_type>\n", &function.output_args[i].data_type);
      fscanf(fin, "\t\t\t<mantissa_length>%d</mantissa_length>\n", &function.output_args[i].mantissa_length);
      fscanf(fin, "\t\t\t<exponent_length>%d</exponent_length>\n", &function.output_args[i].exponent_length);
      fscanf(fin, "\t\t\t<min_range>%d</min_range>\n", &function.output_args[i].min_range);
      fscanf(fin, "\t\t\t<max_range>%d</max_range>\n", &function.output_args[i].max_range);
     
      fscanf(fin, "\t\t</output>\n");  
    }
    
    // insert in the hashmap
    _vprec_inst_function_t *address = malloc(sizeof(_vprec_inst_function_t));
    (*address) = function;
    vfc_hashmap_insert(_vprec_func_map, vfc_hashmap_str_function(function.std_info->id),
                       address);
    
    fscanf(fin, "\t</function>\n");
  }

  fscanf(fin, "</vprec_execution_profile>\n");
 
  /*
  while (fscanf(fin, "%s\t%hd\t%hd\t%hd\t%hd\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                function.std_info->id, &function.std_info->isLibraryFunction,
                &function.std_info->isIntrinsicFunction, &function.std_info->useFloat,
                &function.std_info->useDouble, &function.OpsPrec64, &function.OpsRange64,
                &function.OpsPrec32, &function.OpsRange32,
                &function.nb_input_args, &function.nb_output_args,
                &function.n_calls) == 12) {
    // allocate space for input arguments
    function.input_args =
        malloc(function.nb_input_args * sizeof(_vprec_argument_data_t));
    // allocate space for output arguments
    function.output_args =
        malloc(function.nb_output_args * sizeof(_vprec_argument_data_t));

    // get input arguments precision
    for (int i = 0; i < function.nb_input_args; i++) {
      if (!fscanf(fin, "input:\t%s\t%hd\t%d\t%d\t%d\t%d\n",
                  function.input_args[i].arg_id,
                  &function.input_args[i].data_type,
                  &function.input_args[i].mantissa_length,
                  &function.input_args[i].exponent_length,
                  &function.input_args[i].min_range,
                  &function.input_args[i].max_range)) {
        logger_error("Can't read input arguments of %s\n", function.std_info->id);
      }
    }

    // get output arguments precision
    for (int i = 0; i < function.nb_output_args; i++) {
      if (!fscanf(fin, "output:\t%s\t%hd\t%d\t%d\t%d\t%d\n",
                  function.output_args[i].arg_id,
                  &function.output_args[i].data_type,
                  &function.output_args[i].mantissa_length,
                  &function.output_args[i].exponent_length,
                  &function.output_args[i].min_range,
                  &function.output_args[i].max_range)) {
        logger_error("Can't read output arguments of %s\n", function.std_info->id);
      }
    }

    // insert in the hashmap
    _vprec_inst_function_t *address = malloc(sizeof(_vprec_inst_function_t));
    (*address) = function;
    vfc_hashmap_insert(_vprec_func_map, vfc_hashmap_str_function(function.std_info->id),
                       address);
  }
  */
}

