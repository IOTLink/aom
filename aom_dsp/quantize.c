/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include "aom_dsp/quantize.h"
#include "aom_mem/aom_mem.h"

void quantize_b_helper_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                         int skip_block, const int16_t *zbin_ptr,
                         const int16_t *round_ptr, const int16_t *quant_ptr,
                         const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
                         tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr,
                         uint16_t *eob_ptr, const int16_t *scan,
                         const int16_t *iscan, const qm_val_t *qm_ptr,
                         const qm_val_t *iqm_ptr, const int log_scale) {
  const int zbins[2] = { ROUND_POWER_OF_TWO(zbin_ptr[0], log_scale),
                         ROUND_POWER_OF_TWO(zbin_ptr[1], log_scale) };
  const int nzbins[2] = { zbins[0] * -1, zbins[1] * -1 };
  int i, non_zero_count = (int)n_coeffs, eob = -1;
  (void)iscan;

  memset(qcoeff_ptr, 0, n_coeffs * sizeof(*qcoeff_ptr));
  memset(dqcoeff_ptr, 0, n_coeffs * sizeof(*dqcoeff_ptr));

  if (!skip_block) {
    // Pre-scan pass
    for (i = (int)n_coeffs - 1; i >= 0; i--) {
      const int rc = scan[i];
      const qm_val_t wt = qm_ptr != NULL ? qm_ptr[rc] : (1 << AOM_QM_BITS);
      const int coeff = coeff_ptr[rc] * wt;

      if (coeff < (zbins[rc != 0] * (1 << AOM_QM_BITS)) &&
          coeff > (nzbins[rc != 0] * (1 << AOM_QM_BITS)))
        non_zero_count--;
      else
        break;
    }

    // Quantization pass: All coefficients with index >= zero_flag are
    // skippable. Note: zero_flag can be zero.
    for (i = 0; i < non_zero_count; i++) {
      const int rc = scan[i];
      const int coeff = coeff_ptr[rc];
      const int coeff_sign = (coeff >> 31);
      const int abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
      int tmp32;

      const qm_val_t wt = qm_ptr != NULL ? qm_ptr[rc] : (1 << AOM_QM_BITS);
      if (abs_coeff * wt >= (zbins[rc != 0] << AOM_QM_BITS)) {
        int64_t tmp =
            clamp(abs_coeff + ROUND_POWER_OF_TWO(round_ptr[rc != 0], log_scale),
                  INT16_MIN, INT16_MAX);
        tmp *= wt;
        tmp32 = (int)(((((tmp * quant_ptr[rc != 0]) >> 16) + tmp) *
                       quant_shift_ptr[rc != 0]) >>
                      (16 - log_scale + AOM_QM_BITS));  // quantization
        qcoeff_ptr[rc] = (tmp32 ^ coeff_sign) - coeff_sign;
        const int iwt = iqm_ptr != NULL ? iqm_ptr[rc] : (1 << AOM_QM_BITS);
        const int dequant =
            (dequant_ptr[rc != 0] * iwt + (1 << (AOM_QM_BITS - 1))) >>
            AOM_QM_BITS;
        dqcoeff_ptr[rc] = qcoeff_ptr[rc] * dequant / (1 << log_scale);

        if (tmp32) eob = i;
      }
    }
  }
  *eob_ptr = eob + 1;
}

void highbd_quantize_b_helper_c(
    const tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block,
    const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr,
    const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr,
    const int16_t *scan, const int16_t *iscan, const qm_val_t *qm_ptr,
    const qm_val_t *iqm_ptr, const int log_scale) {
  int i, eob = -1;
  const int zbins[2] = { ROUND_POWER_OF_TWO(zbin_ptr[0], log_scale),
                         ROUND_POWER_OF_TWO(zbin_ptr[1], log_scale) };
  const int nzbins[2] = { zbins[0] * -1, zbins[1] * -1 };
  int dequant;
#if CONFIG_TX64X64
  int idx_arr[4096];
#else
  int idx_arr[1024];
#endif
  (void)iscan;
  int idx = 0;

  memset(qcoeff_ptr, 0, n_coeffs * sizeof(*qcoeff_ptr));
  memset(dqcoeff_ptr, 0, n_coeffs * sizeof(*dqcoeff_ptr));

  if (!skip_block) {
    // Pre-scan pass
    for (i = 0; i < n_coeffs; i++) {
      const int rc = scan[i];
      const qm_val_t wt = qm_ptr != NULL ? qm_ptr[rc] : (1 << AOM_QM_BITS);
      const int coeff = coeff_ptr[rc] * wt;

      // If the coefficient is out of the base ZBIN range, keep it for
      // quantization.
      if (coeff >= (zbins[rc != 0] * (1 << AOM_QM_BITS)) ||
          coeff <= (nzbins[rc != 0] * (1 << AOM_QM_BITS)))
        idx_arr[idx++] = i;
    }

    // Quantization pass: only process the coefficients selected in
    // pre-scan pass. Note: idx can be zero.
    for (i = 0; i < idx; i++) {
      const int rc = scan[idx_arr[i]];
      const int coeff = coeff_ptr[rc];
      const int coeff_sign = (coeff >> 31);
      const qm_val_t wt = qm_ptr != NULL ? qm_ptr[rc] : (1 << AOM_QM_BITS);
      const qm_val_t iwt = iqm_ptr != NULL ? iqm_ptr[rc] : (1 << AOM_QM_BITS);
      const int abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
      const int64_t tmp1 =
          abs_coeff + ROUND_POWER_OF_TWO(round_ptr[rc != 0], log_scale);
      const int64_t tmpw = tmp1 * wt;
      const int64_t tmp2 = ((tmpw * quant_ptr[rc != 0]) >> 16) + tmpw;
      const int abs_qcoeff = (int)((tmp2 * quant_shift_ptr[rc != 0]) >>
                                   (16 - log_scale + AOM_QM_BITS));
      qcoeff_ptr[rc] = (tran_low_t)((abs_qcoeff ^ coeff_sign) - coeff_sign);
      dequant = (dequant_ptr[rc != 0] * iwt + (1 << (AOM_QM_BITS - 1))) >>
                AOM_QM_BITS;
      dqcoeff_ptr[rc] = (qcoeff_ptr[rc] * dequant) / (1 << log_scale);
      if (abs_qcoeff) eob = idx_arr[i];
    }
  }
  *eob_ptr = eob + 1;
}

void quantize_dc_helper(const tran_low_t *coeff_ptr, int n_coeffs,
                        int skip_block, const int16_t *round_ptr,
                        const int16_t quant, tran_low_t *qcoeff_ptr,
                        tran_low_t *dqcoeff_ptr, const int16_t dequant_ptr,
                        uint16_t *eob_ptr, const qm_val_t *qm_ptr,
                        const qm_val_t *iqm_ptr, const int log_scale) {
  const int rc = 0;
  const int coeff = coeff_ptr[rc];
  const int coeff_sign = (coeff >> 31);
  const int abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
  int64_t tmp;
  int eob = -1;
  int32_t tmp32;
  int dequant;

  memset(qcoeff_ptr, 0, n_coeffs * sizeof(*qcoeff_ptr));
  memset(dqcoeff_ptr, 0, n_coeffs * sizeof(*dqcoeff_ptr));

  if (!skip_block) {
    const int wt = qm_ptr != NULL ? qm_ptr[rc] : (1 << AOM_QM_BITS);
    const int iwt = iqm_ptr != NULL ? iqm_ptr[rc] : (1 << AOM_QM_BITS);
    tmp = clamp(abs_coeff + ROUND_POWER_OF_TWO(round_ptr[rc != 0], log_scale),
                INT16_MIN, INT16_MAX);
    tmp32 = (int32_t)((tmp * wt * quant) >> (16 - log_scale + AOM_QM_BITS));
    qcoeff_ptr[rc] = (tmp32 ^ coeff_sign) - coeff_sign;
    dequant = (dequant_ptr * iwt + (1 << (AOM_QM_BITS - 1))) >> AOM_QM_BITS;
    dqcoeff_ptr[rc] = (qcoeff_ptr[rc] * dequant) / (1 << log_scale);
    if (tmp32) eob = 0;
  }
  *eob_ptr = eob + 1;
}

/* These functions should only be called when quantisation matrices
   are not used. */
void aom_quantize_b_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                      int skip_block, const int16_t *zbin_ptr,
                      const int16_t *round_ptr, const int16_t *quant_ptr,
                      const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
                      tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr,
                      uint16_t *eob_ptr, const int16_t *scan,
                      const int16_t *iscan) {
  quantize_b_helper_c(coeff_ptr, n_coeffs, skip_block, zbin_ptr, round_ptr,
                      quant_ptr, quant_shift_ptr, qcoeff_ptr, dqcoeff_ptr,
                      dequant_ptr, eob_ptr, scan, iscan, NULL, NULL, 0);
}

void aom_quantize_b_32x32_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                            int skip_block, const int16_t *zbin_ptr,
                            const int16_t *round_ptr, const int16_t *quant_ptr,
                            const int16_t *quant_shift_ptr,
                            tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                            const int16_t *dequant_ptr, uint16_t *eob_ptr,
                            const int16_t *scan, const int16_t *iscan) {
  quantize_b_helper_c(coeff_ptr, n_coeffs, skip_block, zbin_ptr, round_ptr,
                      quant_ptr, quant_shift_ptr, qcoeff_ptr, dqcoeff_ptr,
                      dequant_ptr, eob_ptr, scan, iscan, NULL, NULL, 1);
}

#if CONFIG_TX64X64
void aom_quantize_b_64x64_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                            int skip_block, const int16_t *zbin_ptr,
                            const int16_t *round_ptr, const int16_t *quant_ptr,
                            const int16_t *quant_shift_ptr,
                            tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                            const int16_t *dequant_ptr, uint16_t *eob_ptr,
                            const int16_t *scan, const int16_t *iscan) {
  quantize_b_helper_c(coeff_ptr, n_coeffs, skip_block, zbin_ptr, round_ptr,
                      quant_ptr, quant_shift_ptr, qcoeff_ptr, dqcoeff_ptr,
                      dequant_ptr, eob_ptr, scan, iscan, NULL, NULL, 2);
}
#endif  // CONFIG_TX64X64

void aom_quantize_dc(const tran_low_t *coeff_ptr, int n_coeffs, int skip_block,
                     const int16_t *round_ptr, const int16_t quant,
                     tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                     const int16_t dequant_ptr, uint16_t *eob_ptr) {
  quantize_dc_helper(coeff_ptr, n_coeffs, skip_block, round_ptr, quant,
                     qcoeff_ptr, dqcoeff_ptr, dequant_ptr, eob_ptr, NULL, NULL,
                     0);
}

void aom_quantize_dc_32x32(const tran_low_t *coeff_ptr, int skip_block,
                           const int16_t *round_ptr, const int16_t quant,
                           tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                           const int16_t dequant_ptr, uint16_t *eob_ptr) {
  quantize_dc_helper(coeff_ptr, 1024, skip_block, round_ptr, quant, qcoeff_ptr,
                     dqcoeff_ptr, dequant_ptr, eob_ptr, NULL, NULL, 1);
}

#if CONFIG_TX64X64
void aom_quantize_dc_64x64(const tran_low_t *coeff_ptr, int skip_block,
                           const int16_t *round_ptr, const int16_t quant,
                           tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                           const int16_t dequant_ptr, uint16_t *eob_ptr) {
  quantize_dc_helper(coeff_ptr, 4096, skip_block, round_ptr, quant, qcoeff_ptr,
                     dqcoeff_ptr, dequant_ptr, eob_ptr, NULL, NULL, 2);
}
#endif  // CONFIG_TX64X64

void aom_highbd_quantize_b_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                             int skip_block, const int16_t *zbin_ptr,
                             const int16_t *round_ptr, const int16_t *quant_ptr,
                             const int16_t *quant_shift_ptr,
                             tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                             const int16_t *dequant_ptr, uint16_t *eob_ptr,
                             const int16_t *scan, const int16_t *iscan) {
  highbd_quantize_b_helper_c(coeff_ptr, n_coeffs, skip_block, zbin_ptr,
                             round_ptr, quant_ptr, quant_shift_ptr, qcoeff_ptr,
                             dqcoeff_ptr, dequant_ptr, eob_ptr, scan, iscan,
                             NULL, NULL, 0);
}

void aom_highbd_quantize_b_32x32_c(
    const tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block,
    const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr,
    const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr,
    const int16_t *scan, const int16_t *iscan) {
  highbd_quantize_b_helper_c(coeff_ptr, n_coeffs, skip_block, zbin_ptr,
                             round_ptr, quant_ptr, quant_shift_ptr, qcoeff_ptr,
                             dqcoeff_ptr, dequant_ptr, eob_ptr, scan, iscan,
                             NULL, NULL, 1);
}

#if CONFIG_TX64X64
void aom_highbd_quantize_b_64x64_c(
    const tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block,
    const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr,
    const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr,
    const int16_t *scan, const int16_t *iscan) {
  highbd_quantize_b_helper_c(coeff_ptr, n_coeffs, skip_block, zbin_ptr,
                             round_ptr, quant_ptr, quant_shift_ptr, qcoeff_ptr,
                             dqcoeff_ptr, dequant_ptr, eob_ptr, scan, iscan,
                             NULL, NULL, 2);
}
#endif  // CONFIG_TX64X64
