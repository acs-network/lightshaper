# L2shaping was changed from DPDK example l3fwd
# the l3fwd License is following
# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2017 Intel Corporation

# meson file, for building this example as part of a main DPDK build.
#
# To build this example as a standalone application with an already-installed
# DPDK instance, use 'make'

deps += ['hash', 'lpm']
sources = files(
	'l3fwd_em.c', 'l3fwd_lpm.c', 'main.c'
)
