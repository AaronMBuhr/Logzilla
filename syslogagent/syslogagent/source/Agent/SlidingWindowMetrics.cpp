#include "stdafx.h"
#include "SlidingWindowMetrics.h"

SlidingWindowMetrics& SlidingWindowMetrics::instance() {
    static SlidingWindowMetrics instance;
    return instance;
}
