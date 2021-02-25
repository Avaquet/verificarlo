#ifndef __INTERFLOP_H__
#define __INTERFLOP_H__

#include "stdarg.h"

/* interflop backend interface */

/* interflop float compare predicates, follows the same order than
 * LLVM's FCMPInstruction predicates */
enum FCMP_PREDICATE {
  FCMP_FALSE,
  FCMP_OEQ,
  FCMP_OGT,
  FCMP_OGE,
  FCMP_OLT,
  FCMP_OLE,
  FCMP_ONE,
  FCMP_ORD,
  FCMP_UNO,
  FCMP_UEQ,
  FCMP_UGT,
  FCMP_UGE,
  FCMP_ULT,
  FCMP_ULE,
  FCMP_UNE,
  FCMP_TRUE,
};

/* Enumeration of types managed by function instrumentation */
enum FTYPES { FFLOAT, FDOUBLE, FFLOAT_PTR, FDOUBLE_PTR, FTYPES_END };

/* Enumeration of the operations managed by verificarlo */
enum Fops { FOP_ADD, FOP_SUB, FOP_MUL, FOP_DIV, FOP_CMP, FOP_IGNORE };

/* Metadata for function call arguments */
typedef struct interflop_arg_info {
  // DataType of the argument
  enum FTYPES argType;
  // Mantissa length of the argument
  unsigned precision;
  // Exponent length of the argument
  unsigned range;
  // Size of the argument (1 if scalar, 0 if pointer with unknow size)
  unsigned argSize;
  // Name of the argument
  char *argName;
  // Minimum value
  unsigned minValue;
  // Maximum rounded value
  unsigned maxValue;
  // Mean rounded value
  unsigned meanValue;
} interflop_arg_info_t;

/* Metadata for function calls */
typedef struct interflop_function_info {
  // Name of the called function
  char *calledName;
  // Name of the library (none if not from library)
  char *libraryName;
} interflop_function_info_t;

/* Metadata for floating point operations */
typedef struct interflop_fops_info {
  // Type of fops
  enum Fops type;
  // DataType
  enum FTYPES dataType;
  // Size of the vector (1 without vectorization)
  unsigned vectorSize;
  // Mantissa length in bit
  unsigned precision;
  // Exponent length in bit
  unsigned range;
} interflop_fops_info_t;

/* Metadata for instructions */
typedef struct interflop_instruction_info {
  // The line of the instruction
  unsigned line;
  // The column of the instruction
  unsigned column;
  // The depth of the instruction (loop depth)
  unsigned depth;
  // Number of execution 
  unsigned nbExec;
  // Number of inputs
  unsigned nbInput;
  // Number of outputs
  unsigned nbOutput;
  // Indicate the identifier of the instruction
  char *id;
  // The path to the file that contains the instruction
  char *filePath;
  // The name of the function that contains the instruction
  char *funcName;
  // The id of the loop or none
  char *loopID;
  // Pointer to the fops informations (null if it is a call)
  interflop_fops_info_t *fopsInfo;
  // Pointer to the call informations (null if it is a fops)
  interflop_function_info_t *functionInfo;
  // Array of input args
  interflop_arg_info_t *inputArgs;
  // Array of ouput args
  interflop_arg_info_t *outputArgs;
} interflop_instruction_info_t;

/* Verificarlo call stack */
#define _VFC_CALL_STACK_MAXSIZE 4096

struct interflop_function_stack_st {
  /* array of function name or ID if instrumented */
  char **array;
  long int top;
};

typedef struct interflop_function_stack_st *interflop_function_stack_t;

/* Verificarlo hash map */
struct vfc_hashmap_st {
  size_t nbits;
  size_t mask;

  size_t capacity;
  size_t *items;
  size_t nitems;
  size_t n_deleted_items;
};

typedef struct vfc_hashmap_st *vfc_hashmap_t;

/* Verificarlo backend interface */
struct interflop_backend_interface_t {
  void (*interflop_add_float)(float a, float b, float *c, char *id,
                              void *context);
  void (*interflop_sub_float)(float a, float b, float *c, char *id,
                              void *context);
  void (*interflop_mul_float)(float a, float b, float *c, char *id,
                              void *context);
  void (*interflop_div_float)(float a, float b, float *c, char *id,
                              void *context);
  void (*interflop_cmp_float)(enum FCMP_PREDICATE p, float a, float b, int *c,
                              char *id, void *context);

  void (*interflop_add_double)(double a, double b, double *c, char *id,
                               void *context);
  void (*interflop_sub_double)(double a, double b, double *c, char *id,
                               void *context);
  void (*interflop_mul_double)(double a, double b, double *c, char *id,
                               void *context);
  void (*interflop_div_double)(double a, double b, double *c, char *id,
                               void *context);
  void (*interflop_cmp_double)(enum FCMP_PREDICATE p, double a, double b,
                               int *c, char *id, void *context);

  void (*interflop_enter_function)(char *id, void *context, int nb_args,
                                   va_list ap);

  void (*interflop_exit_function)(char *id, void *context, int nb_args,
                                  va_list ap);

  /* interflop_finalize: called at the end of the instrumented program
   * execution */
  void (*interflop_finalize)(void *context);
};

/* interflop_init: called at initialization before using a backend.
 * It returns an interflop_backend_interface_t structure with callbacks
 * for each of the numerical instrument hooks.
 *
 * argc: number of arguments passed to the backend
 *
 * argv: arguments passed to the backend, argv[0] always contains the name of
 * the backend library. argv[] may be deallocated after the call to
 * interflop_init. To make it persistent, a backend must copy it.
 *
 * context: the backend is free to make this point to a backend-specific
 * context. The frontend will pass the context back as the last argument of the
 * above instrumentation hooks.
 * */

struct interflop_backend_interface_t
interflop_init(int argc, char **argv, interflop_function_stack_t call_stack,
               vfc_hashmap_t inst_map, void **context);

#endif
