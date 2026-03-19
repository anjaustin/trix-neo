/*
 * metrics.h — TriX Runtime Metrics and Observability
 *
 * Provides metrics collection and export for monitoring.
 * Supports Prometheus format for easy integration.
 *
 * Part of TriX v1.0 production deployment
 * Created: March 19, 2026
 */

#ifndef TRIXC_METRICS_H
#define TRIXC_METRICS_H

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize metrics subsystem
 */
void trix_metrics_init(void);

/*
 * Create a counter metric
 * @param name Metric name
 * @param help Description
 * @return Metric index or -1 on error
 */
int trix_counter_create(const char* name, const char* help);

/*
 * Create a gauge metric
 * @param name Metric name
 * @param help Description
 * @return Metric index or -1 on error
 */
int trix_gauge_create(const char* name, const char* help);

/*
 * Increment counter
 * @param idx Metric index
 */
void trix_counter_inc(int idx);

/*
 * Add value to counter
 * @param idx Metric index
 * @param value Value to add
 */
void trix_counter_add(int idx, double value);

/*
 * Set gauge value
 * @param idx Metric index
 * @param value Value to set
 */
void trix_gauge_set(int idx, double value);

/*
 * Get gauge value
 * @param idx Metric index
 * @return Current value
 */
double trix_gauge_get(int idx);

/*
 * Export metrics in Prometheus format
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes written
 */
int trix_metrics_export_prometheus(char* buffer, size_t size);

/*
 * Export metrics in JSON format
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes written
 */
int trix_metrics_export_json(char* buffer, size_t size);

/*
 * Dump metrics to file
 * @param out Output file
 */
void trix_metrics_dump(FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_METRICS_H */