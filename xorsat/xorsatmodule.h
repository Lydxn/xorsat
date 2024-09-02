#ifndef XORSATMODULE_H
#define XORSATMODULE_H

#include <Python.h>
#include <m4ri/m4ri.h>

typedef enum {
    SHIFT_SHL, SHIFT_SHR, SHIFT_SAR, SHIFT_ROL, SHIFT_ROR
} shift_t;

typedef uint64_t bitset_t;

#define WORD_SIZE (8 * sizeof(bitset_t))
#define BS_SIZE(bits) (((bits) + WORD_SIZE - 1) / WORD_SIZE)

typedef struct {
    PyObject_HEAD
    PyObject *var;
    Py_ssize_t index;
} BitRefObject;

typedef struct {
    PyObject_VAR_HEAD
    Py_ssize_t bits;
    bitset_t buf[1];
} BitSetObject;

typedef struct {
    PyObject_HEAD
    PyObject *system;   /* LinearSystem */
    PyObject *mask;     /* BitSet */
    uint8_t compl;
} BitExprObject;

typedef struct {
    PyObject_VAR_HEAD
    PyObject *system;
    PyObject *exprs[1];
} BitVecObject;

typedef struct {
    PyObject_HEAD
    PyObject *lhs;
    PyObject *rhs;
} ConstraintObject;

typedef struct {
    ConstraintObject super;
} BitVecConstraintObject;

typedef struct {
    PyObject_HEAD
    PyObject *name;
    Py_ssize_t bits;
    Py_ssize_t offset;
} VarInfoObject;

typedef struct {
    PyObject_HEAD
    PyObject *system;
    mzd_t *x;
    mzd_t *kernel;
    uint8_t *state;
} SolveIterObject;

typedef struct {
    PyObject_HEAD
    PyObject **vi_table;
    Py_ssize_t vi_size;
    Py_ssize_t bits;
    PyObject *_expr_const[2];
} LinearSystemObject;

#define BitExpr_Check(obj) PyObject_TypeCheck((obj), &BitExpr_Type)
#define BitVec_Check(obj) PyObject_TypeCheck((obj), &BitVec_Type)

PyObject *varinfo_create(PyTypeObject *type, PyObject *name, Py_ssize_t bits,
                         Py_ssize_t offset);

PyObject *bitref_create(PyTypeObject *type, VarInfoObject *var, Py_ssize_t index);

uint8_t bitset_test(BitSetObject *bs, Py_ssize_t i);
void bitset_set(BitSetObject *bs, Py_ssize_t i, uint8_t val);
PyObject *bitset_from_size(PyTypeObject *type, Py_ssize_t bits, int clear);
void bitset_inplace_xor(BitSetObject *a, BitSetObject *b);
PyObject *bitset_xor_impl(BitSetObject *a, BitSetObject *b);
Py_ssize_t bitset_count_impl(BitSetObject *bs);

PyObject *bitexpr_copy(BitExprObject *expr);
PyObject *bitexpr_from_bit(PyTypeObject *type, uint8_t bit, LinearSystemObject *system);
int getbit(PyObject *num, const char *message);
PyObject *bitexpr_xor_bit(BitExprObject *expr, uint8_t bit);
PyObject *bitexpr_xor_number(BitExprObject *expr, PyObject *num);
PyObject *bitexpr_xor_bitexpr(BitExprObject *a, BitExprObject *b);

PyObject *bitvec_from_size(PyTypeObject *type, Py_ssize_t size, PyObject *system,
                           int zero);
PyObject *bitvec_bitwise_number(BitVecObject *vec, const char op, PyObject *num);

PyObject *constraint_create(PyTypeObject *type, PyObject *lhs, PyObject *rhs);

PyObject *linearsystem_gen_index(LinearSystemObject *self, Py_ssize_t index);

PyObject *generate_model(mzd_t *x, LinearSystemObject *system);

PyObject *mzd_xfree(mzd_t *A)
{
    if (A != NULL)
        mzd_free(A);
}

#endif
