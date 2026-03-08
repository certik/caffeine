// Workaround for flang submodule compilation not inlining intrinsic module
// procedures. When Fortran submodules use iso_c_binding or ieee_exceptions
// intrinsics, flang emits external calls instead of inlining them.
// This file provides C implementations with the exact ABI flang expects.
//
// See: https://github.com/llvm/llvm-project/issues/ (flang submodule lowering)

#include <stdint.h>

typedef struct { intptr_t address; } c_ptr_t;
typedef struct { intptr_t address; } c_funptr_t;

int _QM__fortran_builtinsPc_associated_c_ptr(const c_ptr_t *p1, const c_ptr_t *p2) {
    if (p2 == 0 || p2->address == 0)
        return p1->address != 0;
    return p1->address == p2->address;
}

int _QM__fortran_builtinsPc_associated_c_funptr(const c_funptr_t *p1, const c_funptr_t *p2) {
    if (p2 == 0 || p2->address == 0)
        return p1->address != 0;
    return p1->address == p2->address;
}

void _QM__fortran_builtinsP__builtin_c_funloc(c_funptr_t *result, void (*func)(void)) {
    result->address = (intptr_t)func;
}

void _QMiso_c_bindingPc_f_procpointer(const c_funptr_t *cptr, void (**fptr)(void)) {
    *fptr = (void (*)(void))cptr->address;
}

void ieee_set_flag_l4_(const int *flag, const int *flag_value) {
    (void)flag;
    (void)flag_value;
}
