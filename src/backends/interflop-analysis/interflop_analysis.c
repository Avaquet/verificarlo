/*****************************************************************************
 *                                                                           *
 *  This file is part of Verificarlo.                                        *
 *                                                                           *
 *  Copyright (c) 2015                                                       *
 *     Universite de Versailles St-Quentin-en-Yvelines                       *
 *     CMLA, Ecole Normale Superieure de Cachan                              *
 *  Copyright (c) 2018                                                       *
 *     Universite de Versailles St-Quentin-en-Yvelines                       *
 *  Copyright (c) 2018-2020                                                  *
 *     Verificarlo contributors                                              *
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

#include <argp.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <float.h>

#include "../../common/float_const.h"
#include "../../common/float_struct.h"
#include "../../common/float_utils.h"
#include "../../common/interflop.h"
#include "../../common/logger.h"
#include "../../common/vfc_hashmap.h"
#include "../../common/options.h"
#include "../../common/tinymt64.h"

#include "../../common/float_const.h"
#include "../../common/tinymt64.h"


/*****************************************************************************
 *                                Backend Data                               *
 *****************************************************************************/

static interflop_function_stack_t _vfc_call_stack = NULL;
static vfc_hashmap_t _vfc_inst_map = NULL;
static FILE *analysis_output_file = NULL;
static char *analysis_file_name = NULL;

typedef enum {
  analysis_add = '+',
  analysis_sub = '-',
  analysis_mul = '*',
  analysis_div = '/',
} analysis_operation;

/*****************************************************************************
 *                         Numerical Error Analysis                          *
 *****************************************************************************/

static inline interflop_instruction_info_t *get_inst_info(char *id)
{
  // if the operation is not identified
  if (id == NULL) {
    logger_error("The fops %s cannot be found in the map", id);
  }
 
  // get the fops metadata
  interflop_instruction_info_t *inst_info =
      vfc_hashmap_get(_vfc_inst_map, vfc_hashmap_str_function(id));

  // if the operation is not identified
  if (inst_info == NULL || inst_info->fopsInfo == NULL) {
    logger_error("The fops %s cannot be found in the map", id);
  }

  return inst_info;
}

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define isAdd(op, a, b)  ((op == analysis_add) || (op == analysis_sub && b < 0)) 
#define isSub(op, a, b)  ((op == analysis_sub) || (op == analysis_add && b < 0))

/* perform_bin_op: applies the binary operator (op) to (a) and (b) */
/* and stores the result in (res) */
#define perform_binary_op(op, res, a, b)                                       \
  switch (op) {                                                                \
  case analysis_add:                                                           \
    res = (a) + (b);                                                           \
    break;                                                                     \
  case analysis_mul:                                                           \
    res = (a) * (b);                                                           \
    break;                                                                     \
  case analysis_sub:                                                           \
    res = (a) - (b);                                                           \
    break;                                                                     \
  case analysis_div:                                                           \
    res = (a) / (b);                                                           \
    break;                                                                     \
  default:                                                                     \
    logger_error("invalid operator %c", op);                                   \
  };

#define cancell(X, Y, Z, info)                                            \
  ({                                                                      \
    const int32_t e_z = GET_EXP_FLT(*Z);                                  \
                                                                          \
    int cancellation = max(GET_EXP_FLT(X), GET_EXP_FLT(Y)) - e_z;         \
   })                                                                     \

#define absorb(X, Y, Z, info)                                             \
  ({                                                                      \
    const int32_t e_z = GET_EXP_FLT(*Z);                                  \
                                                                          \
    int32_t delta_exp = abs(min(GET_EXP_FLT(X),GET_EXP_FLT(Y))-e_z); \
    int32_t delta_bit = GET_LSB(min(X,Y)) - GET_PMAN_SIZE(*Z);            \
    int32_t absorption = delta_exp + delta_bit;                          \
    absorption = (absorption > 0) ? absorption : 0;                       \
   })                                                                     \

#define roundof(op, X, Y, Z, info)                                        \
  ({                                                                      \
    __float128 X128 = X;                                                  \
    __float128 Y128 = Y;                                                  \
    __float128 Z128 = *Z;                                                  \
    __float128 NEW_Z128;                                                  \
    perform_binary_op(op, NEW_Z128, X, Y);                               \
    __float128 DELTA128 = NEW_Z128 - Z128;                                \
    double delta64 = (DELTA128 > 0) ? DELTA128 : -DELTA128;               \
    })                                                                    \

#define analyse(precision, op, X, Y, Z, id)                               \
  ({                                                                      \
    interflop_instruction_info_t *inst_info = get_inst_info(id);          \
                                                                          \
    if (isAdd(op, X, Y)){                                                 \
      absorb(X, Y, Z, inst_info);                                         \
    }                                                                     \
                                                                          \
    if (isSub(op, X, Y)){                                                 \
      cancell(X, Y, Z, inst_info);                                        \
    }                                                                     \
                                                                          \
    roundof(op, X, Y, Z, inst_info);                                      \
    })                                                                    \

/*****************************************************************************
 *                         Floating Point Operations                         *
 *****************************************************************************/

static void _interflop_add_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = a + b;

  analyse(32, analysis_add, a, b, c, id);
}

static void _interflop_sub_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = a - b;
  
  analyse(32, analysis_sub, a, b, c, id);
}

static void _interflop_add_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = a + b;
  
  analyse(64, analysis_add, a, b, c, id);
}

static void _interflop_sub_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = a - b;
  
  analyse(64, analysis_sub, a, b, c, id);
}

static void _interflop_mul_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = a * b;

  analyse(32, analysis_mul, a, b, c, id);
}

static void _interflop_div_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = a / b;
  
  analyse(32, analysis_div, a, b, c, id);
}

static void _interflop_mul_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = a * b;

  analyse(64, analysis_mul, a, b, c, id);
}

static void _interflop_div_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = a / b;
  
  analyse(64, analysis_div, a, b, c, id);
}

/*****************************************************************************
 *                    Backend finalization functions                         *
 *****************************************************************************/

void _interflop_finalize(void *context) {
  if (analysis_output_file != stdout){
    fclose(analysis_output_file);
  }
}

/*****************************************************************************
 *                    Backend initialization functions                       *
 *****************************************************************************/

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
  char *tmp_filename = NULL;
  FILE *tmp_file = NULL;
  
  switch (key) {
    case 'o': 
      tmp_filename = arg;
      tmp_file = fopen(tmp_filename, "wr");
      
      if (tmp_file){
        analysis_output_file = tmp_file;
        analysis_file_name = tmp_filename;
      }else{
        logger_error("The output file %s cannot be found\n", tmp_filename);
      }
      break;
    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp_option options[] = {
    {"output-file", 'o', "OUTPUT-FILE", 0, "Set the output file", 0},
    {0}};

static struct argp argp = {options, parse_opt, "", "", NULL, NULL, NULL};

struct interflop_backend_interface_t
interflop_init(int argc, char **argv, interflop_function_stack_t call_stack,
               vfc_hashmap_t inst_map, void **context) {

  logger_init();

  if (inst_map == NULL){
    logger_error("This backend cannot work without the compiler profile\n");
  }
 
  _vfc_inst_map = inst_map;
  _vfc_call_stack = call_stack;
  analysis_output_file = stdout;
  analysis_file_name = "stdout";

  /* parse backend arguments */
  argp_parse(&argp, argc, argv, 0, 0, NULL);

  logger_info("interflop_analysis: loaded backend with output_file = %s\n", 
      analysis_file_name);

  struct interflop_backend_interface_t interflop_backend_cancellation = {
      _interflop_add_float,
      _interflop_sub_float,
      _interflop_mul_float,
      _interflop_div_float,
      NULL,
      _interflop_add_double,
      _interflop_sub_double,
      _interflop_mul_double,
      _interflop_div_double,
      NULL,
      NULL,
      NULL,
      _interflop_finalize};

  return interflop_backend_cancellation;
}


#if 0
typedef struct {
  bool choose_seed;
  uint64_t seed;
} t_context;

/* define default environment variables and default parameters */
#define TOLERANCE_DEFAULT 1
#define WARNING_DEFAULT 0

static int WARN = WARNING_DEFAULT;
static int TOLERANCE = TOLERANCE_DEFAULT;
static vfc_hashmap_t _vfc_inst_map;

#define max(a, b) ((a) > (b) ? (a) : (b))

static void _set_tolerance(int tolerance) { TOLERANCE = tolerance; }

static void _set_warning(bool warning) { WARN = warning; }

/* random generator internal state */
static tinymt64_t random_state;

/* Set the mca seed */
static void _set_mca_seed(const bool choose_seed, const uint64_t seed) {
  _set_seed_default(&random_state, choose_seed, seed);
}

static double _mca_rand(void) {
  /* Returns a random double in the (0,1) open interval */
  return tinymt64_generate_doubleOO(&random_state);
}

/* noise = rand * 2^(exp) */
static inline double _noise_binary64(const int exp) {
  const double d_rand = (_mca_rand() - 0.5);
  return _fast_pow2_binary64(exp) * d_rand;
}

/* cancell: detects the cancellation size; and checks if its larger than the
 * chosen tolerance. It reports a warning to the user and adds a MCA noise of
 * the magnitude of the cancelled bits. */
#define cancell(X, Y, Z, file, line, column)                                   \
  ({                                                                           \
    const int32_t e_z = GET_EXP_FLT(*Z);                                       \
    /* computes the difference between the max of both operands and the        \
     * exponent of the result to find the size of the cancellation */          \
    int cancellation = max(GET_EXP_FLT(X), GET_EXP_FLT(Y)) - e_z;              \
    if (cancellation >= TOLERANCE) {                                           \
      if (WARN) {                                                              \
        logger_info("cancellation of size %d detected in %s at line %u column %u\n", cancellation, file, line, column);       \
      }                                                                        \
      /* Add an MCA noise of the magnitude of cancelled bits.                  \
       * This particular version in the case of cancellations does not use     \
       * extended quad types */                                                \
      const int32_t e_n = e_z - (cancellation - 1);                            \
      *Z = *Z + _noise_binary64(e_n);                                          \
    }                                                                          \
  })

/* Cancellations can only happen during additions and substractions */
static void _interflop_add_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = a + b;

  if (id != NULL && _vfc_inst_map != NULL){
    // get the fops metadata
    interflop_instruction_info_t *inst_info =
        vfc_hashmap_get(_vfc_inst_map, vfc_hashmap_str_function(id));

    // if the operation is not identified
    if (inst_info == NULL || inst_info->fopsInfo == NULL) {
      logger_error("The fops %s cannot be found in the map", id);
    }
    
    cancell(a, b, c, inst_info->filePath, inst_info->line, inst_info->column);
          
  }else{
    cancell(a, b, c, "unknown location", 0, 0);
  } 
}

static void _interflop_sub_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = a - b;
 
  if (id != NULL && _vfc_inst_map != NULL){
    // get the fops metadata
    interflop_instruction_info_t *inst_info =
        vfc_hashmap_get(_vfc_inst_map, vfc_hashmap_str_function(id));

    // if the operation is not identified
    if (inst_info == NULL || inst_info->fopsInfo == NULL) {
      logger_error("The fops %s cannot be found in the map", id);
    }
    
    cancell(a, b, c, inst_info->filePath, inst_info->line, inst_info->column);
          
  }else{
    cancell(a, b, c, "unknown location", 0, 0);
  } 
}

static void _interflop_add_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = a + b;
  
  if (id != NULL && _vfc_inst_map != NULL){
    // get the fops metadata
    interflop_instruction_info_t *inst_info =
        vfc_hashmap_get(_vfc_inst_map, vfc_hashmap_str_function(id));

    // if the operation is not identified
    if (inst_info == NULL || inst_info->fopsInfo == NULL) {
      logger_error("The fops %s cannot be found in the map", id);
    }
    
    cancell(a, b, c, inst_info->filePath, inst_info->line, inst_info->column);
          
  }else{
    cancell(a, b, c, "unknown location", 0, 0);
  } 
}

static void _interflop_sub_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = a - b;
  
  if (id != NULL && _vfc_inst_map != NULL){
    // get the fops metadata
    interflop_instruction_info_t *inst_info =
        vfc_hashmap_get(_vfc_inst_map, vfc_hashmap_str_function(id));

    // if the operation is not identified
    if (inst_info == NULL || inst_info->fopsInfo == NULL) {
      logger_error("The fops %s cannot be found in the map", id);
    }
    
    cancell(a, b, c, inst_info->filePath, inst_info->line, inst_info->column);
          
  }else{
    cancell(a, b, c, "unknown location", 0, 0);
  } 
}

static void _interflop_mul_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = a * b;
}

static void _interflop_div_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = a / b;
}

static void _interflop_mul_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = a * b;
}

static void _interflop_div_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = a / b;
}

static struct argp_option options[] = {
    {"tolerance", 't', "TOLERANCE", 0, "Select tolerance (TOLERANCE >= 0)", 0},
    {"warning", 'w', "WARNING", 0, "Enable warning for cancellations", 0},
    {"seed", 's', "SEED", 0, "Fix the random generator seed", 0},
    {0}};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
  t_context *ctx = (t_context *)state->input;
  char *endptr;
  switch (key) {
  case 't':
    /* tolerance */
    errno = 0;
    int val = strtol(arg, &endptr, 10);
    if (errno != 0 || val < 0) {
      logger_error("--tolerance invalid value provided, must be a"
                   "positive integer.");
    } else {
      _set_tolerance(val);
    }
    break;
  case 'w':
    _set_warning(true);
    break;
  case 's':
    errno = 0;
    ctx->choose_seed = 1;
    ctx->seed = strtoull(arg, &endptr, 10);
    if (errno != 0) {
      logger_error("--seed invalid value provided, must be an integer");
    }
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = {options, parse_opt, "", "", NULL, NULL, NULL};

static void init_context(t_context *ctx) {
  ctx->choose_seed = 0;
  ctx->seed = 0ULL;
}

struct interflop_backend_interface_t
interflop_init(int argc, char **argv, interflop_function_stack_t call_stack,
               vfc_hashmap_t inst_map, void **context) {

  logger_init();

  _set_tolerance(TOLERANCE_DEFAULT);

  t_context *ctx = malloc(sizeof(t_context));
  *context = ctx;
  init_context(ctx);

  _vfc_inst_map = inst_map;

  /* parse backend arguments */
  argp_parse(&argp, argc, argv, 0, 0, ctx);

  logger_info("interflop_cancellation: loaded backend with tolerance = %d\n",
              TOLERANCE);

  struct interflop_backend_interface_t interflop_backend_cancellation = {
      _interflop_add_float,
      _interflop_sub_float,
      _interflop_mul_float,
      _interflop_div_float,
      NULL,
      _interflop_add_double,
      _interflop_sub_double,
      _interflop_mul_double,
      _interflop_div_double,
      NULL,
      NULL,
      NULL,
      NULL};

  /* Initialize the seed */
  _set_mca_seed(ctx->choose_seed, ctx->seed);

  return interflop_backend_cancellation;
}
#endif
