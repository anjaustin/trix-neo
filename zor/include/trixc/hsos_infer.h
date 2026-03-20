/*
 * hsos_infer.h — Bridge between HSOS messaging kernel and TriX inference
 */

#ifndef TRIXC_HSOS_INFER_H
#define TRIXC_HSOS_INFER_H

#include "runtime.h"
#include "../hsos.h"

#ifdef __cplusplus
extern "C" {
#endif

trix_result_t hsos_exec_infer(hsos_system_t *sys,
                               trix_chip_t   *chip,
                               const uint8_t  input[64]);

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_HSOS_INFER_H */
