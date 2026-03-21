/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// The number of worker thread, if changes are needed, initialisation of the thread pool must be changes as well
#define BM_WORKER_THREAD_NUM 2

#define BM_THREAD_NUM (BM_WORKER_THREAD_NUM + 1)

#define BM_WORKER_THREAD_STACKSIZE 0x40000
