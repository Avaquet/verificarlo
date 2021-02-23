/******************************************************************************
 *                                                                            *
 *  This file is part of Verificarlo.                                         *
 *                                                                            *
 *  Copyright (c) 2020                                                        *
 *     Verificarlo contributors                                               *
 *                                                                            *
 *  Verificarlo is free software: you can redistribute it and/or modify       *
 *  it under the terms of the GNU General Public License as published by      *
 *  the Free Software Foundation, either version 3 of the License, or         *
 *  (at your option) any later version.                                       *
 *                                                                            *
 *  Verificarlo is distributed in the hope that it will be useful,            *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 *  GNU General Public License for more details.                              *
 *                                                                            *
 *  You should have received a copy of the GNU General Public License         *
 *  along with Verificarlo.  If not, see <http://www.gnu.org/licenses/>.      *
 *                                                                            *
 ******************************************************************************/

#define _VFC_PROFILE_FILENAME "vfc_profile.xml"
#define _VFC_BUFFER_SIZE 256
#define _VFC_INST_OFFSET 7
#define N_DATA_PER_ARG 3

// return true if the file exists
inline bool exists(const char *name) {
  struct stat buffer;
  return (stat(name, &buffer) == 0);
}

/************************************************************
 *                       Hash Functions                     *
 ************************************************************/
static char buffer[_VFC_BUFFER_SIZE];

static char *unsigned_to_string(unsigned value) {
  sprintf(buffer, "%u", value);

  return buffer;
}

static char *signed_to_string(int value) {
  sprintf(buffer, "%d", value);

  return buffer;
}

// Add a function in the hash table
void vfc_inst_table_add(vfc_hashmap_t _vfc_inst_map,
                        interflop_instruction_info_t *instruction) {
  size_t key = vfc_hashmap_str_function(instruction->id);

  vfc_hashmap_insert(_vfc_inst_map, key, instruction);
}

// Search a function in the hash table
interflop_instruction_info_t *vfc_func_table_get(vfc_hashmap_t _vfc_inst_map,
                                                 const char *id) {
  size_t key = vfc_hashmap_str_function(id);

  return vfc_hashmap_get(_vfc_inst_map, key);
}

void vfc_write_arg(xmlTextWriterPtr writer, interflop_arg_info_t *arg) {
  xmlTextWriterWriteElement(writer, "name", arg->argName);
  xmlTextWriterWriteElement(writer, "size", unsigned_to_string(arg->argSize));
  xmlTextWriterWriteElement(writer, "type", unsigned_to_string(arg->argType));
  xmlTextWriterWriteElement(writer, "precision",
                            unsigned_to_string(arg->precision));
  xmlTextWriterWriteElement(writer, "range", unsigned_to_string(arg->range));
}

void vfc_write_call(xmlTextWriterPtr writer, interflop_function_info_t *call) {
  xmlTextWriterWriteElement(writer, "name", call->calledName);
  xmlTextWriterWriteElement(writer, "library", call->libraryName);

  xmlTextWriterWriteElement(writer, "nb_input",
                            unsigned_to_string(call->nbInput));

  for (int i = 0; i < call->nbInput; i++) {
    xmlTextWriterStartElement(writer, "input");
    vfc_write_arg(writer, &(call->inputArgs[i]));
    xmlTextWriterEndElement(writer);
  }

  xmlTextWriterWriteElement(writer, "nb_output",
                            unsigned_to_string(call->nbOutput));

  for (int i = 0; i < call->nbOutput; i++) {
    xmlTextWriterStartElement(writer, "output");
    vfc_write_arg(writer, &(call->outputArgs[i]));
    xmlTextWriterEndElement(writer);
  }
}

void vfc_write_fops(xmlTextWriterPtr writer, interflop_fops_info_t *fops) {
  xmlTextWriterWriteElement(writer, "type", unsigned_to_string(fops->type));
  xmlTextWriterWriteElement(writer, "data_type",
                            unsigned_to_string(fops->dataType));
  xmlTextWriterWriteElement(writer, "vector_size",
                            unsigned_to_string(fops->vectorSize));
  xmlTextWriterWriteElement(writer, "precision",
                            unsigned_to_string(fops->precision));
  xmlTextWriterWriteElement(writer, "range", unsigned_to_string(fops->range));
}

void vfc_write_instruction(xmlTextWriterPtr writer,
                           interflop_instruction_info_t *instruction) {
  if (instruction->fopsInfo != NULL) {
    xmlTextWriterStartElement(writer, "fops");
  } else {
    xmlTextWriterStartElement(writer, "call");
  }

  xmlTextWriterWriteElement(writer, "id", instruction->id);
  xmlTextWriterWriteElement(writer, "filepath", instruction->filePath);
  xmlTextWriterWriteElement(writer, "function", instruction->funcName);
  xmlTextWriterWriteElement(writer, "line",
                            unsigned_to_string(instruction->line));
  xmlTextWriterWriteElement(writer, "column",
                            unsigned_to_string(instruction->column));
  xmlTextWriterWriteElement(writer, "loop", instruction->loopID);
  xmlTextWriterWriteElement(writer, "depth",
                            unsigned_to_string(instruction->depth));

  if (instruction->fopsInfo != NULL) {
    vfc_write_fops(writer, instruction->fopsInfo);
  } else {
    vfc_write_call(writer, instruction->functionInfo);
  }

  xmlTextWriterEndElement(writer);
}

void vfc_inst_table_write(vfc_hashmap_t _vfc_inst_map, const char *filename) {
  if (!exists(_VFC_PROFILE_FILENAME)) {
    return;
  }

  // build the xml writer
  xmlTextWriterPtr writer = xmlNewTextWriterFilename(filename, 0);
  xmlTextWriterSetIndent(writer, 4);
  // start the xml document
  xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
  // start vprec_execution_profile
  xmlTextWriterStartElement(writer, "profile");

  for (int ii = 0; ii < _vfc_inst_map->capacity; ii++) {
    if (get_value_at(_vfc_inst_map->items, ii) != 0) {
      interflop_instruction_info_t *instruction =
          (interflop_instruction_info_t *)get_value_at(_vfc_inst_map->items,
                                                       ii);

      vfc_write_instruction(writer, instruction);
    }
  }

  // end vprec_execution_profile
  xmlTextWriterEndElement(writer);

  // end the xml document
  xmlTextWriterEndDocument(writer);
  // free the xml writer
  xmlFreeTextWriter(writer);
}

char *get_string(xmlNode *node, int offset) {
  for (int i = 0; i < offset; i++) {
    node = node->next;
  }

  return (char *)xmlNodeGetContent(node);
}

unsigned get_int(xmlNode *node, int offset) {
  for (int i = 0; i < offset; i++) {
    node = node->next;
  }

  return atoi((char *)xmlNodeGetContent(node));
}

xmlNode *get_node(xmlNode *node, int offset) {
  for (int i = 0; i < offset; i++) {
    node = node->next;
  }

  return node;
}

void vfc_init_args(xmlNode *node, interflop_arg_info_t *arg) {
  arg->argName = get_string(node->children, 0);
  arg->argSize = get_int(node->children, 1);
  arg->argType = get_int(node->children, 2);
  arg->precision = get_int(node->children, 3);
  arg->range = get_int(node->children, 4);
}

void vfc_init_fops(xmlNode *node, interflop_fops_info_t *fops) {
  fops->type = get_int(node->children, _VFC_INST_OFFSET);
  fops->dataType = get_int(node->children, _VFC_INST_OFFSET + 1);
  fops->vectorSize = get_int(node->children, _VFC_INST_OFFSET + 2);
  fops->precision = get_int(node->children, _VFC_INST_OFFSET + 3);
  fops->range = get_int(node->children, _VFC_INST_OFFSET + 4);
}

void vfc_init_call(xmlNode *node, interflop_function_info_t *call) {
  call->calledName = get_string(node->children, _VFC_INST_OFFSET);
  call->libraryName = get_string(node->children, _VFC_INST_OFFSET + 1);

  call->nbInput = get_int(node->children, _VFC_INST_OFFSET + 2);
  call->inputArgs = (call->nbInput)
                        ? malloc(call->nbInput * sizeof(interflop_arg_info_t))
                        : NULL;

  for (int i = 0; i < call->nbInput; i++) {
    vfc_init_args(get_node(node->children, _VFC_INST_OFFSET + 3 + i),
                  &(call->inputArgs[i]));
  }

  call->nbOutput =
      get_int(node->children, _VFC_INST_OFFSET + 3 + call->nbInput);
  call->outputArgs = (call->nbOutput)
                         ? malloc(call->nbOutput * sizeof(interflop_arg_info_t))
                         : NULL;

  for (int i = 0; i < call->nbOutput; i++) {
    vfc_init_args(
        get_node(node->children, _VFC_INST_OFFSET + 4 + call->nbInput + i),
        &(call->outputArgs[i]));
  }
}

void vfc_init_instruction(xmlNode *node,
                          interflop_instruction_info_t *instruction) {
  if (strcmp((char *)node->name, "call") == 0) {
    instruction->fopsInfo = NULL;
    instruction->functionInfo = malloc(sizeof(interflop_function_info_t));
    vfc_init_call(node, instruction->functionInfo);
  } else if (strcmp((char *)node->name, "fops") == 0) {
    instruction->functionInfo = NULL;
    instruction->fopsInfo = malloc(sizeof(interflop_fops_info_t));
    vfc_init_fops(node, instruction->fopsInfo);
  } else {
    fprintf(stderr, "Unknown xml node type !\n");
  }

  instruction->id = get_string(node->children, 0);
  instruction->filePath = get_string(node->children, 1);
  instruction->funcName = get_string(node->children, 2);
  instruction->line = get_int(node->children, 3);
  instruction->column = get_int(node->children, 4);
  instruction->loopID = get_string(node->children, 5);
  instruction->depth = get_int(node->children, 6);
}

// Read and initialize the hashmap from the given file
void vfc_inst_table_read(vfc_hashmap_t _vfc_inst_map, const char *filename) {
  if (!exists(_VFC_PROFILE_FILENAME)) {
    logger_info("The profile file of verificarlo cannot be found");
    return;
  }

  // build the document
  xmlKeepBlanksDefault(0);
  xmlDoc *doc = xmlReadFile(filename, NULL, 0);

  // get the root element
  xmlNode *inst_node = xmlDocGetRootElement(doc)->children;

  // iterate over instruction nodes
  while (inst_node) {
    // allocate space for instruction informations
    interflop_instruction_info_t *instruction =
        malloc(sizeof(interflop_instruction_info_t));
    // fill the structure
    vfc_init_instruction(inst_node, instruction);
    // add it to the hasmap
    vfc_inst_table_add(_vfc_inst_map, instruction);

    inst_node = inst_node->next;
  }

  // free the document
  xmlFreeDoc(doc);
  // free the parser
  xmlCleanupParser();
}

vfc_hashmap_t vfc_inst_table_init() {
  vfc_hashmap_t map = vfc_hashmap_create();

  vfc_inst_table_read(map, _VFC_PROFILE_FILENAME);

  return map;
}

void vfc_inst_table_quit(vfc_hashmap_t map) {
  // vfc_inst_table_write(map, _VFC_PROFILE_FILENAME);

  vfc_hashmap_free(map);

  vfc_hashmap_destroy(map);
}

/************************************************************
 *                       Call Stack                         *
 ************************************************************/
// Initialize the call stack
interflop_function_stack_t vfc_call_stack_create() {
  interflop_function_stack_t stack = malloc(sizeof(interflop_function_stack_t));

  stack->top = _VFC_CALL_STACK_MAXSIZE;
  stack->array = malloc(_VFC_CALL_STACK_MAXSIZE * sizeof(char *));
  stack->array[--stack->top] = NULL;

  return stack;
}

// Free the call stack
void vfc_call_stack_destroy(interflop_function_stack_t stack) {
  if (stack->array) {
    free(stack->array);
  }

  free(stack);
}

// Push a function in the call stack
void vfc_call_stack_push(interflop_function_stack_t stack, char *function) {
  if (stack == NULL) {
    logger_error("Call stack not initialized,  please check compilation flags "
                 "(--inst-func)\n");
    return;
  }

  if (stack->top == 0) {
    logger_error("Call stack is full, it max size is %zu\n",
                 _VFC_CALL_STACK_MAXSIZE);
    return;
  }

  stack->array[--stack->top] = function;
}

// Remove a function in the call stack
char *vfc_call_stack_pop(interflop_function_stack_t stack) {
  if (stack == NULL) {
    logger_error("Call stack not initialized,  please check compilation flags "
                 "(--inst-func)\n");
    return NULL;
  }

  if (stack->top < _VFC_CALL_STACK_MAXSIZE)
    return stack->array[stack->top++];

  return NULL;
}

// Print the call stack
void vfc_call_stack_print(interflop_function_stack_t stack, FILE *f) {
  for (int i = _VFC_CALL_STACK_MAXSIZE - 2; i >= stack->top; i--)
    fprintf(f, "%s/", stack->array[i]);
  fprintf(f, "\n");
}

/************************************************************
 *                  Enter and Exit functions                *
 ************************************************************/

// Function called before each function's call of the code
void vfc_enter_function(char *func_name, char *func_id, unsigned n, ...) {
  vfc_call_stack_push(interflop_call_stack, func_name);

  if (func_id != NULL) {
    va_list ap;

    // n is the number of arguments intercepted
    va_start(ap, n * N_DATA_PER_ARG);

    for (int i = 0; i < loaded_backends; i++)
      if (backends[i].interflop_enter_function)
        backends[i].interflop_enter_function(func_id, contexts[i], n, ap);

    va_end(ap);
  }
}

// Function called after each function's call of the code
void vfc_exit_function(char *func_name, char *func_id, unsigned n, ...) {
  if (func_id != NULL) {
    va_list ap;
    // n is the number of arguments intercepted
    va_start(ap, n * N_DATA_PER_ARG);

    for (int i = 0; i < loaded_backends; i++)
      if (backends[i].interflop_exit_function)
        backends[i].interflop_exit_function(func_id, contexts[i], n, ap);

    va_end(ap);
  }

  vfc_call_stack_pop(interflop_call_stack);
}
