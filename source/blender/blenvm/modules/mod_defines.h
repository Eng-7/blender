/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __MOD_DEFINES_H__
#define __MOD_DEFINES_H__

#include "util_opcode.h"

BVM_MOD_NAMESPACE_BEGIN

#ifdef BVM_MOD_ANNOTATE_FUNCTIONS
#define BVM_MOD_FUNCTION(name) \
	__attribute__((annotate(#name))) inline 
#endif

#define bvm_extern \
	extern "C" inline

#define BVM_DECL_FUNCTION_VALUE(name) \
	template <> inline void *get_node_impl_value<OP_##name>() { return (void*)(intptr_t)name; }

template <OpCode op> static inline void *get_node_impl_value() { return NULL; }

template <OpCode op> static inline void *get_node_impl_deriv0() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv1() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv2() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv3() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv4() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv5() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv6() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv7() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv8() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv9() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv10() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv11() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv12() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv13() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv14() { return NULL; }
template <OpCode op> static inline void *get_node_impl_deriv15() { return NULL; }

BVM_MOD_NAMESPACE_END

#endif /* __MOD_DEFINES_H__ */
