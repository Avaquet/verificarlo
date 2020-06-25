/************************************************************
*                      Global variables                     *
************************************************************/

#define table_size 100
#define HASH_MULTIPLIER 31

/* input file variables */
static char* vconfig_input_filename;
static char vprec_input_used = 1;

/* output file variables */
static char* vconfig_output_filename;
static char vprec_output_used = 1;

/************************************************************
*                       Hash Table                          *
************************************************************/

typedef struct interflop_function_array
{
  interflop_function_info_t* functions;
  int size;
  int index;  
}interflop_function_array_t;

static interflop_function_array_t function_table[table_size];

// Add a function at a table index
interflop_function_info_t* _vfc_add_function(interflop_function_array_t *array, interflop_function_info_t function)
{
  if(array->index >= array->size){
    array->size += 10;
    array->functions = realloc(array->functions, array->size * sizeof(interflop_function_info_t));
  }

  array->functions[array->index++] = function;

  return &array->functions[array->index-1];
}

// Delete a function at a table index
interflop_function_info_t* _vfc_del_function(interflop_function_array_t *array)
{
  if (array->index != 0)
    array->index--;

  if (array->index != 0)
    return &array->functions[array->index-1];

  return NULL;
}

// Search a function in a table index
interflop_function_info_t* _vfc_get_function(interflop_function_array_t *array, const char* id)
{
  for (int i = 0; i < array->index; i++){
    if (strcmp(array->functions[i].id, id) == 0){
      return &array->functions[i];
    }
  }

  return NULL;
}

// Hash function
int _vfc_hash_function(const char* id)
{
  unsigned const char *us;

  us = (unsigned const char *) id;

  unsigned long long int index = 0;

  while(*us != '\0') {
    index = index * HASH_MULTIPLIER + *us;
    us++;
  }

  return index % table_size;  
}

// Add a function in the hash table
interflop_function_info_t* _vfc_table_add(interflop_function_info_t function)
{
  int index = _vfc_hash_function(function.id);

  return _vfc_add_function(&function_table[index], function);
}

// Search a function in the table
interflop_function_info_t* _vfc_table_get(const char* id)
{
  int index = _vfc_hash_function(id);

  return _vfc_get_function(&function_table[index], id);
}

// Read and initialize the table from a file
void _vfc_init_table()
{ 
  FILE* fin = fopen(vconfig_input_filename, "r");

  if(fin != NULL)
  {
    char buffer[4096];
    interflop_function_info_t function;

    function.isLibraryFunction = false;
    function.isIntrinsicFunction = false;

    while(fgets(buffer, 4096, fin) != 0)
    {
      if (buffer[0] == '#')
      {
        // comment nothing to do 
      }else if (sscanf(buffer, "%s\t%c\t%c\t%c\t%c\t%d\t%d\t%d\t%d\t%d\n", 
                function.id,
                &function.isLibraryFunction,
                &function.isIntrinsicFunction,
                &function.haveFloat,
                &function.haveDouble,
                &function.binary64_precision,
                &function.binary64_range, 
                &function.binary32_precision,
                &function.binary32_range, 
                &function.n_calls)){

        _vfc_table_add(function);

      }else{
        logger_error("Precision input file can't be loaded correctly\n");
        abort();
      }
    }

    fclose(fin);
  }else{
    logger_error("Precision input file can't be found\n");
    abort();    
  }
}

// Print the list at an index of the table
void _vfc_print_array(FILE *f, interflop_function_array_t *array)
{
  for (int j = 0; j < array->index; j++)
      fprintf(f,"%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",               \
                                array->functions[j].id,                   \
                                array->functions[j].isLibraryFunction,     \
                                array->functions[j].isIntrinsicFunction,   \
                                array->functions[j].haveFloat,             \
                                array->functions[j].haveDouble,            \
                                array->functions[j].binary64_precision,   \
                                array->functions[j].binary64_range,       \
                                array->functions[j].binary32_precision,   \
                                array->functions[j].binary32_range,       \
                                array->functions[j].n_calls);
}

// Print the table 
void _vfc_print_table(FILE *f)
{
  for (int i = 0; i < table_size; i++)
    _vfc_print_array(f, &function_table[i]);
}

/************************************************************
*                       Call Stack                          *
************************************************************/
static interflop_function_stack_t call_stack;

// Print the call stack
void _vfc_print_call_stack(FILE *f)
{
  for(int i = 0; i < call_stack.index; i++)
      fprintf(f, "%s/", call_stack.functions[i]->id);
  fprintf(f, "\n");
}

// Push a function in the call stack
interflop_function_info_t* _vfc_push_function(interflop_function_info_t *function)
{
  if(call_stack.index >= call_stack.size){
    call_stack.size += 10;
    call_stack.functions = realloc(call_stack.functions, call_stack.size * sizeof(interflop_function_info_t*));
  }

  call_stack.functions[call_stack.index++] = function;

  return call_stack.functions[call_stack.index];
}

// Remove a function in the call stack
interflop_function_info_t* _vfc_pop_function()
{
  if (call_stack.index != 0)
    call_stack.index--;

  if (call_stack.index != 0)
    return call_stack.functions[call_stack.index];

  return NULL;
}

// Free the call stack and the table
void _vfc_free_table_and_stack()
{
  for (int i = 0; i < table_size; i++)
    free(function_table[i].functions);

  free(call_stack.functions);
}

/************************************************************
*                  Enter and Exit functions                 *
************************************************************/

// Function called before each function's call of the code
void vfc_enter_function(char* func_name, 
                        char isLibraryFunction,
                        char isIntrinsicFunction,
                        char haveFloat,
                        char haveDouble,
                        int n, ...)
{
  // initalisation de la structure de donnée
  interflop_function_info_t* function = _vfc_table_get(func_name);

  if (function == NULL){
    interflop_function_info_t f = {"", 
                                  isLibraryFunction, 
                                  isIntrinsicFunction,
                                  haveFloat,
                                  haveDouble, 
                                  52, 
                                  11, 
                                  23, 
                                  8, 
                                  1};
    strncpy(f.id, func_name, 500);
    function = _vfc_table_add(f);
  }else{
    function->isLibraryFunction = isLibraryFunction;
    function->isIntrinsicFunction = isIntrinsicFunction;
    function->n_calls++;
  }

  _vfc_push_function(function);

  va_list ap;
  va_start(ap, n*2);

  for(int i = 0; i < loaded_backends; i++)
    backends[i].interflop_enter_function(&call_stack, n, ap);

  va_end(ap);
}

// Function called after each function's call of the code
void vfc_exit_function(char* func_name,
                        char isLibraryFunction,
                        char isIntrinsicFunction,
                        char haveFloat,
                        char haveDouble,
                        int n, ...)
{
  va_list ap;
  va_start(ap, n*2);

  for(int i = 0; i < loaded_backends; i++)
    backends[i].interflop_exit_function(&call_stack, n, ap);

  va_end(ap);

  _vfc_pop_function();
}


/************************************************************
*                   Init and Quit functions                 *
************************************************************/

void vfc_init_func_inst()
{
  // check if precision input file is given 
  vconfig_input_filename = getenv("VFC_PREC_INPUT");
  if (vconfig_input_filename == NULL){
    vprec_input_used = 0;
  }

  // check if profile output file is given 
  vconfig_output_filename = getenv("VFC_PREC_OUTPUT");
  if (vconfig_output_filename == NULL){
    vprec_output_used = 0;
  }

  if (vprec_input_used){
    _vfc_init_table();
  }

  vfc_enter_function("main", 0, 0, 1, 1, 0);
}

void vfc_quit_func_inst()
{
  if (vprec_output_used){
    FILE* f = fopen(vconfig_output_filename, "w");
    if (f != NULL){
      _vfc_print_table(f);
      fclose(f);
    }else{
      logger_error("Cannot create the output file");
    }

  }

  vfc_exit_function("main", 0, 0, 1, 1, 0);

  _vfc_free_table_and_stack();
}