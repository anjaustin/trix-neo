/*
 * metrics.c — TriX Runtime Metrics and Observability
 *
 * Provides metrics collection and export for monitoring.
 * Supports Prometheus format for easy integration.
 *
 * Part of TriX v1.0 production deployment
 * Created: March 19, 2026
 */

#include "../include/trixc/metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define METRICS_MAX_NAME_LEN 64
#define METRICS_MAX_LABELS 8
#define METRICS_MAX_VALUES 256

typedef struct {
    char name[METRICS_MAX_NAME_LEN];
    char labels[METRICS_MAX_LABELS][METRICS_MAX_NAME_LEN];
    char label_values[METRICS_MAX_LABELS][METRICS_MAX_NAME_LEN];
    int num_labels;
    double value;
    char help[128];
    int type;  /* 0=counter, 1=gauge, 2=histogram */
} MetricEntry;

static MetricEntry g_metrics[METRICS_MAX_VALUES];
static int g_metric_count = 0;
static pthread_mutex_t g_metrics_mutex = PTHREAD_MUTEX_INITIALIZER;

void trix_metrics_init(void) {
    pthread_mutex_lock(&g_metrics_mutex);
    g_metric_count = 0;
    pthread_mutex_unlock(&g_metrics_mutex);
}

int trix_counter_create(const char* name, const char* help) {
    pthread_mutex_lock(&g_metrics_mutex);
    
    if (g_metric_count >= METRICS_MAX_VALUES) {
        pthread_mutex_unlock(&g_metrics_mutex);
        return -1;
    }
    
    MetricEntry* m = &g_metrics[g_metric_count++];
    strncpy(m->name, name, METRICS_MAX_NAME_LEN - 1);
    strncpy(m->help, help, sizeof(m->help) - 1);
    m->value = 0.0;
    m->type = 0;
    m->num_labels = 0;
    
    int idx = g_metric_count - 1;
    pthread_mutex_unlock(&g_metrics_mutex);
    return idx;
}

int trix_gauge_create(const char* name, const char* help) {
    pthread_mutex_lock(&g_metrics_mutex);
    
    if (g_metric_count >= METRICS_MAX_VALUES) {
        pthread_mutex_unlock(&g_metrics_mutex);
        return -1;
    }
    
    MetricEntry* m = &g_metrics[g_metric_count++];
    strncpy(m->name, name, METRICS_MAX_NAME_LEN - 1);
    strncpy(m->help, help, sizeof(m->help) - 1);
    m->value = 0.0;
    m->type = 1;
    m->num_labels = 0;
    
    int idx = g_metric_count - 1;
    pthread_mutex_unlock(&g_metrics_mutex);
    return idx;
}

void trix_counter_inc(int idx) {
    pthread_mutex_lock(&g_metrics_mutex);
    if (idx >= 0 && idx < g_metric_count) {
        g_metrics[idx].value += 1.0;
    }
    pthread_mutex_unlock(&g_metrics_mutex);
}

void trix_counter_add(int idx, double value) {
    pthread_mutex_lock(&g_metrics_mutex);
    if (idx >= 0 && idx < g_metric_count) {
        g_metrics[idx].value += value;
    }
    pthread_mutex_unlock(&g_metrics_mutex);
}

void trix_gauge_set(int idx, double value) {
    pthread_mutex_lock(&g_metrics_mutex);
    if (idx >= 0 && idx < g_metric_count) {
        g_metrics[idx].value = value;
    }
    pthread_mutex_unlock(&g_metrics_mutex);
}

double trix_gauge_get(int idx) {
    pthread_mutex_lock(&g_metrics_mutex);
    double value = 0.0;
    if (idx >= 0 && idx < g_metric_count) {
        value = g_metrics[idx].value;
    }
    pthread_mutex_unlock(&g_metrics_mutex);
    return value;
}

int trix_metrics_export_prometheus(char* buffer, size_t size) {
    pthread_mutex_lock(&g_metrics_mutex);
    
    size_t pos = 0;
    
    for (int i = 0; i < g_metric_count && pos < size - 100; i++) {
        MetricEntry* m = &g_metrics[i];
        
        const char* type_str = m->type == 0 ? "counter" : (m->type == 1 ? "gauge" : "histogram");
        
        pos += snprintf(buffer + pos, size - pos, "# HELP %s %s\n", m->name, m->help);
        pos += snprintf(buffer + pos, size - pos, "# TYPE %s %s\n", m->name, type_str);
        
        if (m->num_labels > 0) {
            pos += snprintf(buffer + pos, size - pos, "%s{", m->name);
            for (int j = 0; j < m->num_labels && pos < size - 50; j++) {
                if (j > 0) pos += snprintf(buffer + pos, size - pos, ",");
                pos += snprintf(buffer + pos, size - pos, "%s=\"%s\"", 
                               m->labels[j], m->label_values[j]);
            }
            pos += snprintf(buffer + pos, size - pos, "} %.6f\n", m->value);
        } else {
            pos += snprintf(buffer + pos, size - pos, "%.6f\n", m->value);
        }
    }
    
    pthread_mutex_unlock(&g_metrics_mutex);
    return (int)pos;
}

int trix_metrics_export_json(char* buffer, size_t size) {
    pthread_mutex_lock(&g_metrics_mutex);
    
    size_t pos = 0;
    pos += snprintf(buffer + pos, size - pos, "{\n  \"metrics\": [\n");
    
    for (int i = 0; i < g_metric_count && pos < size - 100; i++) {
        MetricEntry* m = &g_metrics[i];
        
        pos += snprintf(buffer + pos, size - pos, "    {\n");
        pos += snprintf(buffer + pos, size - pos, "      \"name\": \"%s\",\n", m->name);
        pos += snprintf(buffer + pos, size - pos, "      \"value\": %.6f,\n", m->value);
        pos += snprintf(buffer + pos, size - pos, "      \"type\": %d\n", m->type);
        pos += snprintf(buffer + pos, size - pos, "    }%s\n", i < g_metric_count - 1 ? "," : "");
    }
    
    pos += snprintf(buffer + pos, size - pos, "  ]\n}\n");
    
    pthread_mutex_unlock(&g_metrics_mutex);
    return (int)pos;
}

void trix_metrics_dump(FILE* out) {
    char buffer[8192];
    trix_metrics_export_prometheus(buffer, sizeof(buffer));
    fprintf(out, "%s", buffer);
}