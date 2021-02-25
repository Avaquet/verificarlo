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
// 2018-07-7 Initial version from scratch
//
// 2019-11-25 Code refactoring, format conversions moved to
// ../../common/vprec_tools.c
//

#include <argp.h>
#include <err.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../../common/float_const.h"
#include "../../common/float_struct.h"
#include "../../common/float_utils.h"
#include "../../common/interflop.h"
#include "../../common/logger.h"
#include "../../common/vfc_hashmap.h"
#include "../../common/vprec_tools.h"

typedef enum {
  KEY_PREC_B32,
  KEY_PREC_B64,
  KEY_RANGE_B32,
  KEY_RANGE_B64,
  KEY_ERR_EXP,
  KEY_INPUT_FILE,
  KEY_OUTPUT_FILE,
  KEY_LOG_FILE,
  KEY_MODE = 'm',
  KEY_ERR_MODE = 'e',
  KEY_INSTRUMENT = 'i',
  KEY_DAZ = 'd',
  KEY_FTZ = 'f'
} key_args;

static const char key_prec_b32_str[] = "precision-binary32";
static const char key_prec_b64_str[] = "precision-binary64";
static const char key_range_b32_str[] = "range-binary32";
static const char key_range_b64_str[] = "range-binary64";
static const char key_input_file_str[] = "prec-input-file";
static const char key_output_file_str[] = "prec-output-file";
static const char key_log_file_str[] = "prec-log-file";
static const char key_mode_str[] = "mode";
static const char key_err_mode_str[] = "error-mode";
static const char key_err_exp_str[] = "max-abs-error-exponent";
static const char key_instrument_str[] = "instrument";
static const char key_daz_str[] = "daz";
static const char key_ftz_str[] = "ftz";
static vfc_hashmap_t _vfc_inst_map;
static interflop_function_stack_t _vfc_call_stack;

typedef struct {
  bool relErr;
  bool absErr;
  int absErr_exp;
  bool daz;
  bool ftz;
} t_context;

/* define the available VPREC modes of operation */
typedef enum {
  vprecmode_ieee,
  vprecmode_full,
  vprecmode_ib,
  vprecmode_ob,
  _vprecmode_end_
} vprec_mode;

/* Modes' names */
static const char *VPREC_MODE_STR[] = {"ieee", "full", "ib", "ob"};

/* define the available error modes */
typedef enum {
  vprec_err_mode_rel,
  vprec_err_mode_abs,
  vprec_err_mode_all
} vprec_err_mode;

static const char *VPREC_ERR_MODE_STR[] = {"rel", "abs", "all"};

/* define the possible VPREC operation */
typedef enum {
  vprec_add = '+',
  vprec_sub = '-',
  vprec_mul = '*',
  vprec_div = '/',
} vprec_operation;

/* define default environment variables and default parameters */

/* default values of precision and range for binary32 */
#define VPREC_PRECISION_BINARY32_MIN 1
#define VPREC_PRECISION_BINARY32_MAX FLOAT_PMAN_SIZE
#define VPREC_PRECISION_BINARY32_DEFAULT FLOAT_PMAN_SIZE
#define VPREC_RANGE_BINARY32_MIN 2
#define VPREC_RANGE_BINARY32_MAX FLOAT_EXP_SIZE
#define VPREC_RANGE_BINARY32_DEFAULT FLOAT_EXP_SIZE

/* default values of precision and range for binary64 */
#define VPREC_PRECISION_BINARY64_MIN 1
#define VPREC_PRECISION_BINARY64_MAX DOUBLE_PMAN_SIZE
#define VPREC_PRECISION_BINARY64_DEFAULT DOUBLE_PMAN_SIZE
#define VPREC_RANGE_BINARY64_MIN 2
#define VPREC_RANGE_BINARY64_MAX DOUBLE_EXP_SIZE
#define VPREC_RANGE_BINARY64_DEFAULT DOUBLE_EXP_SIZE

/* common default values */
#define VPREC_MODE_DEFAULT vprecmode_ob

/* variables that control precision, range and mode */
static vprec_mode VPRECLIB_MODE = VPREC_MODE_DEFAULT;
static int VPRECLIB_BINARY32_PRECISION = VPREC_PRECISION_BINARY32_DEFAULT;
static int VPRECLIB_BINARY64_PRECISION = VPREC_PRECISION_BINARY64_DEFAULT;
static int VPRECLIB_BINARY32_RANGE = VPREC_RANGE_BINARY32_DEFAULT;
static int VPRECLIB_BINARY64_RANGE = VPREC_RANGE_BINARY64_DEFAULT;

static float _vprec_binary32_binary_op(float a, float b,
                                       const vprec_operation op, char *id,
                                       void *context);
static double _vprec_binary64_binary_op(double a, double b,
                                        const vprec_operation op, char *id,
                                        void *context);

/* variables and structure for instrumentation mode */

/* define instrumentation modes */
typedef enum {
  vprecinst_arg,
  vprecinst_op,
  vprecinst_all,
  vprecinst_none,
  _vprecinst_end_
} vprec_inst_mode;

/* default instrumentation mode */
#define VPREC_INST_MODE_DEFAULT vprecinst_none

static const char *vprec_input_file = NULL;
static const char *vprec_output_file = NULL;
static FILE *vprec_log_file = NULL;
static size_t vprec_log_depth = 0;
static vprec_inst_mode VPREC_INST_MODE = VPREC_INST_MODE_DEFAULT;

/* instrumentation modes' names */
static const char *VPREC_INST_MODE_STR[] = {"arguments", "operations", "all",
                                            "none"};

/******************** VPREC CONTROL FUNCTIONS *******************
 * The following functions are used to set virtual precision,
 * VPREC mode of operation and instrumentation mode.
 ***************************************************************/

void _set_vprec_mode(vprec_mode mode) {
  if (mode >= _vprecmode_end_) {
    logger_error("invalid mode provided, must be one of: "
                 "{ieee, full, ib, ob}.");
  } else {
    VPRECLIB_MODE = mode;
  }
}

void _set_vprec_precision_binary32(int precision) {
  if (precision < VPREC_PRECISION_BINARY32_MIN) {
    logger_error("invalid precision provided for binary32."
                 "Must be greater than (%d) and is %d",
                 VPREC_PRECISION_BINARY32_MIN, precision);
  } else if (VPREC_PRECISION_BINARY32_MAX < precision) {
    logger_error("invalid precision provided for binary32. "
                 "Must be lower than (%d) and is %d",
                 VPREC_PRECISION_BINARY32_MAX, precision);
  } else {
    VPRECLIB_BINARY32_PRECISION = precision;
  }
}

void _set_vprec_range_binary32(int range) {
  if (range < VPREC_RANGE_BINARY32_MIN) {
    logger_error("invalid range provided for binary32."
                 "Must be greater than (%d) and is %d",
                 VPREC_RANGE_BINARY32_MIN, range);
  } else if (VPREC_RANGE_BINARY32_MAX < range) {
    logger_error("invalid range provided for binary32."
                 "Must be lower than (%d) and is %d",
                 VPREC_RANGE_BINARY32_MAX, range);
  } else {
    VPRECLIB_BINARY32_RANGE = range;
  }
}

void _set_vprec_precision_binary64(int precision) {
  if (precision < VPREC_PRECISION_BINARY64_MIN) {
    logger_error("invalid precision provided for binary64."
                 "Must be greater than (%d) and is %d",
                 VPREC_PRECISION_BINARY64_MIN);
  } else if (VPREC_PRECISION_BINARY64_MAX < precision) {
    logger_error("invalid precision provided for binary64."
                 "Must be lower than (%d) and is %d",
                 VPREC_PRECISION_BINARY64_MAX, precision);
  } else {
    VPRECLIB_BINARY64_PRECISION = precision;
  }
}

void _set_vprec_range_binary64(int range) {
  if (range < VPREC_RANGE_BINARY64_MIN) {
    logger_error("invalid range provided for binary64."
                 "Must be greater than (%d) and is %d",
                 VPREC_RANGE_BINARY64_MIN, range);
  } else if (VPREC_RANGE_BINARY64_MAX < range) {
    logger_error("invalid range provided for binary64."
                 "Must be lower than (%d) and is %d",
                 VPREC_RANGE_BINARY64_MAX, range);
  } else {
    VPRECLIB_BINARY64_RANGE = range;
  }
}

void _set_vprec_input_file(const char *input_file) {
  vprec_input_file = input_file;
}

void _set_vprec_output_file(const char *output_file) {
  vprec_output_file = output_file;
}

void _set_vprec_log_file(const char *log_file) {
  vprec_log_file = fopen(log_file, "w");

  if (vprec_log_file == NULL) {
    logger_error("Log file can't be written");
  }
}

void _set_vprec_inst_mode(vprec_inst_mode mode) {
  if (mode >= _vprecinst_end_) {
    logger_error("invalid instrumentation mode provided, must be one of:"
                 "{arguments, operations, all, none}.");
  } else {
    VPREC_INST_MODE = mode;
  }
}

/******************** VPREC HELPER FUNCTIONS *******************
 * The following functions are used to set virtual precision,
 * VPREC mode of operation and instrumentation mode.
 ***************************************************************/

inline int compute_absErr_vprec_binary32(bool isDenormal,
                                         t_context *currentContext, int expDiff,
                                         int binary32_precision) {
  /* this function is used only when in vprec error mode abs and all,
   * so there is no need to handle vprec error mode rel */
  if (isDenormal == true) {
    /* denormal, or underflow case */
    if (currentContext->relErr == true) {
      /* vprec error mode all */
      if (abs(currentContext->absErr_exp) < binary32_precision)
        return currentContext->absErr_exp;
      else
        return binary32_precision;
    } else {
      /* vprec error mode abs */
      return currentContext->absErr_exp;
    }
  } else {
    /* normal case */
    if (currentContext->relErr == true) {
      /* vprec error mode all */
      if (expDiff < binary32_precision)
        return expDiff;
      else {
        return binary32_precision;
      }
    } else {
      /* vprec error mode abs */
      if (expDiff < FLOAT_PMAN_SIZE) {
        return expDiff;
      } else {
        return FLOAT_PMAN_SIZE;
      }
    }
  }
}

inline int compute_absErr_vprec_binary64(bool isDenormal,
                                         t_context *currentContext, int expDiff,
                                         int binary64_precision) {
  /* this function is used only when in vprec error mode abs and all,
   * so there is no need to handle vprec error mode rel */
  if (isDenormal == true) {
    /* denormal, or underflow case */
    if (currentContext->relErr == true) {
      /* vprec error mode all */
      if (abs(currentContext->absErr_exp) < binary64_precision)
        return currentContext->absErr_exp;
      else
        return binary64_precision;
    } else {
      /* vprec error mode abs */
      return currentContext->absErr_exp;
    }
  } else {
    /* normal case */
    if (currentContext->relErr == true) {
      /* vprec error mode all */
      if (expDiff < binary64_precision)
        return expDiff;
      else {
        return binary64_precision;
      }
    } else {
      /* vprec error mode abs */
      if (expDiff < DOUBLE_PMAN_SIZE) {
        return expDiff;
      } else {
        return DOUBLE_PMAN_SIZE;
      }
    }
  }
}

inline float handle_binary32_normal_absErr(float a, int32_t aexp,
                                           int binary32_precision,
                                           t_context *currentContext) {
  /* absolute error mode, or both absolute and relative error modes */
  int expDiff = aexp - currentContext->absErr_exp;
  float retVal;

  if (expDiff < -1) {
    /* equivalent to underflow on the precision given by absolute error */
    retVal = 0;
  } else if (expDiff == -1) {
    /* case when the number is just below the absolute error threshold,
      but will round to one ulp on the format given by the absolute error;
      this needs to be handled separately, as round_binary32_normal cannot
      generate this number */
    retVal = copysignf(exp2f(currentContext->absErr_exp), a);
  } else {
    /* normal case for the absolute error mode */
    int binary32_precision_adjusted = compute_absErr_vprec_binary32(
        false, currentContext, expDiff, binary32_precision);
    retVal = round_binary32_normal(a, binary32_precision_adjusted);
  }

  return retVal;
}

inline double handle_binary64_normal_absErr(double a, int64_t aexp,
                                            int binary64_precision,
                                            t_context *currentContext) {
  /* absolute error mode, or both absolute and relative error modes */
  int expDiff = aexp - currentContext->absErr_exp;
  double retVal;

  if (expDiff < -1) {
    /* equivalent to underflow on the precision given by absolute error */
    retVal = 0;
  } else if (expDiff == -1) {
    /* case when the number is just below the absolute error threshold,
      but will round to one ulp on the format given by the absolute error;
      this needs to be handled separately, as round_binary32_normal cannot
      generate this number */
    retVal = copysign(exp2(currentContext->absErr_exp), a);
  } else {
    /* normal case for the absolute error mode */
    int binary64_precision_adjusted = compute_absErr_vprec_binary64(
        false, currentContext, expDiff, binary64_precision);
    retVal = round_binary64_normal(a, binary64_precision_adjusted);
  }

  return retVal;
}

/******************** VPREC ARITHMETIC FUNCTIONS ********************
 * The following set of functions perform the VPREC operation. Operands
 * are first correctly rounded to the target precison format if inbound
 * is set, the operation is then perform using IEEE hw and
 * correct rounding to the target precision format is done if outbound
 * is set.
 *******************************************************************/

/* perform_bin_op: applies the binary operator (op) to (a) and (b) */
/* and stores the result in (res) */
#define perform_binary_op(op, res, a, b)                                       \
  switch (op) {                                                                \
  case vprec_add:                                                              \
    res = (a) + (b);                                                           \
    break;                                                                     \
  case vprec_mul:                                                              \
    res = (a) * (b);                                                           \
    break;                                                                     \
  case vprec_sub:                                                              \
    res = (a) - (b);                                                           \
    break;                                                                     \
  case vprec_div:                                                              \
    res = (a) / (b);                                                           \
    break;                                                                     \
  default:                                                                     \
    logger_error("invalid operator %c", op);                                   \
  };

// Round the float with the given precision
static float _vprec_round_binary32(float a, char is_input, void *context,
                                   int binary32_range, int binary32_precision) {
  t_context *currentContext = (t_context *)context;

  /* test if 'a' is a special case */
  if (!isfinite(a)) {
    return a;
  }

  /* round to zero or set to infinity if underflow or overflow compared to
   * VPRECLIB_BINARY32_RANGE */
  int emax = (1 << (binary32_range - 1)) - 1;
  /* here emin is the smallest exponent in the *normal* range */
  int emin = 1 - emax;

  binary32 aexp = {.f32 = a};
  aexp.s32 = ((FLOAT_GET_EXP & aexp.u32) >> FLOAT_PMAN_SIZE) - FLOAT_EXP_COMP;

  /* check for overflow in target range */
  if (aexp.s32 > emax) {
    a = a * INFINITY;
    return a;
  }

  /* check for underflow in target range */
  if (aexp.s32 < emin) {
    /* underflow case: possibly a denormal */
    if ((currentContext->daz && is_input) ||
        (currentContext->ftz && !is_input)) {
      return a * 0; // preserve sign
    } else if (FP_ZERO == fpclassify(a)) {
      return a;
    } else {
      if (currentContext->absErr == true) {
        /* absolute error mode, or both absolute and relative error modes */
        int binary32_precision_adjusted = compute_absErr_vprec_binary32(
            true, currentContext, 0, binary32_precision);
        a = handle_binary32_denormal(a, emin, binary32_precision_adjusted);
      } else {
        /* relative error mode */
        a = handle_binary32_denormal(a, emin, binary32_precision);
      }
    }
  } else {
    /* else, normal case: can be executed even if a
     previously rounded and truncated as denormal */
    if (currentContext->absErr == true) {
      /* absolute error mode, or both absolute and relative error modes */
      a = handle_binary32_normal_absErr(a, aexp.s32, binary32_precision,
                                        currentContext);
    } else {
      /* relative error mode */
      a = round_binary32_normal(a, binary32_precision);
    }
  }

  return a;
}

// Round the double with the given precision
static double _vprec_round_binary64(double a, char is_input, void *context,
                                    int binary64_range,
                                    int binary64_precision) {
  t_context *currentContext = (t_context *)context;

  /* test if 'a' is a special case */
  if (!isfinite(a)) {
    return a;
  }

  /* round to zero or set to infinity if underflow or overflow compare to
   * VPRECLIB_BINARY64_RANGE */
  int emax = (1 << (binary64_range - 1)) - 1;
  /* here emin is the smallest exponent in the *normal* range */
  int emin = 1 - emax;

  binary64 aexp = {.f64 = a};
  aexp.s64 =
      ((DOUBLE_GET_EXP & aexp.u64) >> DOUBLE_PMAN_SIZE) - DOUBLE_EXP_COMP;

  /* check for overflow in target range */
  if (aexp.s64 > emax) {
    a = a * INFINITY;
    return a;
  }

  /* check for underflow in target range */
  if (aexp.s64 < emin) {
    /* underflow case: possibly a denormal */
    if ((currentContext->daz && is_input) ||
        (currentContext->ftz && !is_input)) {
      return a * 0; // preserve sign
    } else if (FP_ZERO == fpclassify(a)) {
      return a;
    } else {
      if (currentContext->absErr == true) {
        /* absolute error mode, or both absolute and relative error modes */
        int binary64_precision_adjusted = compute_absErr_vprec_binary64(
            true, currentContext, 0, binary64_precision);
        a = handle_binary64_denormal(a, emin, binary64_precision_adjusted);
      } else {
        /* relative error mode */
        a = handle_binary64_denormal(a, emin, binary64_precision);
      }
    }
  } else {
    /* else, normal case: can be executed even if a
     previously rounded and truncated as denormal */
    if (currentContext->absErr == true) {
      /* absolute error mode, or both absolute and relative error modes */
      a = handle_binary64_normal_absErr(a, aexp.s64, binary64_precision,
                                        currentContext);
    } else {
      /* relative error mode */
      a = round_binary64_normal(a, binary64_precision);
    }
  }

  return a;
}

static inline float _vprec_binary32_binary_op(float a, float b,
                                              const vprec_operation op,
                                              char *id, void *context) {
  float res = 0;
  unsigned precision, range;

  if (_vfc_inst_map != NULL) {
    // if the internal operations should not be rounded
    if (VPREC_INST_MODE == vprecinst_none || VPREC_INST_MODE == vprecinst_arg) {
      perform_binary_op(op, res, a, b);
      return res;
    }

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

    precision = inst_info->fopsInfo->precision;
    range = inst_info->fopsInfo->range;

  } else {
    precision = VPRECLIB_BINARY32_PRECISION;
    range = VPRECLIB_BINARY32_RANGE;
  }

  if ((VPRECLIB_MODE == vprecmode_full) || (VPRECLIB_MODE == vprecmode_ib)) {
    a = _vprec_round_binary32(a, 1, context, range, precision);
    b = _vprec_round_binary32(b, 1, context, range, precision);
  }

  perform_binary_op(op, res, a, b);

  if ((VPRECLIB_MODE == vprecmode_full) || (VPRECLIB_MODE == vprecmode_ob)) {
    res = _vprec_round_binary32(res, 0, context, range, precision);
  }

  return res;
}

static inline double _vprec_binary64_binary_op(double a, double b,
                                               const vprec_operation op,
                                               char *id, void *context) {
  double res = 0;
  unsigned precision, range;

  if (_vfc_inst_map != NULL) {
    // if the internal operations should not be rounded
    if (VPREC_INST_MODE == vprecinst_none || VPREC_INST_MODE == vprecinst_arg) {
      perform_binary_op(op, res, a, b);
      return res;
    }

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

    precision = inst_info->fopsInfo->precision;
    range = inst_info->fopsInfo->range;
  } else {
    precision = VPRECLIB_BINARY64_PRECISION;
    range = VPRECLIB_BINARY64_RANGE;
  }

  if ((VPRECLIB_MODE == vprecmode_full) || (VPRECLIB_MODE == vprecmode_ib)) {
    a = _vprec_round_binary64(a, 1, context, range, precision);
    b = _vprec_round_binary64(b, 1, context, range, precision);
  }

  perform_binary_op(op, res, a, b);

  if ((VPRECLIB_MODE == vprecmode_full) || (VPRECLIB_MODE == vprecmode_ob)) {
    res = _vprec_round_binary64(res, 0, context, range, precision);
  }

  return res;
}

/******************** VPREC INSTRUMENTATION FUNCTIONS ********************
 * The following set of functions is used to apply vprec on instrumented
 * functions. For that we need a hashmap to stock data and reading and
 * writing functions to get and save them. Enter and exit functions are
 * called before and after the instrumented function and allow us to set
 * the desired precision or to round arguments, depending on the mode.
 *************************************************************************/

/*
void _vprec_min_max(bool isFloat, void *tmp_value,
                    interflop_arg_info_t *arg_info) {
  if (isFloat) {
    float *value = tmp_value;

    if (!(isnan(*value) || isinf(*value))) {
      arg_info->minRange = (floor(*value) < arg_info->minRange)
                               ? floorf(*value)
                               : arg_info->minRange;
      arg_info->maxRange = (ceil(*value) > arg_info->maxRange)
                               ? ceilf(*value)
                               : arg_info->maxRange;
    }
  } else {
    double *value = tmp_value;

    if (!(isnan(*value) || isinf(*value))) {
      arg_info->minRange = (floor(*value) < arg_info->minRange)
                               ? floor(*value)
                               : arg_info->minRange;
      arg_info->maxRange = (ceil(*value) > arg_info->maxRange)
                               ? ceil(*value)
                               : arg_info->maxRange;
    }
  }
}
*/

void _vprec_round_arg(va_list ap, int isInput, enum FTYPES argType,
                      unsigned argSize, unsigned precision, unsigned range,
                      void *context) {
  if (argType == FDOUBLE) {
    double *value = va_arg(ap, double *);

    // round the argument
    *value = _vprec_round_binary64(*value, isInput, context, range, precision);

    // set the minimum and maximum values
    //_vprec_min_max(false, value, arg_info);

  } else if (argType == FFLOAT) {
    float *value = va_arg(ap, float *);

    // round the argument
    *value = _vprec_round_binary32(*value, isInput, context, range, precision);

    // set the minimum and maximum values
    //_vprec_min_max(true, value, arg_info);

  } else if (argType == FDOUBLE_PTR) {
    double *value = va_arg(ap, double *);

    for (unsigned int j = 0; value != NULL && j < argSize; j++, value++) {
      // round the argument
      *value =
          _vprec_round_binary64(*value, isInput, context, range, precision);

      // set the minimum and maximum values
      //_vprec_min_max(false, value, arg_info);
    }
  } else if (argType == FFLOAT_PTR) {
    float *value = va_arg(ap, float *);

    for (unsigned int j = 0; value != NULL && j < argSize; j++, value++) {
      // round the argument
      *value =
          _vprec_round_binary32(*value, isInput, context, range, precision);

      // set the minimum and maximum values
      //_vprec_min_max(true, value, arg_info);
    }
  }
}

// Set precision for internal operations and round input arguments for a given
// function call
void _interflop_enter_function(char *id, void *context, int nb_args,
                               va_list ap) {
  // boolean which indicates if arguments should be rounded or not depending on
  // modes
  int mode_flag =
      (((VPRECLIB_MODE == vprecmode_full) || (VPRECLIB_MODE == vprecmode_ib)) &&
       ((VPREC_INST_MODE == vprecinst_all) ||
        (VPREC_INST_MODE == vprecinst_arg)) &&
       VPREC_INST_MODE != vprecinst_none);

  if (!mode_flag) {
    return;
  }

  if (_vfc_inst_map != NULL) {
    // get the call metadata
    interflop_instruction_info_t *inst_info =
        vfc_hashmap_get(_vfc_inst_map, vfc_hashmap_str_function(id));

    if (inst_info == NULL || inst_info->functionInfo == NULL)
      logger_error("The function %s cannot be found in the map\n", id);

    for (int i = 0; i < nb_args; i++) {
      enum FTYPES argType = va_arg(ap, enum FTYPES);
      unsigned argSize = va_arg(ap, unsigned);

      // get the arg metadata
      interflop_arg_info_t *arg_info = &(inst_info->inputArgs[i]);

      // round the arg and set the minimum and maximum values
      _vprec_round_arg(ap, 1, argType, argSize, arg_info->precision,
                       arg_info->range, context);
    }
  } else {
    for (int i = 0; i < nb_args; i++) {
      enum FTYPES argType = va_arg(ap, enum FTYPES);
      unsigned argSize = va_arg(ap, unsigned);
      unsigned precision = (argType == FDOUBLE || argType == FDOUBLE_PTR)
                               ? VPRECLIB_BINARY64_PRECISION
                               : VPRECLIB_BINARY32_PRECISION;
      unsigned range = (argType == FDOUBLE || argType == FDOUBLE_PTR)
                           ? VPRECLIB_BINARY64_RANGE
                           : VPRECLIB_BINARY32_RANGE;

      // round the arg and set the minimum and maximum values
      _vprec_round_arg(ap, 1, argType, argSize, precision, range, context);
    }
  }
}

// Set precision for internal operations and round output arguments for a given
// function call
void _interflop_exit_function(char *id, void *context, int nb_args,
                              va_list ap) {
  // boolean which indicates if arguments should be rounded or not depending on
  // modes
  int mode_flag =
      (((VPRECLIB_MODE == vprecmode_full) || (VPRECLIB_MODE == vprecmode_ob)) &&
       (VPREC_INST_MODE == vprecinst_all || VPREC_INST_MODE == vprecinst_arg) &&
       VPREC_INST_MODE != vprecinst_none);

  if (!mode_flag) {
    return;
  }

  if (_vfc_inst_map != NULL) {
    // get the call metadata
    interflop_instruction_info_t *inst_info =
        vfc_hashmap_get(_vfc_inst_map, vfc_hashmap_str_function(id));

    if (inst_info == NULL || inst_info->functionInfo == NULL)
      logger_error("The function %s cannot be found in the map\n", id);

    for (int i = 0; i < nb_args; i++) {
      enum FTYPES argType = va_arg(ap, enum FTYPES);
      unsigned argSize = va_arg(ap, unsigned);

      // get the arg metadata
      interflop_arg_info_t *arg_info = &(inst_info->outputArgs[i]);

      // round the arg and set the minimum and maximum values
      _vprec_round_arg(ap, 0, argType, argSize, arg_info->precision,
                       arg_info->range, context);
    }
  } else {
    for (int i = 0; i < nb_args; i++) {
      enum FTYPES argType = va_arg(ap, enum FTYPES);
      unsigned argSize = va_arg(ap, unsigned);
      unsigned precision = (argType == FDOUBLE || argType == FDOUBLE_PTR)
                               ? VPRECLIB_BINARY64_PRECISION
                               : VPRECLIB_BINARY32_PRECISION;
      unsigned range = (argType == FDOUBLE || argType == FDOUBLE_PTR)
                           ? VPRECLIB_BINARY64_RANGE
                           : VPRECLIB_BINARY32_RANGE;

      // round the arg and set the minimum and maximum values
      _vprec_round_arg(ap, 0, argType, argSize, precision, range, context);
    }
  }
}

/************************* FPHOOKS FUNCTIONS *************************
 * These functions correspond to those inserted into the source code
 * during source to source compilation and are replacement to floating
 * point operators
 **********************************************************************/

static void _interflop_add_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = _vprec_binary32_binary_op(a, b, vprec_add, id, context);
}

static void _interflop_sub_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = _vprec_binary32_binary_op(a, b, vprec_sub, id, context);
}

static void _interflop_mul_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = _vprec_binary32_binary_op(a, b, vprec_mul, id, context);
}

static void _interflop_div_float(float a, float b, float *c, char *id,
                                 void *context) {
  *c = _vprec_binary32_binary_op(a, b, vprec_div, id, context);
}

static void _interflop_add_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = _vprec_binary64_binary_op(a, b, vprec_add, id, context);
}

static void _interflop_sub_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = _vprec_binary64_binary_op(a, b, vprec_sub, id, context);
}

static void _interflop_mul_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = _vprec_binary64_binary_op(a, b, vprec_mul, id, context);
}

static void _interflop_div_double(double a, double b, double *c, char *id,
                                  void *context) {
  *c = _vprec_binary64_binary_op(a, b, vprec_div, id, context);
}

static struct argp_option options[] = {
    /* --debug, sets the variable debug = true */
    {key_prec_b32_str, KEY_PREC_B32, "PRECISION", 0,
     "select precision for binary32 (PRECISION >= 0)", 0},
    {key_prec_b64_str, KEY_PREC_B64, "PRECISION", 0,
     "select precision for binary64 (PRECISION >= 0)", 0},
    {key_range_b32_str, KEY_RANGE_B32, "RANGE", 0,
     "select range for binary32 (0 < RANGE && RANGE <= 8)", 0},
    {key_range_b64_str, KEY_RANGE_B64, "RANGE", 0,
     "select range for binary64 (0 < RANGE && RANGE <= 11)", 0},
    {key_input_file_str, KEY_INPUT_FILE, "INPUT", 0,
     "input file with the precision configuration to use", 0},
    {key_output_file_str, KEY_OUTPUT_FILE, "OUTPUT", 0,
     "output file where the precision profile is written", 0},
    {key_log_file_str, KEY_LOG_FILE, "LOG", 0,
     "log file where input/output informations are written", 0},
    {key_mode_str, KEY_MODE, "MODE", 0,
     "select VPREC mode among {ieee, full, ib, ob}", 0},
    {key_err_mode_str, KEY_ERR_MODE, "ERROR_MODE", 0,
     "select error mode among {rel, abs, all}", 0},
    {key_err_exp_str, KEY_ERR_EXP, "MAX_ABS_ERROR_EXPONENT", 0,
     "select magnitude of the maximum absolute error", 0},
    {key_instrument_str, KEY_INSTRUMENT, "INSTRUMENTATION", 0,
     "select VPREC instrumentation mode among {arguments, operations, full}",
     0},
    {key_daz_str, KEY_DAZ, 0, 0,
     "denormals-are-zero: sets denormals inputs to zero", 0},
    {key_ftz_str, KEY_FTZ, 0, 0, "flush-to-zero: sets denormal output to zero",
     0},
    {0}};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
  t_context *ctx = (t_context *)state->input;
  char *endptr;
  int val = -1;
  switch (key) {
  case KEY_PREC_B32:
    /* precision */
    errno = 0;
    val = strtol(arg, &endptr, 10);
    if (errno != 0 || val < VPREC_PRECISION_BINARY32_MIN) {
      logger_error("--%s invalid value provided, must be a "
                   "positive integer.",
                   key_prec_b32_str);
    } else if (val > VPREC_PRECISION_BINARY32_MAX) {
      logger_error("--%s invalid value provided, "
                   "must lower than IEEE binary32 precision (%d)",
                   key_prec_b32_str, VPREC_PRECISION_BINARY32_MAX);
    } else {
      _set_vprec_precision_binary32(val);
    }
    break;
  case KEY_PREC_B64:
    /* precision */
    errno = 0;
    val = strtol(arg, &endptr, 10);
    if (errno != 0 || val < VPREC_PRECISION_BINARY64_MIN) {
      logger_error("--%s invalid value provided, must be a "
                   "positive integer.",
                   key_prec_b64_str);
    } else if (val > VPREC_PRECISION_BINARY64_MAX) {
      logger_error("--%s invalid value provided, "
                   "must be lower than IEEE binary64 precision (%d)",
                   key_prec_b64_str, VPREC_PRECISION_BINARY64_MAX);
    } else {
      _set_vprec_precision_binary64(val);
    }
    break;
  case KEY_RANGE_B32:
    /* precision */
    errno = 0;
    val = strtol(arg, &endptr, 10);
    if (errno != 0 || val < VPREC_RANGE_BINARY32_MIN) {
      logger_error("--%s invalid value provided, must be a "
                   "positive integer.",
                   key_range_b32_str);
    } else if (val > VPREC_RANGE_BINARY32_MAX) {
      logger_error("--%s invalid value provided, "
                   "must be lower than IEEE binary32 range size (%d)",
                   key_range_b32_str, VPREC_RANGE_BINARY32_MAX);
    } else {
      _set_vprec_range_binary32(val);
    }
    break;
  case KEY_RANGE_B64:
    /* precision */
    errno = 0;
    val = strtol(arg, &endptr, 10);
    if (errno != 0 || val < VPREC_RANGE_BINARY64_MIN) {
      logger_error("--%s invalid value provided, must be a "
                   "positive integer.",
                   key_range_b64_str);
    } else if (val > VPREC_RANGE_BINARY64_MAX) {
      logger_error("--%s invalid value provided, "
                   "must be lower than IEEE binary64 range size (%d)",
                   key_range_b64_str, VPREC_RANGE_BINARY64_MAX);
    } else {
      _set_vprec_range_binary64(val);
    }
    break;
  case KEY_INPUT_FILE:
    /* input file */
    _set_vprec_input_file(arg);
    break;
  case KEY_OUTPUT_FILE:
    /* output file */
    _set_vprec_output_file(arg);
    break;
  case KEY_LOG_FILE:
    /* log file */
    _set_vprec_log_file(arg);
    break;
  case KEY_MODE:
    /* mode */
    if (strcasecmp(VPREC_MODE_STR[vprecmode_ieee], arg) == 0) {
      _set_vprec_mode(vprecmode_ieee);
    } else if (strcasecmp(VPREC_MODE_STR[vprecmode_full], arg) == 0) {
      _set_vprec_mode(vprecmode_full);
    } else if (strcasecmp(VPREC_MODE_STR[vprecmode_ib], arg) == 0) {
      _set_vprec_mode(vprecmode_ib);
    } else if (strcasecmp(VPREC_MODE_STR[vprecmode_ob], arg) == 0) {
      _set_vprec_mode(vprecmode_ob);
    } else {
      logger_error("--%s invalid value provided, must be one of: "
                   "{ieee, full, ib, ob}.",
                   key_mode_str);
    }
    break;
  case KEY_ERR_MODE:
    /* vprec error mode */
    if (strcasecmp(VPREC_ERR_MODE_STR[vprec_err_mode_rel], arg) == 0) {
      ctx->relErr = true;
      ctx->absErr = false;
    } else if (strcasecmp(VPREC_ERR_MODE_STR[vprec_err_mode_abs], arg) == 0) {
      ctx->relErr = false;
      ctx->absErr = true;
    } else if (strcasecmp(VPREC_ERR_MODE_STR[vprec_err_mode_all], arg) == 0) {
      ctx->relErr = true;
      ctx->absErr = true;
    } else {
      logger_error("--%s invalid value provided, must be one of: "
                   "{rel, abs, all}.",
                   key_err_mode_str);
    }
    break;
  case KEY_ERR_EXP:
    /* exponent of the maximum absolute error */
    errno = 0;
    ctx->absErr_exp = strtol(arg, &endptr, 10);
    if (errno != 0) {
      logger_error("--%s invalid value provided, must be an integer",
                   key_err_exp_str);
    }
    break;
  case KEY_INSTRUMENT:
    /* instrumentation mode */
    if (strcasecmp(VPREC_INST_MODE_STR[vprecinst_arg], arg) == 0) {
      _set_vprec_inst_mode(vprecinst_arg);
    } else if (strcasecmp(VPREC_INST_MODE_STR[vprecinst_op], arg) == 0) {
      _set_vprec_inst_mode(vprecinst_op);
    } else if (strcasecmp(VPREC_INST_MODE_STR[vprecinst_all], arg) == 0) {
      _set_vprec_inst_mode(vprecinst_all);
    } else if (strcasecmp(VPREC_INST_MODE_STR[vprecinst_none], arg) == 0) {
      _set_vprec_inst_mode(vprecinst_none);
    } else {
      logger_error("--%s invalid value provided, must be one of: "
                   "{arguments, operations, all}.",
                   key_instrument_str);
    }
    break;
  case KEY_DAZ:
    /* denormals-are-zero */
    ctx->daz = true;
    break;
  case KEY_FTZ:
    /* flush-to-zero */
    ctx->ftz = true;
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = {options, parse_opt, "", "", NULL, NULL, NULL};

void init_context(t_context *ctx) {
  ctx->relErr = true;
  ctx->absErr = false;
  ctx->absErr_exp = -DOUBLE_EXP_MIN;
  ctx->daz = false;
  ctx->ftz = false;
}

void print_information_header(void *context) {
  /* Environnement variable to disable loading message */
  char *silent_load_env = getenv("VFC_BACKENDS_SILENT_LOAD");
  bool silent_load =
      ((silent_load_env == NULL) || (strcasecmp(silent_load_env, "True") != 0))
          ? false
          : true;

  if (silent_load)
    return;

  t_context *ctx = (t_context *)context;

  logger_info(
      "load backend with "
      "%s = %d, "
      "%s = %d, "
      "%s = %d, "
      "%s = %d, "
      "%s = %s, "
      "%s = %s, "
      "%s = %d, "
      "%s = %s, "
      "%s = %s and "
      "%s = %s"
      "\n",
      key_prec_b32_str, VPRECLIB_BINARY32_PRECISION, key_range_b32_str,
      VPRECLIB_BINARY32_RANGE, key_prec_b64_str, VPRECLIB_BINARY64_PRECISION,
      key_range_b64_str, VPRECLIB_BINARY64_RANGE, key_mode_str,
      VPREC_MODE_STR[VPRECLIB_MODE], key_err_mode_str,
      (ctx->relErr && !ctx->absErr)
          ? VPREC_ERR_MODE_STR[vprec_err_mode_rel]
          : (!ctx->relErr && ctx->absErr)
                ? VPREC_ERR_MODE_STR[vprec_err_mode_abs]
                : (ctx->relErr && ctx->absErr)
                      ? VPREC_ERR_MODE_STR[vprec_err_mode_all]
                      : VPREC_ERR_MODE_STR[vprec_err_mode_rel],
      key_err_exp_str, (ctx->absErr_exp), key_daz_str,
      ctx->daz ? "true" : "false", key_ftz_str, ctx->ftz ? "true" : "false",
      key_instrument_str, VPREC_INST_MODE_STR[VPREC_INST_MODE]);
}

void _interflop_finalize(void *context) {
  /* close log file */
  if (vprec_log_file != NULL) {
    fclose(vprec_log_file);
  }
}

struct interflop_backend_interface_t
interflop_init(int argc, char **argv, interflop_function_stack_t call_stack,
               vfc_hashmap_t inst_map, void **context) {

  /* Initialize the logger */
  logger_init();

  /* Setting to default values */
  _set_vprec_precision_binary32(VPREC_PRECISION_BINARY32_DEFAULT);
  _set_vprec_precision_binary64(VPREC_PRECISION_BINARY64_DEFAULT);
  _set_vprec_range_binary32(VPREC_RANGE_BINARY32_DEFAULT);
  _set_vprec_range_binary64(VPREC_RANGE_BINARY64_DEFAULT);
  _set_vprec_mode(VPREC_MODE_DEFAULT);

  t_context *ctx = malloc(sizeof(t_context));
  *context = ctx;
  init_context(ctx);

  /* parse backend arguments */
  argp_parse(&argp, argc, argv, 0, 0, ctx);

  print_information_header(ctx);

  _vfc_inst_map = inst_map;
  _vfc_call_stack = call_stack;

  struct interflop_backend_interface_t interflop_backend_vprec = {
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
      _interflop_enter_function,
      _interflop_exit_function,
      _interflop_finalize};

  return interflop_backend_vprec;
}
