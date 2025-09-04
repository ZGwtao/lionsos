/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// The number of worker thread, if changes are needed, initialisation of the thread pool must be changes as well
#define PC_WORKER_THREAD_NUM 4

#define PC_THREAD_NUM (PC_WORKER_THREAD_NUM + 1)

#define PC_WORKER_THREAD_STACKSIZE 0x40000
