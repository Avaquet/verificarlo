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
#include <libxml/parser.h>
#include <libxml/xmlwriter.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 256
#define N_FUNCTION_DATA 12

static char buffer[BUFFER_SIZE];

static char *int_to_string(int value) {
  sprintf(buffer, "%d", value);

  return buffer;
}

static char *short_to_string(short value) {
  sprintf(buffer, "%hd", value);

  return buffer;
}

void _vprec_write_hasmap(const char *filename) {
  // build the xml writer
  xmlTextWriterPtr writer = xmlNewTextWriterFilename(filename, 0);
  // start the xml document
  xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);

  // start vprec_execution_profile
  xmlTextWriterStartElement(writer, "vprec_execution_profile");

  for (int ii = 0; ii < _vprec_func_map->capacity; ii++) {
    if (get_value_at(_vprec_func_map->items, ii) != 0 &&
        get_value_at(_vprec_func_map->items, ii) != 0) {
      _vprec_inst_function_t *function =
          (_vprec_inst_function_t *)get_value_at(_vprec_func_map->items, ii);

      // start function
      xmlTextWriterStartElement(writer, "function");

      xmlTextWriterWriteElement(writer, "id", function->std_info->id);
      xmlTextWriterWriteElement(
          writer, "is_lib",
          short_to_string(function->std_info->isLibraryFunction));
      xmlTextWriterWriteElement(
          writer, "is_intr",
          short_to_string(function->std_info->isIntrinsicFunction));
      xmlTextWriterWriteElement(writer, "use_float",
                                short_to_string(function->std_info->useFloat));
      xmlTextWriterWriteElement(writer, "use_double",
                                short_to_string(function->std_info->useDouble));
      xmlTextWriterWriteElement(writer, "prec64",
                                int_to_string(function->OpsPrec64));
      xmlTextWriterWriteElement(writer, "range64",
                                int_to_string(function->OpsRange64));
      xmlTextWriterWriteElement(writer, "prec32",
                                int_to_string(function->OpsPrec32));
      xmlTextWriterWriteElement(writer, "range32",
                                int_to_string(function->OpsRange32));
      xmlTextWriterWriteElement(writer, "nb_input_args",
                                int_to_string(function->nb_input_args));
      xmlTextWriterWriteElement(writer, "nb_output_args",
                                int_to_string(function->nb_output_args));
      xmlTextWriterWriteElement(writer, "nb_calls",
                                int_to_string(function->n_calls));

      for (int i = 0; i < function->nb_input_args; i++) {
        // start input
        xmlTextWriterStartElement(writer, "input");

        xmlTextWriterWriteElement(writer, "arg_id",
                                  function->input_args[i].arg_id);
        xmlTextWriterWriteElement(
            writer, "data_type",
            short_to_string(function->input_args[i].data_type));
        xmlTextWriterWriteElement(
            writer, "mantissa_length",
            int_to_string(function->input_args[i].mantissa_length));
        xmlTextWriterWriteElement(
            writer, "exponent_length",
            int_to_string(function->input_args[i].exponent_length));
        xmlTextWriterWriteElement(
            writer, "min_range",
            int_to_string(function->input_args[i].min_range));
        xmlTextWriterWriteElement(
            writer, "max_range",
            int_to_string(function->input_args[i].max_range));

        // end input
        xmlTextWriterEndElement(writer);
      }
      for (int i = 0; i < function->nb_output_args; i++) {
        // start input
        xmlTextWriterStartElement(writer, "output");

        xmlTextWriterWriteElement(writer, "arg_id",
                                  function->output_args[i].arg_id);
        xmlTextWriterWriteElement(
            writer, "data_type",
            short_to_string(function->output_args[i].data_type));
        xmlTextWriterWriteElement(
            writer, "mantissa_length",
            int_to_string(function->output_args[i].mantissa_length));
        xmlTextWriterWriteElement(
            writer, "exponent_length",
            int_to_string(function->output_args[i].exponent_length));
        xmlTextWriterWriteElement(
            writer, "min_range",
            int_to_string(function->output_args[i].min_range));
        xmlTextWriterWriteElement(
            writer, "max_range",
            int_to_string(function->output_args[i].max_range));

        // end output
        xmlTextWriterEndElement(writer);
      }

      // end function
      xmlTextWriterEndElement(writer);
    }
  }

  // end vprec_execution_profile
  xmlTextWriterEndElement(writer);

  // end the xml document
  xmlTextWriterEndDocument(writer);
  // free the xml writer
  xmlFreeTextWriter(writer);
}

static xmlNode *get_node(xmlNode *node, int offset) {
  for (int i = 0; i < offset; i++) {
    node = node->next;
  }

  return node;
}

static char *get_string(xmlNode *node, int offset) {
  for (int i = 0; i < offset; i++) {
    node = node->next;
  }

  return xmlNodeGetContent(node);
}

static int get_int(xmlNode *node, int offset) {
  for (int i = 0; i < offset; i++) {
    node = node->next;
  }

  return atoi(xmlNodeGetContent(node));
}

// Read and initialize the hashmap from the given file
void _vprec_read_hasmap(const char *filename) {
  // build the document
  xmlDoc *doc = xmlReadFile(filename, NULL, 0);
  // get the root element
  xmlNode *root = xmlDocGetRootElement(doc);

  // allocate space for the function structure
  _vprec_inst_function_t function;
  function.std_info = malloc(sizeof(interflop_function_info_t));

  // iterate over functions
  xmlNode *node_function = root->children;
  while (node_function) {
    // allocate space for the id
    function.std_info->id = malloc(sizeof(char) * BUFFER_SIZE);
    strcpy(function.std_info->id, get_string(node_function->children, 0));
    function.std_info->isLibraryFunction = get_int(node_function->children, 1);
    function.std_info->isIntrinsicFunction =
        get_int(node_function->children, 2);
    function.std_info->useFloat = get_int(node_function->children, 3);
    function.std_info->useDouble = get_int(node_function->children, 4);
    function.OpsPrec64 = get_int(node_function->children, 5);
    function.OpsRange64 = get_int(node_function->children, 6);
    function.OpsPrec32 = get_int(node_function->children, 7);
    function.OpsRange32 = get_int(node_function->children, 8);
    function.nb_input_args = get_int(node_function->children, 9);
    function.nb_output_args = get_int(node_function->children, 10);
    function.n_calls = get_int(node_function->children, 11);

    // allocate space for input arguments
    function.input_args =
        malloc(function.nb_input_args * sizeof(_vprec_argument_data_t));
    // allocate space for output arguments
    function.output_args =
        malloc(function.nb_output_args * sizeof(_vprec_argument_data_t));

    // iterate over input args
    for (int i = 0; i < function.nb_input_args; i++) {
      xmlNode *arg = get_node(node_function->children, N_FUNCTION_DATA + i);

      strcpy(function.input_args[i].arg_id, get_string(arg->children, 0));
      function.input_args[i].data_type = get_int(arg->children, 1);
      function.input_args[i].mantissa_length = get_int(arg->children, 2);
      function.input_args[i].exponent_length = get_int(arg->children, 3);
      function.input_args[i].min_range = get_int(arg->children, 4);
      function.input_args[i].max_range = get_int(arg->children, 5);
    }

    // iterate over output args
    for (int i = 0; i < function.nb_output_args; i++) {
      xmlNode *arg = get_node(node_function->children,
                              N_FUNCTION_DATA + function.nb_input_args + i);

      strcpy(function.output_args[i].arg_id, get_string(arg->children, 0));
      function.output_args[i].data_type = get_int(arg->children, 1);
      function.output_args[i].mantissa_length = get_int(arg->children, 2);
      function.output_args[i].exponent_length = get_int(arg->children, 3);
      function.output_args[i].min_range = get_int(arg->children, 4);
      function.output_args[i].max_range = get_int(arg->children, 5);
    }

    // insert in the hashmap
    _vprec_inst_function_t *address = malloc(sizeof(_vprec_inst_function_t));
    (*address) = function;
    vfc_hashmap_insert(_vprec_func_map,
                       vfc_hashmap_str_function(function.std_info->id),
                       address);

    node_function = node_function->next;
  }

  // free the document
  xmlFreeDoc(doc);
  // free the parser
  xmlCleanupParser();
}
